/* @license This file Copyright © Mnemosaic LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosaic LLC.
   License text can be found in the licenses/ folder. */

// the largest fraction an incomplete Percent reports, so a bar never fills early
const max_incomplete_fraction = 0.9999;

// Progress in [0..1], exact about completion
export class Percent {
  // Complete iff `have >= total`, which includes an empty torrent (`total === 0`)
  constructor(have, total) {
    this.have = have;
    this.total = total;
  }

  // 0%, incomplete, e.g. for a size that is not known yet
  static unknown() {
    return new Percent(0, 1);
  }

  // For sources that only report a fraction. Complete iff `fraction >= 1`.
  static fromDouble(fraction) {
    if (fraction >= 1) {
      return new Percent(1, 1);
    }
    if (!(fraction > 0)) {
      return Percent.unknown();
    }
    const granularity = 10_000;
    const have = Math.min(Math.round(fraction * granularity), granularity - 1);
    return new Percent(have, granularity);
  }

  isComplete() {
    return this.have >= this.total;
  }

  // In [0..1]; below 1 until complete
  toFraction() {
    if (this.isComplete()) {
      return 1;
    }
    return Math.min(this.have / this.total, max_incomplete_fraction);
  }
}
