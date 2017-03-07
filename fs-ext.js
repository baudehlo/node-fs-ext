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

"use strict";

var binding = require('./build/Release/fs-ext');
var fs = require('fs');
var constants = require('constants');

// Used by flock
function stringToFlockFlags(flag) {
  // Only mess with strings
  if (typeof flag !== 'string') {
    return flag;
  }
  switch (flag) {
    case 'sh':
      return binding.LOCK_SH;
    
    case 'ex':
      return binding.LOCK_EX;
    
    case 'shnb':
      return binding.LOCK_SH | binding.LOCK_NB;
    
    case 'exnb':
      return binding.LOCK_EX | binding.LOCK_NB;
    
    case 'un':
      return binding.LOCK_UN;
    
    default:
      throw new Error('Unknown flock flag: ' + flag);
  }
}

// used by Fcntl
function stringToFcntlFlags(flag) {
  if (typeof flag !== 'string') {
    return flag;
  }

  switch (flag) {
    case 'getfd':
      return binding.F_GETFD;
    
    case 'setfd':
      return binding.F_SETFD;
    
    default:
      throw new Error('Unknown fcntl flag: ' + flag);
  }
}

function noop() {}

exports.flock = function(fd, flags, callback) {
  callback = arguments[arguments.length - 1];
  if (typeof(callback) !== 'function') {
    callback = noop;
  }

  var oper = stringToFlockFlags(flags);

  binding.flock(fd, oper, callback);
};

exports.flockSync = function(fd, flags) {
  var oper = stringToFlockFlags(flags);

  return binding.flock(fd, oper);
};

exports.fcntl = function(fd, cmd, arg, callback) {
  cmd = stringToFcntlFlags(cmd);
  if (arguments.length < 4) {
    callback = arg;
    arg = 0;
  }
  if (!arg) arg = 0;
  return binding.fcntl(fd, cmd, arg, callback);
};

exports.fcntlSync = function(fd, cmd, arg) {
  cmd = stringToFcntlFlags(cmd);
  if (!arg) arg = 0;
  return binding.fcntl(fd, cmd, arg);
};

exports.seek = function(fd, position, whence, callback) {
  callback = arguments[arguments.length - 1];
  if (typeof(callback) !== 'function') {
    callback = noop;
  }

  binding.seek(fd, position, whence, callback);
};

exports.seekSync = function(fd, position, whence) {
  return binding.seek(fd, position, whence);
};


// fs.utime('foo' [, atime, mtime] [, func] )

exports.utime = function(path, atime, mtime, callback) {
  callback = arguments[arguments.length - 1];
  if (typeof(callback) !== 'function') {
    callback = noop;
  }

  if (typeof(atime) !== 'number'  &&  typeof(mtime) !== 'number') {
    atime = mtime = Date.now() / 1000;
  }

  binding.utime(path, atime, mtime, callback);
};

// fs.utimeSync('foo' [, atime, mtime] )

exports.utimeSync = function(path, atime, mtime) {

  if (typeof(atime) !== 'number'  &&  typeof(mtime) !== 'number') {
    atime = mtime = Date.now() / 1000;
  }

  return binding.utime(path, atime, mtime);
};

exports.statVFS = function(path, callback) {
  path = path || '/';
  return binding.statVFS(path, callback);
};

// merges all members from the source object to the target object;
// it's like the underscore.extend
function merge(target, source) {
  var key;
  for (key in source) {
    if (source.hasOwnProperty(key)) {
      target[key] = source[key];
    }
  }
}

