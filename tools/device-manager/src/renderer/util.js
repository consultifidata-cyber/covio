// util.js -- shared renderer-side formatting helpers, so each view doesn't
// reimplement the same health/state-to-pill mapping independently.

export function el(tag, attrs = {}, children = []) {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (k === 'class') node.className = v;
    else if (k === 'text') node.textContent = v;
    else if (k.startsWith('on') && typeof v === 'function') node.addEventListener(k.slice(2), v);
    else node.setAttribute(k, v);
  }
  for (const child of [].concat(children)) {
    if (child == null) continue;
    node.appendChild(typeof child === 'string' ? document.createTextNode(child) : child);
  }
  return node;
}

// §13 A.4 health_state: ok | degraded | offline
export function healthPill(state) {
  const map = { ok: ['pill-ok', 'OK'], degraded: ['pill-warn', 'DEGRADED'], offline: ['pill-crit', 'OFFLINE'] };
  const [cls, label] = map[state] || ['pill-neutral', state || 'UNKNOWN'];
  return el('span', { class: `pill ${cls}`, text: label });
}

// §13 A.4 ota_state: none | pending_verify | confirmed | failed
export function otaPill(state) {
  const map = {
    none: ['pill-neutral', 'NONE'],
    pending_verify: ['pill-warn', 'PENDING VERIFY'],
    confirmed: ['pill-ok', 'CONFIRMED'],
    failed: ['pill-crit', 'FAILED'],
  };
  const [cls, label] = map[state] || ['pill-neutral', state || 'UNKNOWN'];
  return el('span', { class: `pill ${cls}`, text: label });
}

// §13 A.4 api_key_status: configured | default | missing -- never the raw value.
export function apiKeyPill(state) {
  const map = {
    configured: ['pill-ok', 'CONFIGURED'],
    default: ['pill-warn', 'DEFAULT (unrotated)'],
    missing: ['pill-crit', 'MISSING'],
  };
  const [cls, label] = map[state] || ['pill-neutral', state || 'UNKNOWN'];
  return el('span', { class: `pill ${cls}`, text: label });
}

export function msAgo(ms) {
  if (ms == null) return 'never';
  if (ms < 1000) return 'just now';
  if (ms < 60000) return `${Math.round(ms / 1000)}s ago`;
  if (ms < 3600000) return `${Math.round(ms / 60000)}m ago`;
  return `${Math.round(ms / 3600000)}h ago`;
}

export function offlinePill() {
  return el('span', { class: 'pill pill-offline', text: 'UNREACHABLE' });
}

export function deviceLabel(device) {
  return device.location ? `${device.location} (${device.hardwareId})` : (device.hostname || device.ip || device.hardwareId);
}
