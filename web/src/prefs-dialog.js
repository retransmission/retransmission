/* @license This file Copyright © Mnemosaic LLC.
   It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
   or any future license endorsed by Mnemosaic LLC.
   License text can be found in the licenses/ folder. */

import { Formatter } from './formatter.js';
import {
  createTextualTabsContainer,
  makeUUID,
  OutsideClickListener,
  setEnabled,
  setTextContent,
} from './utils.js';

export class PrefsDialog extends EventTarget {
  // an IPv6 address's type, the first of session_get's tags
  static _Ranges = {
    gua: {
      about: 'global, reachable from the internet',
      name: 'GUA',
    },
    ula: {
      about: 'unique local, reachable only within your site',
      name: 'ULA',
    },
    'link-local': {
      about:
        'this link only, which no peer or tracker is on. Local peer discovery reaches the link through the interface, without a bind address.',
      name: 'Link-local',
    },
    nat64: {
      about: 'an IPv4 address translated by NAT64',
      name: 'NAT64',
    },
    teredo: { about: 'a Teredo tunnel', name: 'Teredo' },
    '6to4': { about: 'a 6to4 tunnel (deprecated)', name: '6to4' },
    documentation: {
      about: 'for examples only, not routed',
      name: 'Documentation',
    },
    'site-local': { about: 'deprecated', name: 'Site-local' },
    loopback: { about: 'this host only', name: 'Loopback' },
  };

  // session_get's other tags, an address's origin
  static _Origins = {
    'slaac-stable': {
      about: 'autoconfigured, keeps its address',
      name: 'SLAAC-stable',
    },
    'slaac-privacy': {
      about: 'autoconfigured, temporary, replaced regularly',
      name: 'SLAAC-privacy',
    },
    dhcp: { about: 'leased from a DHCPv6 server', name: 'DHCP' },
  };

  // session_get's managed_by values for an interface
  static _Managers = {
    networkmanager: 'NetworkManager',
    docker: 'Docker',
    libvirt: 'libvirt',
    other: 'Other',
  };

  // Filters over a bind address list. `values(address, iface)` gives what a
  // filter's chosen values are matched against; an address passes a filter
  // when any of them was chosen.
  static _AddressFilters = [
    {
      choices: () => Object.entries(PrefsDialog._Managers),
      key: 'managed_by',
      label: 'Managed by',
      values: (_address, iface) => [iface.managed_by],
    },
    {
      choices: (interfaces) => interfaces.map(({ name }) => [name, name]),
      key: 'interface',
      label: 'Interface',
      values: (_address, iface) => [iface.name],
    },
    {
      choices: () =>
        Object.entries(PrefsDialog._Ranges).map(([tag, { name }]) => [
          tag,
          name,
        ]),
      family: 'ipv6',
      key: 'type',
      label: 'Type',
      values: ({ tags }) => (tags ?? []).slice(0, 1),
    },
    {
      choices: () =>
        Object.entries(PrefsDialog._Origins).map(([tag, { name }]) => [
          tag,
          name,
        ]),
      family: 'ipv6',
      key: 'origin',
      label: 'Origin',
      values: ({ tags }) => (tags ?? []).slice(1),
    },
  ];

  // A tunnel passes the Managed by filter whatever manages it: a VPN that
  // wg-quick or OpenVPN brought up is no manager's, yet it is what a user binds to.
  static _passesFilter(def, values, address, iface) {
    if (def.key === 'managed_by' && iface.point_to_point) {
      return true;
    }
    return def.values(address, iface).some((v) => values.includes(v));
  }

  static _BindAddressAdvice = {
    ipv4: 'Recommended: Any. The chosen interface already keeps traffic on it.',
    ipv6: 'Recommended: Any. The chosen interface already keeps traffic on it, and IPv6 addresses change: SLAAC-privacy addresses are replaced regularly, and a new prefix from your provider replaces the rest. To pin one anyway, pick a SLAAC-stable or DHCP address.',
  };

  static _initTimeDropDown(e) {
    for (let index = 0; index < 24 * 4; ++index) {
      const hour = Math.trunc(index / 4);
      const mins = (index % 4) * 15;
      const value = index * 15;
      const content = `${hour}:${mins || '00'}`;
      e.options[index] = new Option(content, value);
    }
  }

  static _initDayDropDown(e) {
    const options = [
      ['Everyday', '127'],
      ['Weekdays', '62'],
      ['Weekends', '65'],
      ['Sunday', '1'],
      ['Monday', '2'],
      ['Tuesday', '4'],
      ['Wednesday', '8'],
      ['Thursday', '16'],
      ['Friday', '32'],
      ['Saturday', '64'],
    ];
    for (let index = 0; options[index]; ++index) {
      const [text, value] = options[index];
      e.options[index] = new Option(text, value);
    }
  }

  _checkPort() {
    for (const [key, element] of Object.entries(
      this.elements.network.port_status_label,
    )) {
      delete element.dataset.open;
      setTextContent(element, 'Checking...');
      this.remote.checkPort(
        key,
        (response) => this._onPortChecked(key, response),
        this,
      );
    }
  }

  _onPortChecked(ip_protocol, response) {
    if (this.closed) {
      return;
    }

    const args = response.result ?? response.error?.data ?? {};
    const element = this.elements.network.port_status_label[ip_protocol];
    const is_open = args.port_is_open ?? false;
    element.dataset.open = is_open;
    if ('port_is_open' in args) {
      setTextContent(element, is_open ? 'Open' : 'Closed');
    } else {
      setTextContent(element, 'Error');
    }
  }

  _setBlocklistButtonEnabled(b) {
    const e = this.elements.peers.blocklist_update_button;
    setEnabled(e, b);
    e.value = b ? 'Update' : 'Updating...';
  }

  static _getValue(e) {
    // a textarea, or a value that is a string even when it looks like a number
    if (e.tagName === 'TEXTAREA' || e.dataset.string !== undefined) {
      return e.value;
    }

    switch (e.type) {
      case 'checkbox':
      case 'radio':
        return e.checked;

      case 'number':
      case 'select-one':
      case 'text':
      case 'url': {
        const string = e.value;
        if (Number.parseInt(string, 10).toString() === string) {
          return Number.parseInt(string, 10);
        }
        if (Number.parseFloat(string).toString() === string) {
          return Number.parseFloat(string);
        }
        return string;
      }

      default:
        return null;
    }
  }

