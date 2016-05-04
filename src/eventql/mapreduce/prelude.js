var __kFnMagic = "\b\bFN<1337Z12323<\b\b";

function __log() {
  var parts = [];

  for (var i = 0; i < arguments.length; ++i) {
    parts.push(String(arguments[i]));
  }

  z1_log(parts.join(", "));
}

function __encode_js(obj) {
  var replacer = function(key, value) {
    switch (typeof value) {
      case "string":
        return value;
      case "object":
        return value;
      case "boolean":
        return value;
      case "number":
        return value;
      case "function":
        return __kFnMagic + String(value);
      default:
        return undefined;
    }
  };

  switch (typeof obj) {
    case "function":
      return __kFnMagic + String(obj);
    default:
      return JSON.stringify(obj, replacer);
  }
}

function __decode_js(str) {
  var decode_fn = function(str) {
    eval("__load_fn = " + str);
    var fn = __load_fn;
    delete __load_fn;
    return fn;
  }

  var reviver = function(key, value) {
    switch (typeof value) {
      case "string":
        if (value.indexOf(__kFnMagic) == 0) {
          return decode_fn(value.substr(__kFnMagic.length));
        } else {
          return value;
        }
      default:
        return value;
    }
  };

  if (str.indexOf(__kFnMagic) == 0) {
    return decode_fn(str.substr(__kFnMagic.length));
  } else {
    return JSON.parse(str, reviver);
  }
}

function __call_with_iter(key, iter) {
  var iter_wrap = (function(iter) {
    return {
      hasNext: function() { return iter.hasNext.apply(iter, arguments); },
      next: function() { return iter.next.apply(iter, arguments); },
    };
  })(iter);

  return __fn.call(this, key, iter_wrap);
}

var __load_closure = (function(global_scope) {
  return function(fn, globals_json, params_json) {
    var globals = __decode_js(globals_json);
    for (k in globals) {
      global_scope[k] = globals[k];
    }

    global_scope["params"] = __decode_js(params_json);
    eval("__fn = " + fn);
  }
})(this);

var console = {
  log: function() {
    __log.apply(this, arguments);
  }
};

var Z1 = (function(global) {
  var seq = 0;
  var jobs = {};
  var bcastdata = {};

  function mkJobID() {
    return "job-" + ++seq;
  };

  function executeJob(root_job) {
    var dependencies = [];
    var dependencies_set = {};
    dependencies.push(root_job);

    var find_dependecies = function(job) {
      if (job.sources) {
        job.sources.forEach(function(job_id) {
          if (dependencies_set[job_id]) {
            return;
          }

          var job = jobs[job_id];
          if (!job) {
            throw "invalid job id: " + job_id;
          }

          dependencies_set[job_id] = true;
          dependencies.push(job);
          find_dependecies(job);
        });
      }
    };

    find_dependecies(root_job);
    z1_executemr(JSON.stringify(dependencies), root_job.id);
  }

  function autoBroadcast() {
    for (k in global) {
      if (typeof k == "string" && k.indexOf("__") == 0) {
        continue;
      }

      if (typeof global[k] == "function") {
        Z1.broadcast(k);
      }
    }
  }

  /* public api */
  var api = {};

  api.log = function() {
    __log.apply(this, arguments);
  }

  api.broadcast = function() {
    for (var i = 0; i < arguments.length; ++i) {
      var var_name = arguments[i];

      if (typeof var_name != "string")  {
        throw "arguments to Z1.broadcast must be strings";
      }

      if (var_name == "params") {
        throw "'params' is a reserved variable and cannot be broadcasted";
      }

      if (!global.hasOwnProperty(var_name)) {
        throw "no such variable in the global namespace: '" + var_name +
              "' -- all broadcast variables must be global";
      }

      bcastdata[var_name] = global[var_name];
    }
  };

  api.mapTable = function(opts) {
    autoBroadcast();
    var job_id = mkJobID();

    jobs[job_id] = {
      id: job_id,
      op: "map_table",
      table_name: opts["table"],
      from: opts["from"],
      until: opts["until"],
      map_fn: String(opts["map_fn"]),
      globals: __encode_js(bcastdata),
      params: __encode_js(opts["params"] || {}),
      required_columns: opts["required_columns"]
    };

    return job_id;
  };

  api.reduce = function(opts) {
    autoBroadcast();
    var job_id = mkJobID();

    jobs[job_id] = {
      id: job_id,
      op: "reduce",
      sources: opts["sources"],
      num_shards: opts["shards"],
      reduce_fn: String(opts["reduce_fn"]),
      globals: __encode_js(bcastdata),
      params: __encode_js(opts["params"] || {})
    };

    return job_id;
  };

  api.downloadResults = function(sources, serialize_fn) {
    if (!serialize_fn) {
      serialize_fn = "";
    }

    executeJob({
      id: mkJobID(),
      op: "return_results",
      sources: sources,
      serialize_fn: String(serialize_fn),
      globals: __encode_js(bcastdata),
      params: __encode_js({})
    });
  };

  api.saveToTable = function(opts) {
    executeJob({
      id: mkJobID(),
      op: "save_to_table",
      table_name: opts["table"],
      sources: opts["sources"]
    });
  };

  api.saveToTablePartition = function(opts) {
    executeJob({
      id: mkJobID(),
      op: "save_to_table_partition",
      table_name: opts["table"],
      partition_key: opts["partition"],
      sources: opts["sources"]
    });
  };

  api.processStream = function(opts) {
    var calculate_fn = opts["calculate_fn"];

    var partitions = z1_listpartitions(
        "" + opts["table"],
        "" + opts["from"],
        "" + opts["until"]);

    partitions.forEach(function(partition) {
      var partition_sources = calculate_fn(
          parseInt(partition.time_begin, 10),
          parseInt(partition.time_limit, 10));

      if (typeof partition_sources != "object") {
        throw "Z1.processStream calculate_fn must return a list of jobs";
      }

      api.saveToTablePartition({
        table: opts["table"],
        partition: partition.partition_key,
        sources: partition_sources
      });
    });
  }

  api.writeToOutput = function(str) {
    if (typeof str != "string") {
      throw "argument to Z1.writeToOutput must be a string";
    }

    z1_returnresult(str);
  };

  return api;
})(this);
