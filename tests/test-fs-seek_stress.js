
// Stress test these APIs published in extension module 'fs-ext'
// Specifically, try to exercise any memory leaks by simple repetition.

// ### fs.seek(fd, offset, whence, [callback])

// ### fs.seekSync(fd, offset, whence)

// Ideas for testing borrowed from bnoordhuis (Ben Noordhuis)


//TODO and Questions


// Make this new-built copy of fs-ext available for testing
require.paths.unshift(__dirname + '/..');
//  console.log( require.resolve('../fs-ext'));

var assert = require('assert'),
    path   = require('path'),
    util   = require('util'),
    fs     = require('../fs-ext');

var tests_ok  = 0,
    tests_run = 0;

var debug_me = true;
    debug_me = false;

var tmp_dir = "/tmp",
    file_path     = path.join(tmp_dir, 'what.when.seek.test'),
    file_path_not = path.join(tmp_dir, 'what.not.seek.test');

var file_fd,
    err;


// Report on test results -  -  -  -  -  -  -  -  -  -  -  -

// Clean up and report on final success or failure of tests here
process.addListener('exit', function() {

  console.log('');
  console.log('  After all testing:');
  display_memory_usage_now();
  console.log('    End time is %s', new Date());

  try {
    fs.closeSync(file_fd);
  } catch (e) {
    // might not be open, that's okay.
  }

  remove_file_wo_error(file_path);

  console.log('Tests run: %d     ok: %d', tests_run, tests_ok);
  assert.equal(tests_ok, tests_run, 'One or more subtests failed');
});


// Test helpers -  -  -  -  -  -  -  -  -  -  -  -  -  -  -

function remove_file_wo_error(file_path) {
  try {
    fs.unlinkSync(file_path);
  } catch (e) {
    // might not exist, that's okay.
  }
}

function display_memory_usage_now() {
  var usage = process.memoryUsage();
  console.log('    memory:  heapUsed  %d      rss       %d', 
                                usage.heapUsed,  usage.rss);
  console.log('             heapTotal %d      vsize     %d', 
                                usage.heapTotal, usage.vsize);
}

function expect_errno(api_name, resource, err, expected_errno) {
  var fault_msg;

  if (debug_me) console.log('  expected_errno(err): ' + err );

  if ( err  &&  err.code !== expected_errno ) {
      fault_msg = api_name + '(): expected error ' + expected_errno + ', got another error';
  } else if ( !err ) {
    fault_msg = api_name + '(): expected error ' + expected_errno + ', got another error';
  }

  if ( ! fault_msg ) {
    tests_ok++;
    if (debug_me) console.log(' FAILED OK: ' + api_name );
  } else {
    console.log('FAILURE: ' + arguments.callee.name + ': ' + fault_msg);
    console.log('   ARGS: ', util.inspect(arguments));
  }
}

function expect_ok(api_name, resource, err) {
  var fault_msg;

  if ( err ) {
    fault_msg = api_name + '(): returned error';
  }

  if ( ! fault_msg ) {
    tests_ok++;
    if (debug_me) console.log('        OK: ' + api_name );
  } else {
    console.log('FAILURE: ' + arguments.callee.name + ': ' + fault_msg);
    console.log('   ARGS: ', util.inspect(arguments));
  }
}


// Setup for testing    -  -  -  -  -  -  -  -  -  -  -  -

// We assume that test-fs-seek.js has run successfully before this 
// test and so we omit several duplicate tests.

// Delete any prior copy of test data file(s)
remove_file_wo_error(file_path);

// Create a new file
tests_run++;
try {
  file_fd = fs.openSync(file_path, 'w');
  tests_ok++;
} catch (e) {
  console.log('  Unable to create test data file %j', file_path);
  console.log('    Error was: %j', e);
}


if ( tests_run !== tests_ok ) {     
  process.exit(1);
}


// Stress testing    -  -  -  -  -  -  -  -  -  -  -  -  -

var how_many_times,
    how_many_secs,
    how_many_done;


console.log('  Start time is %s', new Date());
console.log('  Before any testing:');
display_memory_usage_now();
console.log('');


// If we do quite a lot of nothing, how much does memory change?
if (0) {
  how_many_secs = 5;

  setTimeout(function ho_hum(){
    how_many_secs -= 1;
    if (how_many_secs > 0 ) {
      setTimeout(ho_hum,1000);
      return;
    }
    console.log('  After "do nothing" testing for %d seconds:', how_many_secs);
    display_memory_usage_now();
    console.log('        Time is %s', new Date());
  }, 
  1000 );
}


