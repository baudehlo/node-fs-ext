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

#ifdef _WIN32
#include <windows.h>
#endif

using namespace v8;
using namespace node;

#define THROW_BAD_ARGS \
  ThrowException(Exception::TypeError(String::New("Bad argument")))

struct store_data_t {
  Persistent<Function> *cb;
  int fd;
  int oper;
  off_t offset;
};

static int After(eio_req *req) {
  HandleScope scope;

  store_data_t *store_data = static_cast<store_data_t *>(req->data);

  ev_unref(EV_DEFAULT_UC);

  // there is always at least one argument. "error"
  int argc = 1;

  // Allocate space for one arg.
  Local<Value> argv[1];

  // NOTE: This may be needed to be changed if something returns a -1
  // for a success, which is possible.
  if (req->result == -1) {
    // If the request doesn't have a path parameter set.
    argv[0] = ErrnoException(req->errorno);
  } else {
    // error value is empty or null for non-error.
    argv[0] = Local<Value>::New(Null());
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

  off_t i = lseek(seek_data->fd, seek_data->offset, seek_data->oper);

  if (i == (off_t)-1) {
    req->result = -1;
    req->errorno = errno;
  } else {
    req->result = 0;
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

// TODO: 
static Handle<Value> Seek(const Arguments& args) {
  HandleScope scope;

  if (args.Length() < 3 || 
     !args[0]->IsInt32() || !args[1]->IsInt32() || !args[2]->IsInt32())
  {
    return THROW_BAD_ARGS;
  }

  store_data_t* seek_data = new store_data_t();

  seek_data->fd = args[0]->Int32Value();
  seek_data->oper = args[2]->Int32Value();
  seek_data->offset = args[1]->Int32Value();

  if (args[3]->IsFunction()) {
    seek_data->cb = cb_persist(args[3]);
    eio_custom(EIO_Seek, EIO_PRI_DEFAULT, After, seek_data);
    ev_ref(EV_DEFAULT_UC);
    return Undefined();
  } else {
    off_t i = lseek(seek_data->fd, seek_data->offset, seek_data->oper);
    delete seek_data;
    if (i == (off_t)-1) return ThrowException(ErrnoException(errno));
    return Undefined();
  }

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
}
