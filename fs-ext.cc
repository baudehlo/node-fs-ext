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
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/file.h>
#include <unistd.h>
#include <utime.h>
#include <sys/statvfs.h>
#endif

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <sys/utime.h>
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
#ifndef _WIN32
  struct statvfs statvfs_buf;
#endif
  char *path;
  int error;
  int result;
};

#ifndef _WIN32
static Persistent<String> f_namemax_symbol;
static Persistent<String> f_bsize_symbol;
static Persistent<String> f_frsize_symbol;

static Persistent<String> f_blocks_symbol;
static Persistent<String> f_bavail_symbol;
static Persistent<String> f_bfree_symbol;

static Persistent<String> f_files_symbol;
static Persistent<String> f_favail_symbol;
static Persistent<String> f_ffree_symbol;
#endif

#ifdef _WIN32
  const int LOCK_SH=1;
  const int LOCK_EX=2;
  const int LOCK_NB=4;
  const int LOCK_UN=8;
#endif

enum
{
  FS_OP_FLOCK,
  FS_OP_SEEK,
  FS_OP_UTIME,
  FS_OP_STATVFS
};

static void EIO_After(uv_work_t *req) {
  HandleScope scope;

  store_data_t *store_data = static_cast<store_data_t *>(req->data);

  // there is always at least one argument. "error"
  int argc = 1;

  // Allocate space for two args: error plus possible additional result
  Local<Value> argv[2];
  Local<Object> statvfs_result;
  // NOTE: This may need to be changed if something returns a -1
  // for a success, which is possible.
  if (store_data->result == -1) {
    // If the request doesn't have a path parameter set.
    argv[0] = ErrnoException(store_data->error);
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
      case FS_OP_STATVFS:
#ifndef _WIN32
        argc = 2;
        statvfs_result = Object::New();
        argv[1] = statvfs_result;
        statvfs_result->Set(f_namemax_symbol, Integer::New(store_data->statvfs_buf.f_namemax));
        statvfs_result->Set(f_bsize_symbol, Integer::New(store_data->statvfs_buf.f_bsize));
        statvfs_result->Set(f_frsize_symbol, Integer::New(store_data->statvfs_buf.f_frsize));
        statvfs_result->Set(f_blocks_symbol, Number::New(store_data->statvfs_buf.f_blocks));
        statvfs_result->Set(f_bavail_symbol, Number::New(store_data->statvfs_buf.f_bavail));
        statvfs_result->Set(f_bfree_symbol, Number::New(store_data->statvfs_buf.f_bfree));
        statvfs_result->Set(f_files_symbol, Number::New(store_data->statvfs_buf.f_files));
        statvfs_result->Set(f_favail_symbol, Number::New(store_data->statvfs_buf.f_favail));
        statvfs_result->Set(f_ffree_symbol, Number::New(store_data->statvfs_buf.f_ffree));
#else
        argc = 1;
#endif
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
  delete req;
}

static void EIO_StatVFS(uv_work_t *req) {
  store_data_t* statvfs_data = static_cast<store_data_t *>(req->data);
  statvfs_data->result = 0;
#ifndef _WIN32  
  struct statvfs *data = &(statvfs_data->statvfs_buf);
  if (statvfs(statvfs_data->path, data)) {
    statvfs_data->result = -1;
  	memset(data, 0, sizeof(struct statvfs));
  };
#endif
  free(statvfs_data->path);	
  ;
}

static void EIO_Seek(uv_work_t *req) {
  store_data_t* seek_data = static_cast<store_data_t *>(req->data);

  off_t offs = lseek(seek_data->fd, seek_data->offset, seek_data->oper);

  if (offs == -1) {
    seek_data->result = -1;
    seek_data->error = errno;
  } else {
    seek_data->offset = offs;
  }

}

#ifdef _WIN32
#define LK_LEN          0xffff0000

static int _win32_flock(int fd, int oper) {
  OVERLAPPED o;
  HANDLE fh;

  int i = -1;

  fh = (HANDLE)_get_osfhandle(fd);
  if (fh == (HANDLE)-1)
    return -1;
  
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

static void EIO_Flock(uv_work_t *req) {
  store_data_t* flock_data = static_cast<store_data_t *>(req->data);

#ifdef _WIN32
  int i = _win32_flock(flock_data->fd, flock_data->oper);
#else
  int i = flock(flock_data->fd, flock_data->oper);
#endif
  
  flock_data->result = i;
  flock_data->error = errno;

}

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
    uv_work_t *req = new uv_work_t;
    req->data = flock_data;
    uv_queue_work(uv_default_loop(), req, EIO_Flock, (uv_after_work_cb)EIO_After);
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

  uv_work_t *req = new uv_work_t;
  req->data = seek_data;
  uv_queue_work(uv_default_loop(), req, EIO_Seek, (uv_after_work_cb)EIO_After);

  return Undefined();
}


static void EIO_UTime(uv_work_t *req) {
  store_data_t* utime_data = static_cast<store_data_t *>(req->data);

  off_t i = utime(utime_data->path, &utime_data->utime_buf);
  free( utime_data->path );

  if (i == (off_t)-1) {
    utime_data->result = -1;
    utime_data->error = errno;
  } else {
    utime_data->result = i;
  }
  
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

  uv_work_t *req = new uv_work_t;
  req->data = utime_data;
  uv_queue_work(uv_default_loop(), req, EIO_UTime, (uv_after_work_cb)EIO_After);

  return Undefined();
}

// Wrapper for statvfs(2).
//   fs.statVFS( path, [callback] )

static Handle<Value> StatVFS(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 1 ||
      !args[0]->IsString() ) {
    return THROW_BAD_ARGS;
  }

  String::Utf8Value path(args[0]->ToString());
  
  // Synchronous call needs much less work
  if (!args[1]->IsFunction()) {
#ifndef _WIN32  
    struct statvfs buf;
    int ret = statvfs(*path, &buf);
    if (ret != 0) return ThrowException(ErrnoException(errno, "statvfs", "", *path));
    Handle<Object> result = Object::New();
    result->Set(f_namemax_symbol, Integer::New(buf.f_namemax));
    result->Set(f_bsize_symbol, Integer::New(buf.f_bsize));
    result->Set(f_frsize_symbol, Integer::New(buf.f_frsize));
    
    result->Set(f_blocks_symbol, Number::New(buf.f_blocks));
    result->Set(f_bavail_symbol, Number::New(buf.f_bavail));
    result->Set(f_bfree_symbol, Number::New(buf.f_bfree));
    
    result->Set(f_files_symbol, Number::New(buf.f_files));
    result->Set(f_favail_symbol, Number::New(buf.f_favail));
    result->Set(f_ffree_symbol, Number::New(buf.f_ffree));
    return result;
#else
    return Undefined();
#endif
  }

  store_data_t* statvfs_data = new store_data_t();

  statvfs_data->cb = cb_persist(args[1]);
  statvfs_data->fs_op = FS_OP_STATVFS;
  statvfs_data->path = strdup(*path);

  uv_work_t *req = new uv_work_t;
  req->data = statvfs_data;
  uv_queue_work(uv_default_loop(), req, EIO_StatVFS,(uv_after_work_cb)EIO_After);

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
  NODE_SET_METHOD(target, "statVFS", StatVFS);
#ifndef _WIN32
  f_namemax_symbol = NODE_PSYMBOL("f_namemax");
  f_bsize_symbol = NODE_PSYMBOL("f_bsize");
  f_frsize_symbol = NODE_PSYMBOL("f_frsize");
  
  f_blocks_symbol = NODE_PSYMBOL("f_blocks");
  f_bavail_symbol = NODE_PSYMBOL("f_bavail");
  f_bfree_symbol = NODE_PSYMBOL("f_bfree");
  
  f_files_symbol = NODE_PSYMBOL("f_files");
  f_favail_symbol = NODE_PSYMBOL("f_favail");
  f_ffree_symbol = NODE_PSYMBOL("f_ffree");
#endif
}

#if NODE_MODULE_VERSION > 1
  NODE_MODULE(fs_ext, init)
#endif