// Repeat a successful seekSync() call 
if( 1 ) {
  how_many_times = 10000000;
  //how_many_times = 4;

  for( var i=0 ; i<how_many_times ; i++ ) {
    tests_run++;
    err = fs.seekSync(file_fd, 0, 0);
    expect_ok('seekSync', file_fd, err);
  }

  console.log('  After %d calls to successful seekSync():', how_many_times);
  display_memory_usage_now();
  console.log('        Time is %s', new Date());
}


// Repeat a successful seek() call 
if( 1 ) {
  how_many_times = 1000000;
  //how_many_times = 4;
  how_many_done  = 0;
 
  tests_run++;
  fs.seek(file_fd, 0, 0, function func_good_seek_cb(err){
    expect_ok('seek', file_fd, err);
    if (debug_me) console.log('    seek call counter   %d', how_many_times );

    how_many_done += 1;
    if ( how_many_done < how_many_times ) {
      tests_run++;
      fs.seek(file_fd, 0, 0, func_good_seek_cb );
      return;
    }
    console.log('  After %d calls to successful seek():', how_many_times);
    display_memory_usage_now();
    console.log('        Time is %s', new Date());

    test_failing_seek();
  });
} else {
  test_failing_seek();
}  

function test_failing_seek() {

  if (1) {
    how_many_times = 1000000;
    //how_many_times = 4;
    how_many_done  = 0;
 
    tests_run++;
    fs.seek(-99, 0, 0, function func_good_seek_cb(err){
      expect_errno('seek', -99, err, 'EBADF');
      if (debug_me) console.log('    seek call counter   %d', how_many_times );

      how_many_done += 1;
      if ( how_many_done < how_many_times ) {
        tests_run++;
        fs.seek(-99, 0, 0, func_good_seek_cb );
        return;
      }
      console.log('  After %d calls to failing seek():', how_many_times);
      display_memory_usage_now();
      console.log('        Time is %s', new Date());
    });
  } else {
  }

}

//  } else {
//    tests_run++;
//    fs.seek(-99, 0, 0, function hum_ho_2(err){
//      expect_errno('seek', -99, err, 'EBADF');
//      if (debug_me) console.log('    seek call counter   %d', how_many_times );
//
//      how_many_times -= 1;
//      if ( how_many_times > 0 ) {
//        tests_run++;
//        fs.seek(-99, 0, 0, hum_ho_2 );
//        return;
//      }
//
//      display_memory_usage_now();
//      console.log('    End time is %s', new Date());
//    });
//  }
//}
//

//     PID USER      PR  NI  VIRT  RES  SHR S %CPU %MEM    TIME+  COMMAND
//    2081 Tom       20   0  211m 164m 4580 R 94.6 16.4   0:04.63 node
// vs. sync 
//    2074 Tom       20   0 56076 7636 4224 S  3.6  0.7   0:00.11 node
// vs. async 
//    2213 Tom       20   0 76668  28m 4400 R 97.0  2.9   0:12.55 node

// after delete chg for seekSync path
//    2264 Tom       20   0 56480 9540 4628 R 95.7  0.9   0:05.00 node
// after chg for async, on error-returning path
//    2348 Tom       20   0 60796  27m 4496 R 97.4  2.7   0:37.01 node

// sync
//  Start time is Sun Jun 12 2011 16:50:54 GMT-0500 (CDT)
//  memory usage:  { "rss":         7815168,
//                   "vsize":      57425920,
//                   "heapTotal":   2853056,
//                   "heapUsed":    1851472}
//  memory usage:  { "rss":       249483264,
//                   "vsize":     297807872,
//                   "heapTotal":   4388064,
//                   "heapUsed":    1772168}
//    End time is Sun Jun 12 2011 16:51:02 GMT-0500 (CDT)
//  Tests run: 10000007     ok: 10000007

// async
//  Start time is Sun Jun 12 2011 16:45:24 GMT-0500 (CDT)
//  memory usage:  { "rss":         7573504,
//                   "vsize":      57425920,
//                   "heapTotal":   2861216,
//                   "heapUsed":    1828544}
//  memory usage:  { "rss":        34828288,
//                   "vsize":      83243008,
//                   "heapTotal":   4236992,
//                   "heapUsed":    2377772}
//    End time is Sun Jun 12 2011 16:45:40 GMT-0500 (CDT)
//  Tests run: 1000007     ok: 1000007

