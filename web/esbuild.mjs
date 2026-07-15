import * as esbuild from 'esbuild';
import * as process from 'node:process';

const ctx = await esbuild.context({
  bundle: true,
  entryPoints: ['./src/main.js'],
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
  // keep in sync with the esbuild command in README.md.
  target: ['chrome104', 'firefox115', 'safari16.4'],
});

if (process.env.DEV) {
  await ctx.watch();
  console.log('watching...');
} else {
  await ctx.rebuild();
  ctx.dispose();
}
