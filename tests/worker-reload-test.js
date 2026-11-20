"use strict";

// Loading the addon in one worker after another must not touch handles that belonged to a finished worker.
// The addon used to store its statVFS field names in static persistent handles, and each load reset them,
// so the next load freed a handle owned by an isolate that no longer existed and V8 aborted the process.
const { Worker, isMainThread, parentPort } = require("node:worker_threads");
const assert = require("node:assert");
const v8 = require("node:v8");
const vm = require("node:vm");
require("../fs-ext");

const WORKERS = 20;

if (isMainThread) {
  v8.setFlagsFromString("--expose-gc");
  const gc = vm.runInNewContext("gc");
  let finished = 0;
  const next = () => {
    new Worker(__filename).on("exit", (code) => {
      assert.equal(code, 0);
      gc();
      if (++finished < WORKERS) next();
    });
  };
  next();
  process.on("exit", () => assert.equal(finished, WORKERS));
}
else {
  parentPort.close();
}