// noop
//
//  Start time is Sun Jun 12 2011 16:54:17 GMT-0500 (CDT)
//  memory usage:  { "rss":         7827456,
//                   "vsize":      57425920,
//                   "heapTotal":   2844896,
//                   "heapUsed":    1853660}
//  memory usage:  { "rss":         9220096,
//                   "vsize":      57790464,
//                   "heapTotal":   4249344,
//                   "heapUsed":    1775448}
//    End time is Sun Jun 12 2011 16:54:17 GMT-0500 (CDT)
//  Tests run: 10000007     ok: 10000007
//
//
// after delete chg for seekSync path
//  Start time is Sun Jun 12 2011 16:59:37 GMT-0500 (CDT)
//  memory usage:  { "rss":         7839744,
//                   "vsize":      57425920,
//                   "heapTotal"    2853056,
//                   "heapUsed":    1855784}
//  memory usage:  { "rss":         9768960,
//                   "vsize":      57839616,
//                   "heapTotal":   4257504,
//                   "heapUsed":    1780820}
//    End time is Sun Jun 12 2011 16:59:45 GMT-0500 (CDT)
//  Tests run: 10000007     ok: 10000007
//
//
// after delete chg for async path
//  Start time is Sun Jun 12 2011 17:06:16 GMT-0500 (CDT)
//  memory usage:  { "rss":         7831552,
//                   "vsize":      57425920,
//                   "heapTotal":   2853056,
//                   "heapUsed":    1858488}
//  memory usage:  { "rss":         9351168,
//                   "vsize":      57835520,
//                   "heapTotal":   4232928,
//                   "heapUsed":    2387816}
//    End time is Sun Jun 12 2011 17:06:32 GMT-0500 (CDT)
//  Tests run: 1000007     ok: 1000007
//
// after chg for async, on error-returning path
//
//  2348 Tom       20   0 60796  27m 4496 R 97.4  2.7   0:37.01 node
//
//  Start time is Sun Jun 12 2011 17:20:48 GMT-0500 (CDT)
//  memory usage:  { "rss":         7839744,
//                   "vsize":      57425920,
//                   "heapTotal":   2844896,
//                   "heapUsed":    1864576}
//  memory usage:  { "rss":        27209728,
//                   "vsize":      60948480,
//                   "heapTotal":  22005504,
//                   "heapUsed":    6403888}
//    End time is Sun Jun 12 2011 17:21:29 GMT-0500 (CDT)
//  Tests run: 1000007     ok: 1000007
//


//------------------------------------------------------------------------------
//- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - 
//-  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  -  

// ### fs.seek(fd, offset, whence, [callback])

// ### fs.seekSync(fd, offset, whence)

// err { "stack":   "TypeError: Bad argument\n    at Object.seekSync (/home/Tom/study/javascript/node.js/baudehlo-node-fs-ext-3a489b7_/fs-ext.js:80:18)\n    at Object.<anonymous> (/home/Tom/study/javascript/node.js/baudehlo-node-fs-ext-3a489b7_/tests/test-fs-seek.js:101:12)\n    at Module._compile (module.js:407:26)\n    at Object..js (module.js:413:10)\n    at Module.load (module.js:339:31)\n    at Function._load (module.js:298:12)\n    at Array.0 (module.js:426:10)\n    at EventEmitter._tickCallback (node.js:126:26)",
//       "message": "Bad argument"}

// err { "stack":   "Error: EBADF, Bad file descriptor\n    at Object.seekSync (/home/Tom/study/javascript/node.js/baudehlo-node-fs-ext-3a489b7_/fs-ext.js:80:18)\n    at Object.<anonymous> (/home/Tom/study/javascript/node.js/baudehlo-node-fs-ext-3a489b7_/tests/test-fs-seek.js:137:12)\n    at Module._compile (module.js:407:26)\n    at Object..js (module.js:413:10)\n    at Module.load (module.js:339:31)\n    at Function._load (module.js:298:12)\n    at Array.0 (module.js:426:10)\n    at EventEmitter._tickCallback (node.js:126:26)",
//       "message": "EBADF, Bad file descriptor",
//       "errno":   9,
//       "code":    "EBADF"}

//XXX Does seek() return current position correctly?
//  fseek() doesn't return positions, only ftell() does that
//  ahh, but lseek() does return current positions  !!!
//
//  RETURN VALUE
//    Upon  successful completion, lseek() returns the resulting offset loca‐
//    tion as measured in bytes from the beginning of the  file.   On  error,
//    the  value  (off_t) -1  is  returned  and  errno is set to indicate the
//    error.