  // the daemon's binding, which it reads with surrounding whitespace removed
  _savedInterface() {
    return (
      this.session_manager.session_properties.bind_interface ?? ''
    ).trim();
  }

  // the entry of `network_interfaces` named `name`, if the daemon listed one
  _findInterface(name) {
    return this.network_interfaces.find((candidate) => candidate.name === name);
  }

  // session_get lists the interfaces only when asked, so the dialog asks
  // for them itself whenever the session changes
  _loadNetworkInterfaces() {
    this.remote.loadNetworkInterfaces((data) => {
      if (this.closed) {
        return;
      }
      this.network_interfaces = data?.result?.network_interfaces ?? [];
      this._updateInterfaceSelect();
      this._updateInterfaceAddresses();
    });
  }

  // a configured interface that has gone away stays selectable,
  // so the dialog shows what the daemon is actually using
  _updateInterfaceSelect() {
    const select = this.elements.network.interface_select;
    const current = this._savedInterface();
    // the interface in use stays listed whatever the filters say
    const passes = (iface) =>
      iface.name === current || this._interfacePasses(this.filters, iface);
    const all_names = this.network_interfaces.map(({ name }) => name);
    const names = this.network_interfaces
      .filter(passes)
      .map(({ name }) => name);

    // rebuilt only on change: replacing the options closes an open dropdown
    const key = JSON.stringify([current, names]);
    if (select.dataset.options !== key) {
      select.dataset.options = key;

      const options = [new Option('Any (default route)', '')];
      for (const name of names) {
        options.push(new Option(name, name));
      }
      if (current && current !== 'blocked' && !all_names.includes(current)) {
        options.push(new Option(`${current} (unavailable)`, current));
      }
      options.push(new Option('None (block all traffic)', 'blocked'));

      select.replaceChildren(...options);
    }
    // _update() has just set the untrimmed value, which no option may match
    select.value = current;

    // an empty list means the daemon's platform can't bind to an interface
    setEnabled(select, all_names.length > 0 || current !== '');
    this._renderFilterButtons();
  }

  // one line for an address: the address and its origin,
  // e.g. "2001:db8::1 · SLAAC-stable"
  static _describeAddress({ address, tags }) {
    const origins = (tags ?? [])
      .slice(1)
      .map((tag) => PrefsDialog._Origins[tag]?.name ?? tag);
    return [address, ...origins].join(' · ');
  }

  // explains the types and origins among `addresses`, one per line
  static _describeTags(addresses) {
    const tags = new Set(addresses.flatMap((address) => address.tags ?? []));
    const ranges = Object.entries(PrefsDialog._Ranges)
      .filter(([tag]) => tags.has(tag))
      .map(([, { about, name }]) => `${name}: ${about}`);
    const origins = Object.entries(PrefsDialog._Origins)
      .filter(([tag]) => tags.has(tag))
      .map(([, { about, name }]) => `${name}: ${about}`);
    return [ranges.join('\n'), origins.join('\n')].filter(Boolean).join('\n\n');
  }

  // a link-local address is only reachable on its own link,
  // which no peer or tracker is on
  static _isLinkLocal({ tags }) {
    return (tags ?? []).includes('link-local');
  }

  // shows the addresses of the interface picked in `select`, which may not
  // be saved yet, and offers them in the bind address lists
  _updateInterfaceAddresses() {
    const { network } = this.elements;
    const iface = this._findInterface(network.interface_select.value);
    const addresses = iface?.addresses ?? [];

    // an interface that's gone has no addresses to list; the binding
    // status says what that does to traffic
    const label = network.interface_addresses_label;
    const key = iface ? JSON.stringify(addresses) : '';
    // rebuilt only on change, so a session refresh can't replace a button mid-click
    if (label.dataset.addresses !== key) {
      label.dataset.addresses = key;
      if (addresses.length > 0) {
        const about = PrefsDialog._describeTags(addresses);
        label.replaceChildren(
          ...addresses.map((address) =>
            PrefsDialog._createCopyButton(address, about),
          ),
        );
      } else {
        label.textContent = iface ? 'No addresses' : '';
      }
    }
    label.classList.toggle('hidden', !key);

    this._updateBindStatus();

    for (const family of Object.keys(network.bind_address)) {
      this._updateBindAddressSelect(family);
    }
  }

