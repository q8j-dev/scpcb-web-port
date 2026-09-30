(function () {
  function locate(path) {
    return Module['locateFile'] ? Module['locateFile'](path, '') : path;
  }

  function fetchPart(url, parse) {
    return fetch(locate(url)).then(function (r) {
      if (!r.ok) throw new Error(url + ': HTTP ' + r.status);
      return parse(r);
    });
  }

  var pending = {};
  ['assets-core', 'assets-world', 'assets-audio'].forEach(function (base) {
    pending[base] = Promise.all([
      fetchPart(base + '.json', function (r) { return r.json(); }),
      fetchPart(base + '.data', function (r) { return r.arrayBuffer(); }),
    ]);
  });

  Module['scpcbMount'] = function (base, done) {
    return pending[base].then(function (res) {
      var meta = res[0];
      var bytes = new Uint8Array(res[1]);
      for (var i = 0; i < meta.files.length; ++i) {
        var f = meta.files[i];
        var dir = f.name.substring(0, f.name.lastIndexOf('/')) || '/';
        FS.mkdirTree(dir);
        FS.createDataFile(f.name, null, bytes.subarray(f.start, f.end),
                          true, true, true  );
      }
      Module['scpcbReady'] = Module['scpcbReady'] || {};
      Module['scpcbReady'][meta.package] = 1;
      if (done) done();
    });
  };

  Module['preRun'] = Module['preRun'] || [];
  Module['preRun'].push(function () {
    addRunDependency('scpcb-core');
    Module['scpcbMount']('assets-core', function () {
      removeRunDependency('scpcb-core');
    })['catch'](function (e) {
      console.error('[scpcb] core asset package failed:', e);
      if (Module['setStatus']) Module['setStatus']('Asset download failed: ' + e);
      throw e;
    });

    Module['scpcbDeferred'] = Promise.all([
      Module['scpcbMount']('assets-world'),
      Module['scpcbMount']('assets-audio'),
    ]);
    Module['scpcbDeferred']['catch'](function (e) {
      console.error('[scpcb] deferred asset package failed:', e);
      if (Module['setStatus']) Module['setStatus']('Asset download failed: ' + e);
    });
  });
})();
