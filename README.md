fs-ext
======

[![Build Status][ci-img]][ci-url]
[![Coverage Status][cov-img]][cov-url]
[![Windows Status][ci-win-img]][ci-win-url]

Extras not included in Node's fs module.

Installation
------------

Install via npm:

    npm install fs-ext

Usage
-----

fs-ext imports all of the methods from the core 'fs' module, so you don't
need two objects.

```js
var fs = require('fs-ext');
var fd = fs.openSync('foo.txt', 'r');
fs.flock(fd, 'ex', function (err) {
    if (err) {
        return console.log("Couldn't lock file");
    }
    // file is locked
})
```

For an advanced example checkout `example.js`.

API
---

### fs.flock(fd, flags, [callback])

Asynchronous flock(2). No arguments other than a possible error are passed to
the callback. Flags can be 'sh', 'ex', 'shnb', 'exnb', 'un' and correspond
to the various LOCK_SH, LOCK_EX, LOCK_SH|LOCK_NB, etc.

NOTE (from flock() man page): flock() does not lock files over NFS. Use fcntl(2)
instead: that does work over NFS, given a sufficiently recent version of Linux
and a server which supports locking.


### fs.flockSync(fd, flags)

Synchronous flock(2). Throws an exception on error.

### fs.fcntl(fd, cmd, [arg], [callback])

Asynchronous fcntl(2).

callback will be given two arguments (err, result).

The supported commands are:

- 'getfd' ( F_GETFD )
- 'setfd' ( F_SETFD )
- 'setlk' ( F_SETLK )
- 'getlk' ( F_GETLK )
- 'setlkw' ( F_SETLKW )

Requiring this module adds `FD_CLOEXEC` to the constants module, for use with F_SETFD,
and also F_RDLCK, F_WRLCK and F_UNLCK for use with F_SETLK (etc).

File locking can be used like so:

	fs.fcntl(fd, 'setlkw', constants.F_WRLCK, function(err, result) { 
		if (result!=null) {
			//Lock succeeded
		}
	});

### fs.fcntlSync(fd, flags)

Synchronous fcntl(2). Throws an exception on error.

### fs.seek(fd, offset, whence, [callback])

Asynchronous lseek(2).  

callback will be given two arguments (err, currFilePos).

whence can be 0 (SEEK_SET) to set the new position in bytes to offset, 
1 (SEEK_CUR) to set the new position to the current position plus offset 
bytes (can be negative), or 2 (SEEK_END) to set to the end of the file 
plus offset bytes (usually negative or zero to seek to the end of the file).

### fs.seekSync(fd, offset, whence)

Synchronous lseek(2). Throws an exception on error.  Returns current
file position.


### fs.utime(path [, atime, mtime] [, callback])

Asynchronous utime(2).

Arguments `atime` and `mtime` are in seconds as for the system call.  Note
that the number value of Date() is in milliseconds, so to use the 'now'
value with `fs.utime()` you would have to divide by 1000 first, e.g. 
Date.now()/1000

Just like for utime(2), the absence of the `atime` and `mtime` means 'now'.

### fs.utimeSync(path [, atime, mtime])

Synchronous version of utime().  Throws an exception on error.



[ci-img]: https://travis-ci.org/baudehlo/node-fs-ext.svg?branch=master
[ci-url]: https://travis-ci.org/baudehlo/node-fs-ext
[cov-img]: https://codecov.io/github/baudehlo/node-fs-ext/coverage.svg
[cov-url]: https://codecov.io/github/baudehlo/node-fs-ext?branch=master
[ci-win-img]: https://ci.appveyor.com/api/projects/status/pqbnutckk0n46uc8?svg=true
[ci-win-url]: https://ci.appveyor.com/project/baudehlo/node-fs-ext/branch/master
