import * as process from 'node:process';
import * as esbuild from 'esbuild';

const ctx = await esbuild.context({
  bundle: true,
  entryPoints: ['./src/main.js'],
  // the dialog logo already ships in public_html/images/; leave its url()
  // alone rather than inlining a second copy of the svg as a data: URI
  external: ['*/favicon.svg'],
  legalComments: 'external',
  loader: {
    '.png': 'dataurl',
    '.svg': 'dataurl',
  },
  minify: true,
  outfile: './public_html/transmission-app.js',
  sourcemap: true,
  // the oldest browsers the bundle needs to run in. esbuild transpiles
  // newer JS syntax and flattens native CSS nesting to fit these targets.
  // keep in sync with the esbuild commands in README.md and CMakeLists.txt.
  target: ['chrome104', 'firefox115', 'safari16.4'],
});

if (process.env.DEV) {
  await ctx.watch();
  console.log('watching...');
} else {
  await ctx.rebuild();
  ctx.dispose();
}