  // Lists the local addresses of `family` a bind address can be: the chosen
  // interface's, or every interface's while the default route is chosen.
  // A saved address no interface has stays selectable, as with interfaces.
  _updateBindAddressSelect(family) {
    const { network } = this.elements;
    const { feedback, select } = network.bind_address[family];
    // "0.0.0.0" and "::" are older settings' spelling of any address
    const value = (
      this.session_manager.session_properties[select.dataset.key] ?? ''
    ).trim();
    const saved = value === '0.0.0.0' || value === '::' ? '' : value;

    const chosen = network.interface_select.value;
    let interfaces = [];
    if (chosen === '') {
      interfaces = this.network_interfaces;
    } else if (chosen !== 'blocked') {
      interfaces = [this._findInterface(chosen)].filter(Boolean);
    }
    const is_family = ({ address }) =>
      address.includes(':') === (family === 'ipv6');
    // the address in use stays listed whatever the filters say
    const passes = (address, iface) =>
      address.address === saved ||
      this._activeFilters(family).every(({ key, values }) => {
        // a chosen interface's addresses are listed whatever manages it
        if (key === 'managed_by' && chosen !== '') {
          return true;
        }
        const def = PrefsDialog._AddressFilters.find((d) => d.key === key);
        return PrefsDialog._passesFilter(def, values, address, iface);
      });
    const groups = interfaces
      .map((iface) => ({
        addresses: iface.addresses
          .filter(is_family)
          .filter((address) => passes(address, iface)),
        name: iface.name,
      }))
      .filter(({ addresses }) => addresses.length > 0);
    this._renderFilterButtons();
    const listed = groups.flatMap(({ addresses }) =>
      addresses.map(({ address }) => address),
    );

    // rebuilt only on change: replacing the options closes an open dropdown
    const key = JSON.stringify([saved, groups]);
    if (select.dataset.options !== key) {
      select.dataset.options = key;
      select.title = [
        PrefsDialog._BindAddressAdvice[family],
        PrefsDialog._describeTags(groups.flatMap(({ addresses }) => addresses)),
      ]
        .filter(Boolean)
        .join('\n\n');

      const options = [
        new Option(
          family === 'ipv4'
            ? 'Any (0.0.0.0), recommended'
            : 'Any (::), recommended',
          '',
        ),
      ];
      const toOption = (address) =>
        new Option(PrefsDialog._describeAddress(address), address.address);
      // under their interface's name, even when it's the only one
      for (const { addresses, name } of groups) {
        const group = document.createElement('optgroup');
        group.label = name;
        group.append(...addresses.map(toOption));
        options.push(group);
      }
      if (saved && !listed.includes(saved)) {
        options.push(new Option(`${saved} (unavailable)`, saved));
      }
      select.replaceChildren(...options);
    }
    select.value = saved;

    // binding an address the chosen interface doesn't have fails at runtime,
    // but the interface may be changed next, so this only warns;
    // an unavailable interface has no addresses to check against
    if (
      saved &&
      chosen !== '' &&
      chosen !== 'blocked' &&
      this._findInterface(chosen) &&
      !listed.includes(saved)
    ) {
      PrefsDialog._setFeedback(
        feedback,
        'warning',
        `Not an address of ${chosen}`,
      );
    } else if (feedback.dataset.state !== 'saved') {
      PrefsDialog._setFeedback(feedback, null, '');
    }
  }

  // A Managed by filter counts only while some interface has one of its
  // managers, so the default one leaves the lists whole where nothing reports
  // NetworkManager, e.g. on macOS.
  _counts({ key, values }) {
    return (
      key !== 'managed_by' ||
      this.network_interfaces.some((i) => values.includes(i.managed_by))
    );
  }

  // The filters in use, shared by the interface list and both address lists.
  // With `family`, also those that apply to its addresses only; without, only
  // those that apply to interfaces.
  _activeFilters(family = null) {
    return this.filters.filter((filter) => {
      const def = PrefsDialog._AddressFilters.find((d) => d.key === filter.key);
      return this._counts(filter) && (!def.family || def.family === family);
    });
  }

  // the `filters` that count with their definitions, split into those on
  // interfaces and those on IPv6 addresses
  _splitFilters(filters) {
    const defined = filters
      .filter((filter) => this._counts(filter))
      .map(({ key, values }) => ({
        def: PrefsDialog._AddressFilters.find((d) => d.key === key),
        values,
      }));
    return {
      for_addresses: defined.filter(({ def }) => def.family),
      for_interfaces: defined.filter(({ def }) => !def.family),
    };
  }

  static _passesAll(defined, address, iface) {
    return defined.every(({ def, values }) =>
      PrefsDialog._passesFilter(def, values, address, iface),
    );
  }

  // An interface is listed when it passes the filters on interfaces and,
  // while filters on IPv6 addresses are in use, still has an address to bind:
  // an IPv4 one, which they don't judge, or an IPv6 one they leave.
  _interfacePasses(filters, iface) {
    const { for_addresses, for_interfaces } = this._splitFilters(filters);
    return (
      PrefsDialog._passesAll(for_interfaces, null, iface) &&
      (for_addresses.length === 0 ||
        iface.addresses.some(
          (address) =>
            !address.address.includes(':') ||
            PrefsDialog._passesAll(for_addresses, address, iface),
        ))
    );
  }

  // `def`'s choices that something listed still has, as the other filters in
  // use leave it, so each filter narrows the next.
  _filterChoices(def) {
    const others = this.filters.filter(({ key }) => key !== def.key);

    const { for_addresses, for_interfaces } = this._splitFilters(others);

    const present = new Set();
    for (const iface of this.network_interfaces) {
      if (!def.family) {
        if (this._interfacePasses(others, iface)) {
          for (const v of def.values(null, iface)) {
            present.add(v);
          }
        }
        continue;
      }
      if (!PrefsDialog._passesAll(for_interfaces, null, iface)) {
        continue;
      }
      for (const address of iface.addresses) {
        if (
          address.address.includes(':') === (def.family === 'ipv6') &&
          PrefsDialog._passesAll(for_addresses, address, iface)
        ) {
          for (const v of def.values(address, iface)) {
            present.add(v);
          }
        }
      }
    }

    return def.choices(this.network_interfaces).filter(([v]) => present.has(v));
  }

  _setFilters(filters) {
    this.filters = filters.filter(({ values }) => values.length > 0);
    this._updateInterfaceSelect();
    this._updateInterfaceAddresses();
  }

  // Each ▽ button is dark gray while filters apply to its list, which its
  // tooltip names. `family` is null for the interface list's.
  _renderFilterButtons() {
    const { network } = this.elements;
    const buttons = [
      [network.interface_filter, null],
      [network.bind_address.ipv4.filter, 'ipv4'],
      [network.bind_address.ipv6.filter, 'ipv6'],
    ];
    for (const [button, family] of buttons) {
      const text = this._activeFilters(family)
        .map(({ key, values }) => {
          const def = PrefsDialog._AddressFilters.find((d) => d.key === key);
          const choices = def.choices(this.network_interfaces);
          const names = new Map(choices);
          // a filter that keeps most values reads better as what it leaves out
          const left_out = choices.filter(([v]) => !values.includes(v));
          if (left_out.length < values.length) {
            return `${def.label}: not ${left_out.map(([, name]) => name).join(', ')}`;
          }
          return `${def.label}: ${values.map((v) => names.get(v) ?? v).join(', ')}`;
        })
        .join('; ');
      button.classList.toggle('active', Boolean(text));
      button.title = text
        ? `Listing only what passes: ${text}`
        : 'Filter the entries listed';
    }
  }

