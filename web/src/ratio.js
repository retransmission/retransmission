/* @license This file Copyright © Mnemosaic LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosaic LLC.
   License text can be found in the licenses/ folder. */

// An upload ratio as the RPC sends it: a number, or one of the daemon's sentinels
export const Ratio = Object.freeze({
  // TR_RATIO_INF: uploads against nothing downloaded
  Infinite: -2,
  // TR_RATIO_NA: no data at all
  None: -1,

  // `uploaded / downloaded`, unrounded; `None` for 0/0, `Infinite` for x/0
  fromCounts(uploaded, downloaded) {
    const ratio = uploaded / downloaded;
    if (Number.isNaN(ratio)) {
      return Ratio.None;
    }
    if (!Number.isFinite(ratio)) {
      return Ratio.Infinite;
    }
    return ratio;
  },

  // Rounded half up to hundredths: two decimals below 5, one below 100, none from there
  format(ratio) {
    if (ratio === Ratio.None) {
      return 'None';
    }
    if (ratio === Ratio.Infinite) {
      return '∞';
    }
    const hundredths = Math.round(ratio * 100);
    if (hundredths < 500) {
      return (hundredths / 100).toFixed(2);
    }
    const tenths = Math.round(hundredths / 10);
    return tenths < 1000
      ? (tenths / 10).toFixed(1)
      : Math.round(hundredths / 100).toFixed(0);
  },
});
