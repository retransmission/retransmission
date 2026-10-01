/* @license This file Copyright © Mnemosaic LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosaic LLC.
   License text can be found in the licenses/ folder. */

import { createDialogContainer } from './utils.js';

export class AboutDialog extends EventTarget {
  constructor(version_info) {
    super();

    this.elements = AboutDialog._create(version_info);
    this.elements.dismiss.addEventListener('click', () => this.close());
    document.body.append(this.elements.root);
    this.elements.dismiss.focus();
  }

  close() {
    this.elements.root.remove();
    this.dispatchEvent(new Event('close'));
    delete this.elements;
  }

  static _create(version_info) {
    const elements = createDialogContainer('about-dialog');
    elements.root.setAttribute('aria-label', 'About Retransmission');
    elements.heading.textContent = 'Retransmission';
    elements.dismiss.textContent = 'Close';

    let e = document.createElement('div');
    e.classList.add('about-dialog-version-number');
    e.textContent = version_info.version;
    elements.heading.append(e);

    e = document.createElement('div');
    e.classList.add('about-dialog-version-checksum');
    e.textContent = version_info.checksum;
    elements.heading.append(e);

    e = document.createElement('div');
    e.textContent = 'A fast and easy bitTorrent client';
    elements.workarea.append(e);
    e = document.createElement('div');
    e.textContent = 'Copyright © The Retransmission Project';
    elements.workarea.append(e);

    e = document.createElement('a');
    e.href = 'https://retransmission.org/';
    e.target = '_blank';
    e.textContent = 'https://retransmission.org/';
    elements.workarea.append(e);

    elements.confirm.remove();
    delete elements.confirm;

    return elements;
  }
}
