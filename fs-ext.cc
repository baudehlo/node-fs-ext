// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <node.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/file.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <utime.h>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace v8;
using namespace node;

#define THROW_BAD_ARGS \
  ThrowException(Exception::TypeError(String::New("Bad argument")))

struct store_data_t {
  Persistent<Function> *cb;
  int fs_op;  // operation type within this module
  int fd;
  int oper;
  off_t offset;
  struct utimbuf utime_buf;
  char *path;
};

enum
{
  FS_OP_FLOCK,
  FS_OP_SEEK,
  FS_OP_UTIME
};

static int After(eio_req *req) {
  HandleScope scope;

  store_data_t *store_data = static_cast<store_data_t *>(req->data);

  ev_unref(EV_DEFAULT_UC);

  // there is always at least one argument. "error"
  int argc = 1;

  // Allocate space for two args: error plus possible additional result
  Local<Value> argv[2];

  // NOTE: This may need to be changed if something returns a -1
  // for a success, which is possible.
  if (req->result == -1) {
    // If the request doesn't have a path parameter set.
    argv[0] = ErrnoException(req->errorno);
  } else {
    // error value is empty or null for non-error.
    argv[0] = Local<Value>::New(Null());

    switch (store_data->fs_op) {
      // These operations have no data to pass other than "error".
      case FS_OP_FLOCK:
      case FS_OP_UTIME:
        argc = 1;
        break;

      case FS_OP_SEEK:
        argc = 2;
        argv[1] = Number::New(store_data->offset);
        break;

      default:
        assert(0 && "Unhandled op type value");
    }
  }

  TryCatch try_catch;

  (*(store_data->cb))->Call(v8::Context::GetCurrent()->Global(), argc, argv);

  if (try_catch.HasCaught()) {
    FatalException(try_catch);
  }

  // Dispose of the persistent handle
  cb_destroy(store_data->cb);
  delete store_data;

  return 0;
}

static int EIO_Seek(eio_req *req) {
  store_data_t* seek_data = static_cast<store_data_t *>(req->data);

  off_t offs = lseek(seek_data->fd, seek_data->offset, seek_data->oper);

  if (offs == -1) {
    req->result = -1;
    req->errorno = errno;
  } else {
    seek_data->offset = offs;
  }

  return 0;
}

static int EIO_Flock(eio_req *req) {
  store_data_t* flock_data = static_cast<store_data_t *>(req->data);

#ifdef _WIN32
  int i = _win32_flock(flock_data->fd, flock_data->oper);
#else
  int i = flock(flock_data->fd, flock_data->oper);
#endif
  
  req->result = i;
  req->errorno = errno;

  return 0;
}

#ifdef _WIN32
#define LK_LEN          0xffff0000

static int _win32_flock(int fd, int oper) {
  OVERLAPPED o;
  HANDLE fh;

  int i = -1;

  fh = (HANDLE)_get_osfhandle(fd);
  if (fh == (HANDLE)-1)
    return ThrowException(ErrnoException(errno));
  
  memset(&o, 0, sizeof(o));

  switch(oper) {
  case LOCK_SH:               /* shared lock */
      if (LockFileEx(fh, 0, 0, LK_LEN, 0, &o))
        i = 0;
      break;
  case LOCK_EX:               /* exclusive lock */
      if (LockFileEx(fh, LOCKFILE_EXCLUSIVE_LOCK, 0, LK_LEN, 0, &o))
        i = 0;
      break;
  case LOCK_SH|LOCK_NB:       /* non-blocking shared lock */
      if (LockFileEx(fh, LOCKFILE_FAIL_IMMEDIATELY, 0, LK_LEN, 0, &o))
        i = 0;
      break;
  case LOCK_EX|LOCK_NB:       /* non-blocking exclusive lock */
      if (LockFileEx(fh, LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,
                     0, LK_LEN, 0, &o))
        i = 0;
      break;
  case LOCK_UN:               /* unlock lock */
      if (UnlockFileEx(fh, 0, LK_LEN, 0, &o))
        i = 0;
      break;
  default:                    /* unknown */
      errno = EINVAL;
      return -1;
  }
  if (i == -1) {
    if (GetLastError() == ERROR_LOCK_VIOLATION)
      errno = WSAEWOULDBLOCK;
    else
      errno = EINVAL;
  }
  return i;
}
#endif

static Handle<Value> Flock(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 2 || !args[0]->IsInt32() || !args[1]->IsInt32()) {
    return THROW_BAD_ARGS;
  }

  store_data_t* flock_data = new store_data_t();
  
  flock_data->fs_op = FS_OP_FLOCK;
  flock_data->fd = args[0]->Int32Value();
  flock_data->oper = args[1]->Int32Value();

  if (args[2]->IsFunction()) {
    flock_data->cb = cb_persist(args[2]);
    eio_custom(EIO_Flock, EIO_PRI_DEFAULT, After, flock_data);
    ev_ref(EV_DEFAULT_UC);
    return Undefined();
  } else {
#ifdef _WIN32
    int i = _win32_flock(flock_data->fd, flock_data->oper);
#else
    int i = flock(flock_data->fd, flock_data->oper);
#endif
    delete flock_data;
    if (i != 0) return ThrowException(ErrnoException(errno));
    return Undefined();
  }
}


