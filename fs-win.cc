#ifdef _WIN32

#include "fs-win.h"
#include "autores.h"
#include "winwrap.h"

#include <io.h>
#include <accctrl.h>
#include <aclapi.h>
#include <sddl.h>

// methods:
//   fgetown, getown,
//   fchown, chown
//
// method implementation pattern:
//
// register method_impl as exports.method
// method_impl {
//   if sync:  call method_sync, return convert_result
//   if async: queue method_async with after_async
// }
// method_async {
//   call method_sync
// }
// after_async {
//   for OPERATION_METHOD: return convert_result to callback
// }

namespace fs_win {

using namespace node;
using namespace v8;
using namespace autores;

// helpers for returning errors from native methods
#define THROW_TYPE_ERROR(msg) \
  ThrowException(Exception::TypeError(String::New(msg)))
#define LAST_WINAPI_ERROR \
  ((int) GetLastError())
#define THROW_WINAPI_ERROR(err) \
  ThrowException(WinapiErrnoException(err))
#define THROW_LAST_WINAPI_ERROR \
  THROW_WINAPI_ERROR(LAST_WINAPI_ERROR)

// members names of result object literals
static Persistent<String> uid_symbol;
static Persistent<String> gid_symbol;

// ------------------------------------------------
// internal functions to support the native exports

// class helper to enable and disable taking object ownership in this
// process; it's used explicitly by calling Enable and Disable to be
// able to check for errors, but it supports RAII too for error cases
class TakingOwhership {
  private:
    WinHandle<HANDLE> process;
    bool enabled;

    // changes the privileges necessary for taking ownership
    // in the current process - either enabling or disabling it
    BOOL SetPrivileges(BOOL enable) {
      LPCTSTR const names[] = {
        SE_TAKE_OWNERSHIP_NAME, SE_SECURITY_NAME,
        SE_BACKUP_NAME, SE_RESTORE_NAME
      };

      HeapMem<PTOKEN_PRIVILEGES> privileges =
        HeapMem<PTOKEN_PRIVILEGES>::Allocate(FIELD_OFFSET(
          TOKEN_PRIVILEGES, Privileges[sizeof(names) / sizeof(names[0])]));
      if (privileges == NULL) {
        return FALSE;
      }
      privileges->PrivilegeCount = sizeof(names) / sizeof(names[0]);
      for (size_t i = 0; i < privileges->PrivilegeCount; ++i) {
        if (LookupPrivilegeValue(NULL, names[i],
            &privileges->Privileges[i].Luid) == FALSE) {
          return FALSE;
        }
        privileges->Privileges[i].Attributes =
          enable != FALSE ? SE_PRIVILEGE_ENABLED : 0;
      }

      if (AdjustTokenPrivileges(process, FALSE, privileges,
          sizeof(privileges), NULL, NULL) == FALSE) {
        return FALSE;
      }
      if (GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
        SetLastError(ERROR_NOT_ALL_ASSIGNED);
        return FALSE;
      }

      return TRUE;
    }

  public:
    TakingOwhership() : enabled(false) {}

    ~TakingOwhership() {
      Disable();
    }

    DWORD Enable() {
      if (OpenProcessToken(GetCurrentProcess(),
          TOKEN_ADJUST_PRIVILEGES, &process) == FALSE)  {
        return LAST_WINAPI_ERROR;
      }
      if (SetPrivileges(TRUE) == FALSE) {
        return LAST_WINAPI_ERROR;
      }
      enabled = true;
      return ERROR_SUCCESS;
    }

    DWORD Disable() {
      if (enabled) {
        if (SetPrivileges(FALSE) == FALSE) {
          return LAST_WINAPI_ERROR;
        }
        if (!process.Dispose()) {
          return LAST_WINAPI_ERROR;
        }
        enabled = false;
      }
      return ERROR_SUCCESS;
    }
};

// -----------------------------------------
// support for asynchronous method execution

// codes of exposed native methods
typedef enum {
  OPERATION_FGETOWN,
  OPERATION_GETOWN,
  OPERATION_FCHOWN,
  OPERATION_CHOWN
} operation_t;

// passes input/output parameters between the native method entry point
// and the worker method doing the work, which is called asynchronously
struct async_data_t {
  uv_work_t request;
  Persistent<Function> callback;
  DWORD error;

  operation_t operation;
  int fd;
  HeapMem<LPSTR> path;
  LocalMem<LPSTR> susid, sgsid;

  async_data_t(Local<Function> lcallback) {
    if (!lcallback.IsEmpty()) {
      callback = Persistent<Function>::New(lcallback);
    }
    request.data = this;
  }