  // Opens the filter menu under `anchor`: the filters that apply to its list,
  // `family`'s addresses or, without, the interfaces, each opening its
  // choices. Each change applies at once, to every list it applies to.
  _openFilterMenu(anchor, family = null) {
    this.filter_menu?.close();

    const root = document.createElement('div');
    root.role = 'menu';
    root.classList.add('address-filter-menu', 'popup');
    // a click can replace the menu's contents, detaching its target before
    // an OutsideClickListener would see it inside the menu
    root.addEventListener('click', (event_) => event_.stopPropagation());

    const outside = new OutsideClickListener(root);
    const close = () => {
      outside.stop();
      root.remove();
      if (this.filter_menu?.root === root) {
        this.filter_menu = null;
      }
    };
    outside.addEventListener('click', close);
    this.filter_menu = { close, root };

    const item = (text, on_click, ...classes) => {
      const div = document.createElement('div');
      div.role = 'menuitem';
      div.classList.add('address-filter-menuitem', ...classes);
      setTextContent(div, text);
      div.addEventListener('click', on_click);
      return div;
    };

    // Managed by is offered only where something other than Other manages an interface
    const has_managers = this.network_interfaces.some(
      (i) => i.managed_by && i.managed_by !== 'other',
    );
    const defs = PrefsDialog._AddressFilters.filter(
      (def) =>
        (def.key !== 'managed_by' || has_managers) &&
        (!def.family || def.family === family),
    );
    const valuesOf = (def) =>
      this.filters.find((f) => f.key === def.key)?.values ?? [];
    // a changed chip keeps its place; a new one goes last
    const setValues = (def, values) => {
      const filters = [...this.filters];
      const index = filters.findIndex((f) => f.key === def.key);
      if (index === -1) {
        filters.push({ key: def.key, values });
      } else {
        filters[index] = { key: def.key, values };
      }
      this._setFilters(filters);
    };

    // the menu's two views refer to each other
    const views = {};
    views.list = () => {
      const active = new Set(this._activeFilters(family).map((f) => f.key));
      // a filter with nothing left to choose isn't offered
      const offered = defs.filter((def) => this._filterChoices(def).length > 0);
      const items = offered.map((def) => {
        const div = item(
          def.label,
          () => views.choices(def),
          'address-filter-group',
        );
        div.classList.toggle('active', active.has(def.key));
        return div;
      });
      if (active.size > 0) {
        items.push(
          item(
            'Clear filters',
            () => {
              // only this list's: an IPv6 Type filter outlives the IPv4 list's Clear
              this._setFilters(this.filters.filter((f) => !active.has(f.key)));
              close();
            },
            'address-filter-clear',
          ),
        );
      }
      root.replaceChildren(...items);
    };

    views.choices = (def) => {
      const children = [
        item(`‹ ${def.label}`, () => views.list(), 'address-filter-back'),
      ];
      const choices = this._filterChoices(def);
      const selected = new Set(valuesOf(def));
      for (const [value, text] of choices) {
        const label = document.createElement('label');
        label.classList.add('address-filter-choice');
        const input = document.createElement('input');
        input.type = 'checkbox';
        input.checked = selected.has(value);
        input.addEventListener('change', () => {
          if (input.checked) {
            selected.add(value);
          } else {
            selected.delete(value);
          }
          // values keep the menu's order
          setValues(
            def,
            choices.map(([v]) => v).filter((v) => selected.has(v)),
          );
        });
        label.append(input, ` ${text}`);
        children.push(label);
      }
      root.replaceChildren(...children);
    };

    views.list();

    document.body.append(root);
    const where = anchor.getBoundingClientRect();
    const left = Math.min(
      where.left,
      document.documentElement.clientWidth - root.offsetWidth - 8,
    );
    root.style.left = `${Math.max(left, 0) + window.scrollX}px`;
    root.style.top = `${where.bottom + 4 + window.scrollY}px`;
  }

  // a button that copies `address`, labelled with the address and its tags
  static _createCopyButton({ address, tags }, about) {
    const text = address;
    const button = document.createElement('button');
    button.type = 'button';
    button.classList.add('interface-address');
    button.classList.toggle(
      'link-local',
      PrefsDialog._isLinkLocal({ address, tags }),
    );
    button.textContent = PrefsDialog._describeAddress({ address, tags });
    button.title = about ? `Click to copy\n\n${about}` : 'Click to copy';
    button.addEventListener('click', () => {
      if (!navigator.clipboard) {
        // navigator.clipboard requires HTTPS or localhost
        prompt('Select all then copy', text);
        return;
      }
      navigator.clipboard.writeText(text).then(
        () => {
          button.dataset.copied = 'true';
          setTimeout(() => delete button.dataset.copied, 1500);
        },
        () => prompt('Select all then copy', text),
      );
    });
    return button;
  }

  // says when the daemon's binding is stopping traffic, which a missing
  // interface or a refused binding does instead of using the default route
  _updateBindStatus() {
    const { network } = this.elements;
    const properties = this.session_manager.session_properties;
    const name = this._savedInterface();

    let state = null;
    let text = '';
    // the status describes the saved binding, not a choice still being saved
    if (network.interface_select.value === name) {
      switch (properties.bind_interface_status) {
        case 'missing':
          state = 'warning';
          text = `Kill switch on: ${name} is missing, so peer and tracker traffic is stopped until it returns.`;
          break;
        case 'bind_failed':
          state = 'error';
          text = `Kill switch on: binding to ${name} failed, so peer and tracker traffic is stopped. ${properties.bind_interface_error ?? ''}`;
          break;
        case 'blocked':
          state = 'warning';
          text = 'All peer and tracker traffic is blocked.';
          break;
        case 'unsupported':
          state = 'error';
          text = `The daemon can't bind to ${name} on its platform, so peer and tracker traffic is stopped.`;
          break;
        default:
          break;
      }
    }
    PrefsDialog._setFeedback(
      network.interface_status_label,
      state,
      text.trim(),
    );
  }

  _onBindAddressChanged(family) {
    const { feedback, select } = this.elements.network.bind_address[family];
    this.remote.savePrefs(
      { [select.dataset.key]: select.value },
      (response) => {
        if (this.closed) {
          return;
        }
        if (response?.error) {
          PrefsDialog._setFeedback(
            feedback,
            'error',
            response.error.data?.error_string ?? response.error.message,
          );
        } else {
          PrefsDialog._setFeedback(feedback, 'saved', 'Saved');
        }
      },
    );
  }