#ifdef _LARGEFILE_SOURCE
static inline int IsInt64(double x) {
  return x == static_cast<double>(static_cast<int64_t>(x));
}
#endif

#ifndef _LARGEFILE_SOURCE
#define ASSERT_OFFSET(a) \
  if (!(a)->IsUndefined() && !(a)->IsNull() && !(a)->IsInt32()) { \
    return ThrowException(Exception::TypeError(String::New("Not an integer"))); \
  }
#define GET_OFFSET(a) ((a)->IsNumber() ? (a)->Int32Value() : -1)
#else
#define ASSERT_OFFSET(a) \
  if (!(a)->IsUndefined() && !(a)->IsNull() && !IsInt64((a)->NumberValue())) { \
    return ThrowException(Exception::TypeError(String::New("Not an integer"))); \
  }
#define GET_OFFSET(a) ((a)->IsNumber() ? (a)->IntegerValue() : -1)
#endif

//  fs.seek(fd, position, whence [, callback] )

static Handle<Value> Seek(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 3 || 
     !args[0]->IsInt32() || 
     !args[2]->IsInt32()) {
    return THROW_BAD_ARGS;
  }

  int fd = args[0]->Int32Value();
  ASSERT_OFFSET(args[1]);
  off_t offs = GET_OFFSET(args[1]);
  int whence = args[2]->Int32Value();

  if ( ! args[3]->IsFunction()) {
    off_t offs_result = lseek(fd, offs, whence);
    if (offs_result == -1) return ThrowException(ErrnoException(errno));
    return scope.Close(Number::New(offs_result));
  }

  store_data_t* seek_data = new store_data_t();

  seek_data->cb = cb_persist(args[3]);
  seek_data->fs_op = FS_OP_SEEK;
  seek_data->fd = fd;
  seek_data->offset = offs;
  seek_data->oper = whence;

  eio_custom(EIO_Seek, EIO_PRI_DEFAULT, After, seek_data);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();
}


static int EIO_UTime(eio_req *req) {
  store_data_t* utime_data = static_cast<store_data_t *>(req->data);

  off_t i = utime(utime_data->path, &utime_data->utime_buf);
  free( utime_data->path );

  if (i == (off_t)-1) {
    req->result = -1;
    req->errorno = errno;
  } else {
    req->result = i;
  }
  
  return 0;
}

// Wrapper for utime(2).
//   fs.utime( path, atime, mtime, [callback] )

static Handle<Value> UTime(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 3 ||
      args.Length() > 4 ||
      !args[0]->IsString() ||
      !args[1]->IsNumber() ||
      !args[2]->IsNumber() ) {
    return THROW_BAD_ARGS;
  }

  String::Utf8Value path(args[0]->ToString());
  time_t atime = args[1]->IntegerValue();
  time_t mtime = args[2]->IntegerValue();

  // Synchronous call needs much less work
  if ( ! args[3]->IsFunction()) {
    struct utimbuf buf;
    buf.actime  = atime;
    buf.modtime = mtime;
    int ret = utime(*path, &buf);
    if (ret != 0) return ThrowException(ErrnoException(errno, "utime", "", *path));
    return Undefined();
  }

  store_data_t* utime_data = new store_data_t();

  utime_data->cb = cb_persist(args[3]);
  utime_data->fs_op = FS_OP_UTIME;
  utime_data->path = strdup(*path);
  utime_data->utime_buf.actime  = atime;
  utime_data->utime_buf.modtime = mtime;

  eio_custom(EIO_UTime, EIO_PRI_DEFAULT, After, utime_data);
  ev_ref(EV_DEFAULT_UC);
  return Undefined();
}



extern "C" void
init (Handle<Object> target)
{
  HandleScope scope;

#ifdef SEEK_SET
  NODE_DEFINE_CONSTANT(target, SEEK_SET);
#endif

#ifdef SEEK_CUR
  NODE_DEFINE_CONSTANT(target, SEEK_CUR);
#endif

#ifdef SEEK_END
  NODE_DEFINE_CONSTANT(target, SEEK_END);
#endif

#ifdef LOCK_SH
  NODE_DEFINE_CONSTANT(target, LOCK_SH);
#endif

#ifdef LOCK_EX
  NODE_DEFINE_CONSTANT(target, LOCK_EX);
#endif

#ifdef LOCK_NB
  NODE_DEFINE_CONSTANT(target, LOCK_NB);
#endif

#ifdef LOCK_UN
  NODE_DEFINE_CONSTANT(target, LOCK_UN);
#endif

  NODE_SET_METHOD(target, "seek", Seek);
  NODE_SET_METHOD(target, "flock", Flock);
  NODE_SET_METHOD(target, "utime", UTime);
}
