// tests the fs methods stat, lstat and fstat
"use strict";
var assert = require("assert"),
  path = require("path"),
  fs = require("../fs-ext"),
  tmp_dir = process.env.TMP || process.env.TEMP || "/tmp",
  file_path = path.join(tmp_dir, "fs-ext_stat.test"),
  fd, stats;

function check_stats(stats) {
  if (process.platform.match(/^win/i)) {
    assert.equal(typeof stats.uid, "string");
    assert.equal(stats.uid.indexOf("S-"), 0);
    assert.equal(typeof stats.gid, "string");
    assert.equal(stats.gid.indexOf("S-"), 0);
  }
  else {
    assert.equal(typeof stats.uid, "number");
    assert.equal(typeof stats.gid, "number");
  }
}

fd = fs.openSync(file_path, "w");
fs.closeSync(fd);
fs.chmodSync(file_path, "0666");

stats = fs.statSync(file_path);
check_stats(stats);
fs.stat(file_path, function (error, stats) {
  assert.ok(!error && stats);
  check_stats(stats);
});

stats = fs.lstatSync(file_path);
check_stats(stats);
fs.lstat(file_path, function (error, stats) {
  assert.ok(!error && stats);
  check_stats(stats);
});

fd = fs.openSync(file_path, "r");
stats = fs.fstatSync(fd);
check_stats(stats);
fs.fstat(fd, function (error, stats) {
  assert.ok(!error && stats);
  check_stats(stats);
});

process.addListener("exit", function() {
  try {
    fs.closeSync(fd);
  }
  catch (error) {
    // Do nothing
  }
  try {
    fs.unlinkSync(file_path);
  }
  catch (error) {
    console.warn("  deleting", file_path, "failed");
  }
});
