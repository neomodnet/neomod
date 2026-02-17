// polyfill browser globals for Node.js headless mode
if(typeof window === 'undefined') {
    var noop = function() {};
    globalThis.window = {
        location: { search: '', href: '', hostname: '' },
        addEventListener: noop,
        removeEventListener: noop,
        innerWidth: 1280,
        innerHeight: 720,
        devicePixelRatio: 1,
    };
    globalThis.document = {
        URL: '',
        createElement: function() { return { style: {} }; },
        getElementById: function() { return null; },
        querySelector: function() { return null; },
        addEventListener: noop,
        removeEventListener: noop,
        body: { appendChild: noop },
    };
    globalThis.screen = { width: 1280, height: 720 };

    // emrun's pre-js (injected after this file) checks `if(globalThis.window)`
    // and overrides Module['arguments'] with (empty) URL search params.
    // make Module['arguments'] always return process.argv so emrun can't clobber it.
    if(typeof process !== 'undefined') {
        Object.defineProperty(Module, 'arguments', {
            get: function() { return process.argv.slice(2); },
            set: noop,
            configurable: true,
        });

        // non-blocking stdin line buffer for headless mode.
        // pthreads proxy all stdio to the main thread, so blocking reads from a
        // worker thread don't work in Node.js. instead, buffer lines here and
        // let C++ poll them from the main thread via EM_ASM.
        globalThis.__stdinLines = [];
        globalThis.__stdinEOF = false;
        var _partial = '';
        process.stdin.setEncoding('utf8');
        process.stdin.on('data', function(chunk) {
            var parts = ((_partial || '') + chunk).split('\n');
            _partial = parts.pop();  // last element is the incomplete line (or '' if chunk ended with \n)
            for(var i = 0; i < parts.length; i++) {
                globalThis.__stdinLines.push(parts[i]);
            }
        });
        process.stdin.on('end', function() {
            if(_partial.length > 0) {
                globalThis.__stdinLines.push(_partial);
                _partial = '';
            }
            globalThis.__stdinEOF = true;
        });
        process.stdin.resume();

    }
}

// mount persistent filesystem at /persist/ before C++ starts
Module['preRun'] = Module['preRun'] || [];
Module['preRun'].push(function() {
    FS.mkdir('/persist');
    if(typeof process !== 'undefined' && process.versions && process.versions.node) {
        // Node.js: use NODEFS backed by a host directory next to neomod.js
        var path = require('path');
        var fs = require('fs');
        var dataDir = path.join(path.dirname(process.argv[1]), 'neomod-data');
        fs.mkdirSync(dataDir, { recursive: true });
        FS.mount(NODEFS, { root: dataDir }, '/persist');
    } else {
        // browser: use IDBFS backed by IndexedDB
        addRunDependency('persist-sync');
        FS.mount(IDBFS, { autoPersist: true }, '/persist');
        FS.syncfs(true, function(err) {
            if(err) console.error('IDBFS initial sync error:', err);
            removeRunDependency('persist-sync');
        });
        // best-effort final sync on tab close (autoPersist handles most cases)
        window.addEventListener('beforeunload', function() {
            FS.syncfs(false, function(e) { if(e) console.error('syncfs error:', e); });
        });
    }
});