  static _setFeedback(element, state, text) {
    setTextContent(element, text);
    if (state) {
      element.dataset.state = state;
    } else {
      delete element.dataset.state;
    }
    element.classList.toggle('hidden', !text);
  }

  _onMaybePortChanged(key) {
    if (key === 'peer_port' || key === 'port_forwarding_enabled') {
      this._checkPort();
    }
  }

  // this callback is for controls whose changes can be applied
  // immediately, like checkboxs, radioboxes, and selects
  _onControlChanged(event_) {
    const { key } = event_.target.dataset;
    this.remote.savePrefs({
      [key]: PrefsDialog._getValue(event_.target),
    });
    this._onMaybePortChanged(key);
  }

  _onDialogClosed() {
    this.dispatchEvent(new Event('closed'));
  }

  // update the dialog's controls
  _update() {
    this._setBlocklistButtonEnabled(true);

    for (const [key, value] of Object.entries(
      this.session_manager.session_properties,
    )) {
      for (const element of this.elements.root.querySelectorAll(
        `[data-key="${key}"]`,
      )) {
        if (key === 'blocklist_size') {
          const n = Formatter.number(value);
          element.innerHTML = `Blocklist has <span class="blocklist-size-number">${n}</span> rules`;
          setTextContent(this.elements.peers.blocklist_update_button, 'Update');
        } else {
          switch (element.type) {
            case 'checkbox':
            case 'radio':
              element.checked = value;
              break;
            case 'text':
            case 'textarea':
            case 'url':
            case 'email':
            case 'number':
            case 'search':
              // don't change the text if the user's editing it.
              // it's very annoying when that happens!
              // Nor replace a value the dialog refused to send
              if (
                element !== document.activeElement &&
                element.getAttribute('aria-invalid') !== 'true'
              ) {
                // eslint-disable-next-line eqeqeq
                if (element.value != value) {
                  this._onMaybePortChanged(key);
                }
                element.value = value;
              }
              break;
            case 'select-one':
              element.value = value;
              break;
            default:
              console.log(element.type);
              break;
          }
        }

        element.dispatchEvent(new Event('session-change'));
      }
    }
  }

  static _createCheckAndLabel(id, text) {
    const root = document.createElement('div');
    root.id = id;

    const check = document.createElement('input');
    check.id = makeUUID();
    check.type = 'checkbox';
    root.append(check);

    const label = document.createElement('label');
    label.textContent = text;
    label.setAttribute('for', check.id);
    root.append(label);

    return { check, label, root };
  }

  static _enableIfChecked(element, check) {
    const callback = () => {
      if (element.tagName === 'INPUT') {
        setEnabled(element, check.checked);
      } else {
        element.classList.toggle('disabled', !check.checked);
      }
    };
    check.addEventListener('session-change', callback);
    callback();
  }

  static _getProtocolHandlerRegistered() {
    return localStorage.getItem('protocol-handler-registered') === 'true';
  }

  static _updateProtocolHandlerButton(button) {
    button.removeAttribute('disabled');
    button.removeAttribute('title');

    if (PrefsDialog._getProtocolHandlerRegistered()) {
      button.textContent = 'Remove Browser Handler';
      if (!('unregisterProtocolHandler' in navigator)) {
        button.setAttribute(
          'title',
          'Your browser does not support removing protocol handlers. This button only allows you to re-register a handler.',
        );
      }
    } else {
      button.textContent = 'Add Browser Handler';
      button.removeAttribute('title');
      if (!('registerProtocolHandler' in navigator)) {
        button.setAttribute('disabled', true);
        button.setAttribute(
          'title',
          'Your browser does not support protocol handlers',
        );
      }
    }
  }

  static _toggleProtocolHandler(button) {
    const handlerUrl = new URL(globalThis.location.href);
    handlerUrl.search = 'addtorrent=%s';
    if (this._getProtocolHandlerRegistered()) {
      navigator.unregisterProtocolHandler?.('magnet', handlerUrl.toString());
      localStorage.removeItem('protocol-handler-registered');
      PrefsDialog._updateProtocolHandlerButton(button);
    } else {
      navigator.registerProtocolHandler(
        'magnet',
        handlerUrl.toString(),
        'Retransmission Web',
      );
      localStorage.setItem('protocol-handler-registered', 'true');
      PrefsDialog._updateProtocolHandlerButton(button);
    }
  }

  static _createTorrentsPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-torrents-page');

    let label = document.createElement('div');
    label.textContent = 'Downloading';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Download to:';
    root.append(label);

    let input = document.createElement('input');
    input.type = 'text';
    input.id = makeUUID();
    input.dataset.key = 'download_dir';
    label.setAttribute('for', input.id);
    root.append(input);
    const download_dir = input;

    let cal = PrefsDialog._createCheckAndLabel(
      'incomplete-dir-div',
      'Use temporary folder:',
    );
    cal.check.title =
      'Separate folder to temporarily store downloads until they are complete.';
    cal.check.dataset.key = 'incomplete_dir_enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const incomplete_dir_check = cal.check;

    input = document.createElement('input');
    input.type = 'text';
    input.dataset.key = 'incomplete_dir';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const incomplete_dir_input = input;

