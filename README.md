fs-ext
======

Extras not included in Node's fs module.

Installation
------------

Install via npm:

    npm install fs-ext

Usage
-----

fs-ext imports all of the methods from the core 'fs' module, so you don't
need two objects.

    var fs = require('fs-ext');
    var fd = fs.openSync('foo.txt', 'r');
    fs.flock(fd, 'ex', function (err) {
        if (err) {
            return console.log("Couldn't lock file");
        }
        // file is locked
    })

API
---

### fs.flock(fd, flags, [callback])

Asynchronous flock(2). No arguments other than a possible error are passed to
the callback. Flags can be 'sh', 'ex', 'shnb', 'exnb', 'un' and correspond
to the various LOCK_SH, LOCK_EX, LOCK_SH|LOCK_NB, etc.

### fs.flockSync(fd, flags)

Synchronous flock(2). Throws an exception on error.
