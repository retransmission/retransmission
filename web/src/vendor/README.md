# Vendored third-party modules

These are the webapp's only third-party runtime dependencies. They are
vendored here rather than fetched with npm so that the bundle can be
built without `npm install` — see "Notes for Packagers" in
[web/README.md](../../README.md).

Both are MIT-licensed. clusterize.js keeps its upstream copyright header;
fast-deep-equal's header was composed from its LICENSE file, which had no
header comment upstream. The license text is in `licenses/mit.txt` at the
repo root.

| file                 | upstream                                         | version                | local changes                                    |
| -------------------- | ------------------------------------------------ | ---------------------- | ------------------------------------------------ |
| `clusterize.js`      | <https://github.com/NeXTs/Clusterize.js>         | 1.0.0                  | UMD wrapper replaced with an ES module export    |
| `fast-deep-equal.js` | <https://github.com/epoberezkin/fast-deep-equal> | 3.1.3 (`es6/index.js`) | CommonJS export converted to an ES module export |