    cal = PrefsDialog._createCheckAndLabel('autostart-div', 'Start when added');
    cal.check.dataset.key = 'start_added_torrents';
    root.append(cal.root);
    const autostart_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'suffix-div',
      `Append "part" to incomplete files' names`,
    );
    cal.check.dataset.key = 'rename_partial_files';
    root.append(cal.root);
    const suffix_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'download-queue-div',
      'Download queue size:',
    );
    cal.check.dataset.key = 'download_queue_enabled';
    root.append(cal.root);
    const download_queue_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'download_queue_size';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const download_queue_input = input;

    cal = PrefsDialog._createCheckAndLabel(
      'download-done-verify-div',
      'Verify data when download completes',
    );
    cal.check.dataset.key = 'torrent_complete_verify_enabled';
    root.append(cal.root);
    const download_done_verify_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Seeding';
    label.classList.add('section-label');
    root.append(label);

    cal = PrefsDialog._createCheckAndLabel(
      'stop-ratio-div',
      'Stop seeding at ratio:',
    );
    cal.check.dataset.key = 'seed_ratio_limited';
    root.append(cal.root);
    const stop_ratio_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.min = '0.1';
    input.step = 'any';
    input.dataset.key = 'seed_ratio_limit';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const stop_ratio_input = input;

    cal = PrefsDialog._createCheckAndLabel(
      'stop-idle-div',
      'Stop seeding if idle for N mins:',
    );
    cal.check.dataset.key = 'idle_seeding_limit_enabled';
    root.append(cal.root);
    const stop_idle_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.min = '0.1';
    input.step = 'any';
    input.dataset.key = 'idle_seeding_limit';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const stop_idle_input = input;

    label = document.createElement('div');
    label.textContent = 'Magnet Protocol Handler';
    label.classList.add('section-label');
    root.append(label);

    const button = document.createElement('button');
    button.classList.add('register-handler-button');
    PrefsDialog._updateProtocolHandlerButton(button);
    root.append(button);
    const register_handler_button = button;

    return {
      autostart_check,
      download_dir,
      download_done_verify_check,
      download_queue_check,
      download_queue_input,
      incomplete_dir_check,
      incomplete_dir_input,
      register_handler_button,
      root,
      stop_idle_check,
      stop_idle_input,
      stop_ratio_check,
      stop_ratio_input,
      suffix_check,
    };
  }

  static _createSpeedPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-speed-page');

    let label = document.createElement('div');
    label.textContent = 'Speed Limits';
    label.classList.add('section-label');
    root.append(label);

    let cal = PrefsDialog._createCheckAndLabel(
      'upload-speed-div',
      'Upload (kB/s):',
    );
    cal.check.dataset.key = 'speed_limit_up_enabled';
    root.append(cal.root);
    const upload_speed_check = cal.check;

    let input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'speed_limit_up';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const upload_speed_input = input;

    cal = PrefsDialog._createCheckAndLabel(
      'download-speed-div',
      'Download (kB/s):',
    );
    cal.check.dataset.key = 'speed_limit_down_enabled';
    root.append(cal.root);
    const download_speed_check = cal.check;

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'speed_limit_down';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const download_speed_input = input;

    label = document.createElement('div');
    label.textContent = 'Alternative Speed Limits';
    label.classList.add('section-label', 'alt-speed-section-label');
    root.append(label);

    label = document.createElement('div');
    label.textContent =
      'Override normal speed limits manually or at scheduled times';
    label.classList.add('alt-speed-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Upload (kB/s):';
    root.append(label);

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'alt_speed_up';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const alt_upload_speed_input = input;

    label = document.createElement('label');
    label.textContent = 'Download (kB/s):';
    root.append(label);

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'alt_speed_down';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const alt_download_speed_input = input;

    cal = PrefsDialog._createCheckAndLabel('alt-times-div', 'Scheduled times');
    cal.check.dataset.key = 'alt_speed_time_enabled';
    root.append(cal.root);
    const alt_times_check = cal.check;

    label = document.createElement('label');
    label.textContent = 'From:';
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    let select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'alt_speed_time_begin';
    PrefsDialog._initTimeDropDown(select);
    label.setAttribute('for', select.id);
    root.append(select);
    PrefsDialog._enableIfChecked(select, cal.check);
    const alt_from_select = select;

    label = document.createElement('label');
    label.textContent = 'To:';
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'alt_speed_time_end';
    PrefsDialog._initTimeDropDown(select);
    label.setAttribute('for', select.id);
    root.append(select);
    PrefsDialog._enableIfChecked(select, cal.check);
    const alt_to_select = select;

    label = document.createElement('label');
    label.textContent = 'On days:';
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'alt_speed_time_day';
    PrefsDialog._initDayDropDown(select);
    label.setAttribute('for', select.id);
    root.append(select);
    PrefsDialog._enableIfChecked(select, cal.check);
    const alt_days_select = select;

    return {
      alt_days_select,
      alt_download_speed_input,
      alt_from_select,
      alt_times_check,
      alt_to_select,
      alt_upload_speed_input,
      download_speed_check,
      download_speed_input,
      root,
      upload_speed_check,
      upload_speed_input,
    };
  }

  static _createPeersPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-peers-page');

    let label = document.createElement('div');
    label.textContent = 'Connections';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Max peers per torrent:';
    root.append(label);

    let input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'peer_limit_per_torrent';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const max_peers_per_torrent_input = input;

    label = document.createElement('label');
    label.textContent = 'Max peers overall:';
    root.append(label);

    input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'peer_limit_global';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const max_peers_overall_input = input;

    label = document.createElement('div');
    label.textContent = 'Options';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Encryption mode:';
    root.append(label);

    const select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'encryption';
    select.options[0] = new Option('Prefer encryption', 'preferred');
    select.options[1] = new Option('Allow encryption', 'tolerated');
    select.options[2] = new Option('Require encryption', 'required');
    root.append(select);
    const encryption_select = select;

    let cal = PrefsDialog._createCheckAndLabel(
      'use-pex-div',
      'Use PEX to find more peers',
    );
    cal.check.title =
      "PEX is a tool for exchanging peer lists with the peers you're connected to.";
    cal.check.dataset.key = 'pex_enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const pex_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'use-dht-div',
      'Use DHT to find more peers',
    );
    cal.check.title = 'DHT is a tool for finding peers without a tracker.';
    cal.check.dataset.key = 'dht_enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const dht_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'use-lpd-div',
      'Use LPD to find more peers',
    );
    cal.check.title = 'LPD is a tool for finding peers on your local network.';
    cal.check.dataset.key = 'lpd_enabled';
    cal.label.title = cal.check.title;
    root.append(cal.root);
    const lpd_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Blocklist';
    label.classList.add('section-label');
    root.append(label);

    cal = PrefsDialog._createCheckAndLabel(
      'blocklist-enabled-div',
      'Enable blocklist:',
    );
    cal.check.dataset.key = 'blocklist_enabled';
    root.append(cal.root);
    const blocklist_enabled_check = cal.check;

    input = document.createElement('input');
    input.type = 'url';
    input.value = 'http://www.example.com/blocklist';
    input.dataset.key = 'blocklist_url';
    root.append(input);
    PrefsDialog._enableIfChecked(input, cal.check);
    const blocklist_url_input = input;

    label = document.createElement('label');
    label.textContent = 'Blocklist has {n} rules';
    label.dataset.key = 'blocklist_size';
    label.classList.add('blocklist-size-label');
    PrefsDialog._enableIfChecked(label, cal.check);
    root.append(label);

    const button = document.createElement('button');
    button.classList.add('blocklist-update-button');
    button.textContent = 'Update';
    root.append(button);
    PrefsDialog._enableIfChecked(button, cal.check);
    const blocklist_update_button = button;

    return {
      blocklist_enabled_check,
      blocklist_update_button,
      blocklist_url_input,
      dht_check,
      encryption_select,
      lpd_check,
      max_peers_overall_input,
      max_peers_per_torrent_input,
      pex_check,
      root,
    };
  }

  static _createNetworkPage() {
    const root = document.createElement('div');
    root.classList.add('prefs-network-page');

    let label = document.createElement('div');
    label.textContent = 'Network Interface';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Network interface:';
    label.title =
      'Route peer and tracker traffic through this interface only, such as a VPN tunnel.';
    root.append(label);

    // the ▽ filter button, shared with the address lists, then the interface list
    const interface_row = document.createElement('div');
    interface_row.classList.add('address-filter-row');
    root.append(interface_row);

    const interface_filter = document.createElement('button');
    interface_filter.type = 'button';
    interface_filter.classList.add('address-filter-add');
    setTextContent(interface_filter, '▽');

    const select = document.createElement('select');
    select.id = makeUUID();
    select.dataset.key = 'bind_interface';
    // an interface may be named "100", which is still a name
    select.dataset.string = '';
    select.title = label.title;
    label.setAttribute('for', select.id);
    interface_row.append(interface_filter, select);
    const interface_select = select;

    const interface_addresses_label = document.createElement('div');
    interface_addresses_label.classList.add('interface-addresses', 'hidden');
    root.append(interface_addresses_label);

    const interface_status_label = document.createElement('div');
    interface_status_label.classList.add('interface-status', 'hidden');
    interface_status_label.setAttribute('aria-live', 'polite');
    root.append(interface_status_label);

    const createBindAddressSelect = (text, key) => {
      const address_label = document.createElement('label');
      address_label.textContent = text;
      address_label.title =
        'Listen for peers and send traffic from this local address only.';
      root.append(address_label);

      // the Filter button, then the list its filters narrow
      const row = document.createElement('div');
      row.classList.add('address-filter-row');
      root.append(row);

      const filter = document.createElement('button');
      filter.type = 'button';
      filter.classList.add('address-filter-add');
      setTextContent(filter, '▽');

      const address_select = document.createElement('select');
      // _update() sets it from this key, then _updateBindAddressSelect() rebuilds it
      address_select.dataset.key = key;
      address_select.id = makeUUID();
      address_select.title = address_label.title;
      address_label.setAttribute('for', address_select.id);
      row.append(filter, address_select);

      const feedback = document.createElement('div');
      feedback.classList.add('bind-address-feedback', 'hidden');
      feedback.setAttribute('aria-live', 'polite');
      feedback.id = makeUUID();
      address_select.setAttribute('aria-describedby', feedback.id);
      root.append(feedback);

      return { feedback, filter, select: address_select };
    };
    const bind_ipv4 = createBindAddressSelect(
      'IPv4 bind address:',
      'bind_address_ipv4',
    );
    const bind_ipv6 = createBindAddressSelect(
      'IPv6 bind address:',
      'bind_address_ipv6',
    );

    // shown but not editable: session_set can't change it, since moving the
    // listener could cut off the connection this dialog is using
    label = document.createElement('label');
    label.textContent = 'Web interface on:';
    label.title =
      'The interface this web interface is reachable through. Change it with rpc_bind_interface in settings.json or --rpc-bind-interface.';
    root.append(label);

    const rpc_interface_input = document.createElement('input');
    rpc_interface_input.type = 'text';
    rpc_interface_input.dataset.key = 'rpc_bind_interface';
    rpc_interface_input.id = makeUUID();
    rpc_interface_input.placeholder = 'Any interface';
    rpc_interface_input.title = label.title;
    rpc_interface_input.disabled = true;
    label.setAttribute('for', rpc_interface_input.id);
    label.classList.add('disabled');
    root.append(rpc_interface_input);

    label = document.createElement('div');
    label.textContent = 'Listening Port';
    label.classList.add('section-label');
    root.append(label);

    label = document.createElement('label');
    label.textContent = 'Peer listening port:';
    root.append(label);

    const input = document.createElement('input');
    input.type = 'number';
    input.dataset.key = 'peer_port';
    input.id = makeUUID();
    label.setAttribute('for', input.id);
    root.append(input);
    const port_input = input;

    const port_status_div = document.createElement('div');
    port_status_div.classList.add('port-status');
    label = document.createElement('label');
    label.textContent = 'IPv4 port is';
    port_status_div.append(label);
    const port_status_label_ipv4 = document.createElement('label');
    port_status_label_ipv4.textContent = '?';
    port_status_label_ipv4.classList.add('port-status-label');
    port_status_div.append(port_status_label_ipv4);
    port_status_div.append(document.createElement('br'));
    label = document.createElement('label');
    label.textContent = 'IPv6 port is';
    port_status_div.append(label);
    const port_status_label_ipv6 = document.createElement('label');
    port_status_label_ipv6.textContent = '?';
    port_status_label_ipv6.classList.add('port-status-label');
    port_status_div.append(port_status_label_ipv6);
    root.append(port_status_div);

    let cal = PrefsDialog._createCheckAndLabel(
      'randomize-port',
      'Randomize port on launch',
    );
    cal.check.dataset.key = 'peer_port_random_on_start';
    root.append(cal.root);
    const random_port_check = cal.check;

    cal = PrefsDialog._createCheckAndLabel(
      'port-forwarding',
      'Use port forwarding from my router',
    );
    cal.check.dataset.key = 'port_forwarding_enabled';
    root.append(cal.root);
    const port_forwarding_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Options';
    label.classList.add('section-label');
    root.append(label);

    cal = PrefsDialog._createCheckAndLabel(
      'utp-enabled',
      'Enable uTP for peer communication',
    );
    cal.check.dataset.key = 'utp_enabled';
    root.append(cal.root);
    const utp_check = cal.check;

    label = document.createElement('div');
    label.textContent = 'Default Public Trackers';
    label.classList.add('section-label');
    root.append(label);

    const tracker_labels = [
      'Trackers to use on all public torrents.',
      'To add a backup URL, add it on the next line after a primary URL.',
      'To add a new primary URL, add it after a blank line.',
    ];
    for (const text of tracker_labels) {
      label = document.createElement('label');
      label.classList.add('default-trackers-label');
      label.textContent = text;
      label.setAttribute('for', 'default-trackers');
      root.append(label);
    }

    const textarea = document.createElement('textarea');
    textarea.dataset.key = 'default_trackers';
    textarea.id = 'default-trackers';
    root.append(textarea);
    const default_trackers_textarea = textarea;

    return {
      // nested, so the generic change handler skips these validated inputs
      bind_address: { ipv4: bind_ipv4, ipv6: bind_ipv6 },
      default_trackers_textarea,
      interface_addresses_label,
      interface_status_label,
      interface_filter,
      interface_select,
      port_forwarding_check,
      port_input,
      port_status_label: {
        ipv4: port_status_label_ipv4,
        ipv6: port_status_label_ipv6,
      },
      random_port_check,
      root,
      utp_check,
    };
  }

  static _create() {
    const pages = {
      network: PrefsDialog._createNetworkPage(),
      peers: PrefsDialog._createPeersPage(),
      speed: PrefsDialog._createSpeedPage(),
      torrents: PrefsDialog._createTorrentsPage(),
    };

    const elements = createTextualTabsContainer('prefs-dialog', [
      ['prefs-tab-torrent', pages.torrents.root, 'Torrents'],
      ['prefs-tab-speed', pages.speed.root, 'Speed'],
      ['prefs-tab-peers', pages.peers.root, 'Peers'],
      ['prefs-tab-network', pages.network.root, 'Network'],
    ]);

    return { ...elements, ...pages };
  }

  constructor(session_manager, remote) {
    super();

    this.closed = false;
    this.session_manager = session_manager;
    this.remote = remote;
    this.update_from_session = () => this._update();

    this.elements = PrefsDialog._create();
    this.elements.peers.blocklist_update_button.addEventListener(
      'click',
      (event_) => {
        setTextContent(event_.target, 'Updating blocklist...');
        this.remote.updateBlocklist();
        this._setBlocklistButtonEnabled(false);
      },
    );
    this.elements.torrents.register_handler_button.addEventListener(
      'click',
      (event_) => {
        PrefsDialog._toggleProtocolHandler(event_.currentTarget);
      },
    );
    this.network_interfaces = [];
    this.elements.network.interface_select.addEventListener(
      'session-change',
      () => {
        this._updateInterfaceSelect();
        this._updateInterfaceAddresses();
        this._loadNetworkInterfaces();
      },
    );
    this.elements.network.interface_select.addEventListener('change', () =>
      this._updateInterfaceAddresses(),
    );
    // Interfaces come and go, and the daemon's first list after it starts has
    // no NetworkManager states yet, so the list is asked for again while open.
    this.interfaces_timer = setInterval(
      () => this._loadNetworkInterfaces(),
      5000,
    );
    this.filter_menu = null;
    // By default, list only the interfaces NetworkManager set up, where it
    // reports, and no link-local address, which no peer or tracker can reach.
    this.filters = [
      { key: 'managed_by', values: ['networkmanager'] },
      {
        key: 'type',
        values: Object.keys(PrefsDialog._Ranges).filter(
          (tag) => tag !== 'link-local',
        ),
      },
    ];
    this.elements.network.interface_filter.addEventListener(
      'click',
      (event_) => {
        event_.stopPropagation();
        this._openFilterMenu(this.elements.network.interface_filter);
      },
    );
    for (const [family, { filter, select }] of Object.entries(
      this.elements.network.bind_address,
    )) {
      select.addEventListener('change', () =>
        this._onBindAddressChanged(family),
      );
      filter.addEventListener('click', (event_) => {
        event_.stopPropagation();
        this._openFilterMenu(filter, family);
      });
    }
    this.elements.dismiss.addEventListener('click', () => this.close());
    this.outside = new OutsideClickListener(this.elements.root);
    this.outside.addEventListener('click', () => this.close());

    Object.seal(this);

    // listen for user input
    const on_change = this._onControlChanged.bind(this);
    const walk = (o) => {
      for (const element of Object.values(o)) {
        if (element.tagName === 'INPUT') {
          switch (element.type) {
            case 'checkbox':
            case 'radio':
            case 'number':
            case 'text':
            case 'url':
              element.addEventListener('change', on_change);
              break;
            default:
              console.trace(`unhandled input: ${element.type}`);
              break;
          }
        } else if (
          element.tagName === 'TEXTAREA' ||
          element.tagName === 'SELECT'
        ) {
          element.addEventListener('change', on_change);
        }
      }
    };
    walk(this.elements.network);
    walk(this.elements.peers);
    walk(this.elements.speed);
    walk(this.elements.torrents);

    this.session_manager.addEventListener(
      'session-change',
      this.update_from_session,
    );
    this.update_from_session();

    document.body.append(this.elements.root);
  }

  close() {
    if (!this.closed) {
      clearInterval(this.interfaces_timer);
      this.filter_menu?.close();
      this.outside.stop();
      this.session_manager.removeEventListener(
        'session-change',
        this.update_from_session,
      );
      this.elements.root.remove();
      this.dispatchEvent(new Event('close'));
      for (const key of Object.keys(this)) {
        this[key] = null;
      }
      this.closed = true;
    }
  }
}