  ~async_data_t() {
    if (!callback.IsEmpty()) {
      callback.Dispose();
    }
  }
};

// makes a JavaScript result object literal of user and group SIDs
static Local<Object> convert_ownership(LPSTR uid, LPSTR gid) {
  Local<Object> result = Object::New();
  if (!result.IsEmpty()) {
    result->Set(uid_symbol, String::New(uid));
    result->Set(gid_symbol, String::New(gid));
  }
  return result;
}

// called after an asynchronously called method (method_sync) has
// finished to convert the results to JavaScript objects and pass
// them to JavaScript callback
static void after_async(uv_work_t * req) {
  assert(req != NULL);
  HandleScope scope;

  Local<Value> argv[2];
  int argc = 1;

  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  if (async_data->error != ERROR_SUCCESS) {
    argv[0] = WinapiErrnoException(async_data->error);
  } else {
    // in case of success, make the first argument (error) null
    argv[0] = Local<Value>::New(Null());
    // in case of success, populate the second and other arguments
    switch (async_data->operation) {
      case OPERATION_FGETOWN:
      case OPERATION_GETOWN: {
        argv[1] = convert_ownership(
          async_data->susid, async_data->sgsid);
        argc = 2;
        break;
      }
      case OPERATION_FCHOWN:
      case OPERATION_CHOWN:
        break;
      default:
        assert(FALSE && "Unknown operation");
    }
  }

  // pass the results to the external callback
  TryCatch tryCatch;
  async_data->callback->Call(Context::GetCurrent()->Global(), argc, argv);
  if (tryCatch.HasCaught()) {
    FatalException(tryCatch);
  }

  async_data->callback.Dispose();
  delete async_data;
}

// -------------------------------------------------------
// fgetown - gets the file or directory ownership as SIDs:
// { uid, gid }  fgetown( fd, [callback] )

static int fgetown_sync(int fd, LPSTR *uid, LPSTR *gid) {
  assert(uid != NULL);
  assert(gid != NULL);

  HANDLE fh = (HANDLE) _get_osfhandle(fd);

  PSID usid = NULL, gsid = NULL;
  LocalMem<PSECURITY_DESCRIPTOR> sd;
  DWORD error = GetSecurityInfo(fh, SE_FILE_OBJECT,
    OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
    &usid, &gsid, NULL, NULL, &sd);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  LocalMem<LPSTR> susid;
  if (ConvertSidToStringSid(usid, &susid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  LocalMem<LPSTR> sgsid;
  if (ConvertSidToStringSid(gsid, &sgsid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  *uid = susid.Detach();
  *gid = sgsid.Detach();

  return ERROR_SUCCESS;
}

// passes the execution to fgetown_sync; the results will be processed
// by after_async
static void fgetown_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = fgetown_sync(async_data->fd,
    &async_data->susid, &async_data->sgsid);
}

// the native entry point for the exposed fgetown function
static Handle<Value> fgetown_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("fd required");
  if (argc > 2)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsInt32())
    return THROW_TYPE_ERROR("fd must be an int");
  if (argc > 1 && !args[1]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  int fd = args[0]->Int32Value();
  
  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[1]->IsFunction()) {
    LocalMem<LPSTR> susid, sgsid;
    DWORD error = fgetown_sync(fd, &susid, &sgsid);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Local<Object> result = convert_ownership(susid, sgsid);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[1]));
  async_data->operation = OPERATION_FGETOWN;
  async_data->fd = fd;

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    fgetown_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// ------------------------------------------------------
// getown - gets the file or directory ownership as SIDs:
// { uid, gid }  getown( path, [callback] )

// gets the file ownership (uid and gid) for the file path
static int getown_sync(LPCSTR path, LPSTR *uid, LPSTR *gid) {
  assert(path != NULL);
  assert(uid != NULL);
  assert(gid != NULL);

  PSID usid = NULL, gsid = NULL;
  LocalMem<PSECURITY_DESCRIPTOR> sd;
  DWORD error = GetNamedSecurityInfo(path, SE_FILE_OBJECT,
    OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
    &usid, &gsid, NULL, NULL, &sd);
  if (error != ERROR_SUCCESS) {
    return error;
  }

  LocalMem<LPSTR> susid;
  if (ConvertSidToStringSid(usid, &susid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  LocalMem<LPSTR> sgsid;
  if (ConvertSidToStringSid(gsid, &sgsid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  *uid = susid.Detach();
  *gid = sgsid.Detach();

  return ERROR_SUCCESS;
}

// passes the execution to getown_sync; the results will be processed
// by after_async
static void getown_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = getown_sync(async_data->path,
    &async_data->susid, &async_data->sgsid);
}

// the native entry point for the exposed getown function
static Handle<Value> getown_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("path required");
  if (argc > 2)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsString())
    return THROW_TYPE_ERROR("path must be a string");
  if (argc > 1 && !args[1]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");

  String::Utf8Value path(args[0]->ToString());
  
  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[1]->IsFunction()) {
    LocalMem<LPSTR> susid, sgsid;
    DWORD error = getown_sync(*path, &susid, &sgsid);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    Local<Object> result = convert_ownership(susid, sgsid);
    return scope.Close(result);
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[1]));
  async_data->operation = OPERATION_GETOWN;
  async_data->path = HeapStrDup(HeapBase::ProcessHeap(), *path);
  if (!async_data->path.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    getown_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// --------------------------------------------------------
// fchown - sets the file or directory ownership with SIDs:
// fchown( fd, uid, gid, [callback] )

// change the ownership (uid and gid) of the file specified by the
// file descriptor; either uid or gid can be empty ("") to change
// just one of them
static int fchown_sync(int fd, LPCSTR uid, LPCSTR gid) {
  assert(uid != NULL);
  assert(gid != NULL);

  // get the OS file handle for the specified file descriptor
  HANDLE fh = (HANDLE) _get_osfhandle(fd);

  // convert the input SIDs from strings to SID structures
  LocalMem<PSID> usid;
  if (*uid && ConvertStringSidToSid(uid, &usid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }
  LocalMem<PSID> gsid;
  if (*gid && ConvertStringSidToSid(gid, &gsid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // enable taking object ownership in the current process
  // if the effective user has enough permissions
  TakingOwhership takingOwhership;
  DWORD error = takingOwhership.Enable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // take ownership of the object specified by the file handle
  if (*uid && *gid) {
    if (SetSecurityInfo(fh, SE_FILE_OBJECT,
          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
          usid, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return LAST_WINAPI_ERROR;
    }
  } else if (*uid) {
    if (SetSecurityInfo(fh, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
          usid, NULL, NULL, NULL) != ERROR_SUCCESS) {
      return LAST_WINAPI_ERROR;
    }
  } else if (*gid) {
    if (SetSecurityInfo(fh, SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
          NULL, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return LAST_WINAPI_ERROR;
    }
  }

  // disnable taking object ownership in the current process
  // not to leak the availability of this privileged operation
  error = takingOwhership.Disable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  return ERROR_SUCCESS;
}

// passes the execution to fchown_sync; the results will be processed
// by after_async
static void fchown_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = fchown_sync(async_data->fd,
    async_data->susid, async_data->sgsid);
}

// the native entry point for the exposed fchown function
static Handle<Value> fchown_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("fd required");
  if (argc > 4)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsInt32())
    return THROW_TYPE_ERROR("fd must be an int");
  if (argc < 2)
    return THROW_TYPE_ERROR("uid required");
  if (!args[1]->IsString() && !args[1]->IsUndefined())
    return THROW_TYPE_ERROR("uid must be a string or undefined");
  if (argc < 3)
    return THROW_TYPE_ERROR("gid required");
  if (!args[2]->IsString() && !args[2]->IsUndefined())
    return THROW_TYPE_ERROR("gid must be a string or undefined");
  if (argc > 3 && !args[3]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");
  if (args[1]->IsUndefined() && args[2]->IsUndefined())
    return THROW_TYPE_ERROR("either uid or gid must be defined");

  int fd = args[0]->Int32Value();
  String::AsciiValue susid(args[1]->IsString() ?
    args[1]->ToString() : String::New(""));
  String::AsciiValue sgsid(args[2]->IsString() ?
    args[2]->ToString() : String::New(""));

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[3]->IsFunction()) {
    DWORD error = fchown_sync(fd, *susid, *sgsid);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    return Undefined();
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[3]));
  async_data->operation = OPERATION_FCHOWN;
  async_data->fd = fd;
  async_data->susid = LocalStrDup(*sgsid);
  if (!async_data->susid.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }
  async_data->sgsid = LocalStrDup(*susid);
  if (!async_data->sgsid.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    fchown_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// -------------------------------------------------------
// chown - sets the file or directory ownership with SIDs:
// chown( name, uid, gid, [callback] )

// change the ownership (uid and gid) of the file specified by the
// file path; either uid or gid can be empty ("") to change
// just one of them
static int chown_sync(LPCSTR path, LPCSTR uid, LPCSTR gid) {
  assert(path != NULL);
  assert(uid != NULL);
  assert(gid != NULL);

  // convert the input SIDs from strings to SID structures
  LocalMem<PSID> usid;
  if (*uid && ConvertStringSidToSid(uid, &usid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }
  LocalMem<PSID> gsid;
  if (*gid && ConvertStringSidToSid(gid, &gsid) == FALSE) {
    return LAST_WINAPI_ERROR;
  }

  // enable taking object ownership in the current process
  // if the effective user has enough permissions
  TakingOwhership takingOwhership;
  DWORD error = takingOwhership.Enable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  // take ownership of the object specified by its path
  if (*uid && *gid) {
    if (SetNamedSecurityInfo(const_cast<LPSTR>(path), SE_FILE_OBJECT,
          OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
          usid, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return LAST_WINAPI_ERROR;
    }
  } else if (*uid) {
    if (SetNamedSecurityInfo(const_cast<LPSTR>(path),
          SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, 
          usid, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return LAST_WINAPI_ERROR;
    }
  } else if (*gid) {
    if (SetNamedSecurityInfo(const_cast<LPSTR>(path),
          SE_FILE_OBJECT, GROUP_SECURITY_INFORMATION,
          NULL, gsid, NULL, NULL) != ERROR_SUCCESS) {
      return LAST_WINAPI_ERROR;
    }
  }

  // disnable taking object ownership in the current process
  // not to leak the availability of this privileged operation
  error = takingOwhership.Disable();
  if (error != ERROR_SUCCESS) {
    return error;
  }

  return ERROR_SUCCESS;
}

// passes the execution to chown_sync; the results will be processed
// by after_async
static void chown_async(uv_work_t * req) {
  assert(req != NULL);
  async_data_t * async_data = static_cast<async_data_t *>(req->data);
  async_data->error = chown_sync(async_data->path,
    async_data->susid, async_data->sgsid);
}

// the native entry point for the exposed chown function
static Handle<Value> chown_impl(Arguments const & args) {
  HandleScope scope;

  int argc = args.Length();
  if (argc < 1)
    return THROW_TYPE_ERROR("path required");
  if (argc > 4)
    return THROW_TYPE_ERROR("too many arguments");
  if (!args[0]->IsString())
    return THROW_TYPE_ERROR("path must be a string");
  if (argc < 2)
    return THROW_TYPE_ERROR("uid required");
  if (!args[1]->IsString() && !args[1]->IsUndefined())
    return THROW_TYPE_ERROR("uid must be a string or undefined");
  if (argc < 3)
    return THROW_TYPE_ERROR("gid required");
  if (!args[2]->IsString() && !args[2]->IsUndefined())
    return THROW_TYPE_ERROR("gid must be a string or undefined");
  if (argc > 3 && !args[3]->IsFunction())
    return THROW_TYPE_ERROR("callback must be a function");
  if (args[1]->IsUndefined() && args[2]->IsUndefined())
    return THROW_TYPE_ERROR("either uid or gid must be defined");

  String::Utf8Value path(args[0]->ToString());
  String::AsciiValue susid(args[1]->IsString() ?
    args[1]->ToString() : String::New(""));
  String::AsciiValue sgsid(args[2]->IsString() ?
    args[2]->ToString() : String::New(""));

  // if no callback was provided, assume the synchronous scenario,
  // call the method_sync immediately and return its results
  if (!args[3]->IsFunction()) {
    DWORD error = chown_sync(*path, *susid, *sgsid);
    if (error != ERROR_SUCCESS) {
      return THROW_WINAPI_ERROR(error);
    }
    return Undefined();
  }

  // prepare parameters for the method_sync to be called later
  // from the method_async called from the worker thread
  CppObj<async_data_t *> async_data = new async_data_t(
    Local<Function>::Cast(args[3]));
  async_data->operation = OPERATION_CHOWN;
  async_data->path = HeapStrDup(HeapBase::ProcessHeap(), *path);
  if (!async_data->path.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }
  async_data->susid = LocalStrDup(*sgsid);
  if (!async_data->susid.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }
  async_data->sgsid = LocalStrDup(*susid);
  if (!async_data->sgsid.IsValid()) {
    return THROW_LAST_WINAPI_ERROR;
  }

  // queue the method_async to be called when posibble and
  // after_async to send its result to the external callback
  uv_queue_work(uv_default_loop(), &async_data->request,
    chown_async, (uv_after_work_cb) after_async);

  async_data.Detach();
  return Undefined();
}

// exposes methods implemented by this sub-package and initializes the
// string symbols for the converted resulting object literals; to be
// called from the add-on module-initializing function
void init(Handle<Object> target) {
  HandleScope scope;

  NODE_SET_METHOD(target, "fgetown", fgetown_impl);
  NODE_SET_METHOD(target, "getown", getown_impl);
  NODE_SET_METHOD(target, "fchown", fchown_impl);
  NODE_SET_METHOD(target, "chown", chown_impl);

  uid_symbol = NODE_PSYMBOL("uid");
  gid_symbol = NODE_PSYMBOL("gid");
}

} // namespace fs_win

#endif // _WIN32
