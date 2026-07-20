# Transmission Web Client

A web interface is built into all Transmission flavors, enabling them to be controlled remotely.

## Notes for Packagers

Transmission releases include a prebuilt webapp bundle in
`web/public_html/`, so most packages don't need to build it themselves.

If your distro's policies require building the bundle from source, the
only tool needed is [esbuild](https://esbuild.github.io/), which is
packaged by Debian, Fedora, Homebrew, and others. Neither Node nor npm
is required: the webapp has no external runtime dependencies — the few
third-party modules it uses are vendored in `src/vendor/`.

```sh
$ cd transmission/web/
$ esbuild \
  --bundle \
  --external:*/favicon.svg \
  --legal-comments=external \
  --loader:.png=dataurl \
  --loader:.svg=dataurl \
  --minify \
  --outfile=public_html/transmission-app.js \
  --sourcemap \
  --target=chrome104,firefox115,safari16.4 \
  src/main.js
```

These flags mirror `esbuild.mjs` (what `npm run build` uses to generate
the official bundle) and `CMakeLists.txt` (what the main build runs when
configured with `-DREBUILD_WEB=ON`); keep the three in sync.

Instead of running esbuild by hand, you can also let the main build do
it: configure with `-DREBUILD_WEB=ON` and CMake will find your system
esbuild and rebuild the bundle as part of the build. To build just the
webapp this way, point CMake at this directory:

```sh
$ cmake -S web -B build-web
$ cmake --build build-web
```

## Notes for Developers

```sh
$ npm install
$ npm run dev
```

`npm run dev` stays running in the background and rebuilds
`public_html/transmission-app.{js,css}` whenever you change and save a
source file. It does not run a web server: to see your changes, point a
running Transmission at your freshly built assets and open its web UI at
[localhost:9091](http://localhost:9091/):

```sh
$ TRANSMISSION_WEB_HOME="$PWD/public_html" transmission-daemon
```

Transmission serves these files with long-expiry cache headers, so do a
hard refresh (e.g. Ctrl+Shift+R) in the browser to pick up a rebuild.

## Notes for Testers

Use this bookmarklet to anonymize your torrent names before submitting a screenshot:

`javascript:void%20function(){const%20a=document.getElementsByClassName(%22torrent-name%22);for(const%20b%20of%20a)console.log(b),b.textContent=%22Lorem%20ipsum%20dolor%20sit%20amet.iso%22}();`

You’ll typically have about 3 seconds before the next batch of RPC updates overwrite the text content of any currently-downloading files.
