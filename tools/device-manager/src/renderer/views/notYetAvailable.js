// notYetAvailable.js -- shared honest "not yet available" stub, per §8's
// acceptance criteria: "tabs whose data isn't ready yet (Logs, Calibration
// governance fields) shown as honestly empty/'not yet available' rather
// than hidden or faked". Used for Logs, Calibration, and Factory Test.

import { el } from '../util.js';

export function renderNotYetAvailable(root, title, reason) {
  const wrap = el('div');
  wrap.appendChild(el('h1', { text: title }));
  wrap.appendChild(el('div', { class: 'empty-state' }, [
    el('p', { text: 'Not yet available.' }),
    el('p', { text: reason, class: 'text-small' }),
  ]));
  root.appendChild(wrap);
}