// stat and chown function group is reimplemented to work
// with SIDs for Windows only
var fsExt = process.platform.match(/^win/i) ?
  (function () {

    // the result of fs.readlink is the exact string used when the link
    // was created by `ln -s`, which can be a relative path; however,
    // the path was relative to the current directory when the command
    // was executed; not to the path of the link; if it wasn't so, this
    // method will resolve to an invalid path and that's wahy you should
    // always create links using the absolute target path
    function resolveLink(fpath, lpath) {
      // check if the path is absolute on both Windows and POSIX platforms
      if (/^([a-z]:)?[\/\\]/i.test(lpath)) {
        return lpath;
      }
      return path.join(path.dirname(fpath), lpath);
    }

    var path = require("path"),

      // declare the extra methods for the built-in fs module
      // which provide the POSIX functionality on Windows
      fsExt = (function () {

        // merges the ownership to the stats
        function completeStats(stats, fd, callback) {
          // allow calling with both fd and path
          (typeof fd === "string" ? binding.getown :
            binding.fgetown)(fd, function(error, ownership) {
              if (error) {
                callback(error);
              }
              else {
                // replace the uid and gid members in the original stats
                // with the values containing SIDs
                merge(stats, ownership);
                callback(undefined, stats);
              }
            });
        }

        // merges the ownership to the stats
        function completeStatsSync(stats, fd) {
          // allow calling with both fd and path
          var ownership = (typeof fd === "string" ?
            binding.getown : binding.fgetown)(fd);
          // replace the uid and gid members in the original stats
          // with the values containing SIDs
          merge(stats, ownership);
          return stats;
        }

        return {

          // fs.fstat returning uid and gid as SIDs
          fstat: function(fd, callback) {
            // get the built-in stats which work on Windows too
            fs.fstat(fd, function(error, stats) {
              if (error) {
                callback(error);
              }
              else {
                // replace the ownership information (uid and gid)
                // with the data useful on Windows - principal SIDs
                completeStats(stats, fd, callback);
              }
            });
          },

          // fs.fstatSync returning uid and gid as SIDs
          fstatSync: function(fd) {
            // get the built-in stats which work on Windows too
            var stats = fs.fstatSync(fd);
            // replace the ownership information (uid and gid)
            // with the data useful on Windows - principal SIDs
            return completeStatsSync(stats, fd);
          },

          // fs.stat returning uid and gid as SIDs
          stat: function(fpath, callback) {
            // get the built-in stats which work on Windows too
            fs.lstat(fpath, function(error, stats) {
              if (error) {
                callback(error);
              }
              else if (stats.isSymbolicLink()) {
                // GetNamedSecurityInfo, which is used by binding.getown,
                // doesn't resolve sybolic links automatically; do the
                // resolution here and call the lstat implementation
                
                fs.readlink(fpath, function(error, lpath) {
                  if (error) {
                    callback(error);
                  }
                  else {
                    fpath = resolveLink(fpath, lpath);
                    fsExt.lstat(fpath, callback);
                  }
                });
              }
              else {
                // replace the ownership information (uid and gid)
                // with the data useful on Windows - principal SIDs
                completeStats(stats, fpath, callback);
              }
            });
          },

          // fs.statSync returning uid and gid as SIDs
          statSync: function(fpath) {
            // get the built-in stats which work on Windows too
            // GetNamedSecurityInfo, which is used by binding.getown,
            // doesn't resolve sybolic links automatically; do the
            // resolution here and call the lstat implementation
            var stats = fs.lstatSync(fpath);
            if (stats.isSymbolicLink()) {
              var lpath = fs.readlinkSync(fpath);
              fpath = resolveLink(fpath, lpath);
              return fsExt.lstatSync(fpath);
            }
            // replace the ownership information (uid and gid)
            // with the data useful on Windows - principal SIDs
            return completeStatsSync(stats, fpath);
          },

          // fs.lstat returning uid and gid as SIDs
          lstat: function(fpath, callback) {
            // get the built-in stats which work on Windows too
            fs.lstat(fpath, function(error, stats) {
              if (error) {
                callback(error);
              }
              else {
                // replace the ownership information (uid and gid)
                // with the data useful on Windows - principal SIDs
                completeStats(stats, fpath, callback);
              }
            });
          },

          // fs.lstatSync returning uid and gid as SIDs
          lstatSync: function(fpath) {
            // get the built-in stats which work on Windows too
            // GetNamedSecurityInfo, which is used by binding.getown,
            // doesn't resolve sybolic links automatically; it's
            // suitable for the lstat implementation as-is
            var stats = fs.lstatSync(fpath);
            // replace the ownership information (uid and gid)
            // with the data useful on Windows - principal SIDs
            return completeStatsSync(stats, fpath);
          },

          // fs.fchown accepting uid and gid as SIDs
          fchown: function(fd, uid, gid, callback) {
            binding.fchown(fd, uid, gid, function(error) {
              callback(error);
            });
          },

          // fs.fchownSync accepting uid and gid as SIDs
          fchownSync: function(fd, uid, gid) {
            binding.fchown(fd, uid, gid);
          },

          // fs.chown accepting uid and gid as SIDs
          chown: function(fpath, uid, gid, callback) {
            fs.lstat(fpath, function(error, stats) {
              if (error) {
                callback(error);
              }
              else if (stats.isSymbolicLink()) {
                fs.readlink(fpath, function(error, lpath) {
                  if (error) {
                    callback(error);
                  }
                  else {
                    fpath = resolveLink(fpath, lpath);
                    fsExt.lchown(fpath, uid, gid, callback);
                  }
                });
              }
              else {
                fsExt.lchown(fpath, uid, gid, callback);
              }
            });
          },

          // fs.chownSync accepting uid and gid as SIDs
          chownSync: function(fpath, uid, gid) {
            // SetNamedSecurityInfo, which is used by binding.chown,
            // doesn't resolve sybolic links automatically; do the
            // resolution here and call the lchown implementation
            var stats = fs.lstatSync(fpath);
            if (stats.isSymbolicLink()) {
              var lpath = fs.readlinkSync(fpath);
              fpath = resolveLink(fpath, lpath);
            }
            fsExt.lchownSync(fpath, uid, gid);
          },

          // fs.lchown accepting uid and gid as SIDs
          lchown: function(fpath, uid, gid, callback) {
            binding.chown(fpath, uid, gid, function(error) {
              callback(error);
            });
          },

          // fs.lchownSync accepting uid and gid as SIDs
          lchownSync: function(fpath, uid, gid) {
            // SetNamedSecurityInfo, which is used by binding.chown,
            // doesn't resolve sybolic links automatically; it's
            // suitable for the lchown implementation as-is
            binding.chown(fpath, uid, gid);
          }

        };

      }());

    return fsExt;
  }())
  :
  // the implementation doesn't need the native add-on on POSIX
  {};

// populate with fs functions from there
merge(exports, fs);

// replace the functions enhanced on Windows
merge(exports, fsExt);

// put constants into constants module (don't like doing this but...)
for (var key in binding) {
  if (/^[A-Z_]+$/.test(key) && !constants.hasOwnProperty(key)) {
    constants[key] = binding[key];
  }
}

