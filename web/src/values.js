/* @license This file Copyright © Mnemosaic LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosaic LLC.
   License text can be found in the licenses/ folder. */

// ten-thousandths, i.e. 0.01%: the finest step any display shows
const granularity = 10_000;

// Progress in [0..1], exact about completion
export class Percent {
  // Complete iff `have >= total`, which includes an empty torrent (`total === 0`)
  constructor(have, total) {
    this.have = have;
    this.total = total;
  }

  // For sources that only report a fraction. Complete iff `fraction >= 1`.
  static fromDouble(fraction) {
    if (fraction >= 1) {
      return Percent.Complete;
    }
    if (!(fraction > 0)) {
      return Percent.Unknown;
    }
    const have = Math.min(Math.round(fraction * granularity), granularity - 1);
    return new Percent(have, granularity);
  }

  isComplete() {
    return this.have >= this.total;
  }

  // In [0..1]; at most 0.9999 until complete, so a bar sized from it never fills early
  toFraction() {
    return this._tenThousandths() / granularity;
  }

  // Digits only, for a "{percent}%" display string; CSS takes `toFraction()`.
  // Two decimals below 5, one below 100, none at 100.
  // Rounds half up, but reads "99.9" until complete.
  toString() {
    const hundredths = this._tenThousandths(); // of a percent
    if (hundredths === granularity) {
      return '100';
    }
    if (hundredths < 500) {
      return (hundredths / 100).toFixed(2);
    }
    return (Math.min(Math.round(hundredths / 10), 999) / 10).toFixed(1);
  }

  // Likewise in whole percents: "99" until complete
  toWholeString() {
    const hundredths = this._tenThousandths();
    if (hundredths === granularity) {
      return '100';
    }
    return Math.min(Math.round(hundredths / 100), 99).toFixed(0);
  }

  // `granularity` iff complete; otherwise capped one step below it
  _tenThousandths() {
    if (this.isComplete()) {
      return granularity;
    }
    const rounded = Math.round((this.have * granularity) / this.total);
    return Math.min(rounded, granularity - 1);
  }
}

Percent.Complete = Object.freeze(new Percent(1, 1));
// 0%, incomplete, e.g. for a size that is not known yet
Percent.Unknown = Object.freeze(new Percent(0, 1));

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

  // For comparisons and arithmetic: `Infinite` as `Infinity`, else unchanged
  toNumber(ratio) {
    return ratio === Ratio.Infinite ? Number.POSITIVE_INFINITY : ratio;
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
