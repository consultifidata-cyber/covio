'use strict';

// qrGenerator.js -- DM-Phase 6 (§11.2/§11.7): renders a Logical Device ID as
// a QR code for the factory label-printing step. Generated in the MAIN
// process (same security model as every other capability -- see main.js's
// own header comment): the renderer is sandboxed with no npm module access,
// so this returns a data: URL string over IPC for the renderer to drop
// straight into an <img src>, rather than exposing the `qrcode` module (or
// any filesystem/network access) to the renderer itself.

const QRCode = require('qrcode');

// toDataURL is callback-based in older API surfaces but this version
// returns a Promise when no callback is supplied.
function generateQrDataUrl(text) {
  if (!text || typeof text !== 'string') {
    return Promise.reject(new Error('text must be a non-empty string'));
  }
  return QRCode.toDataURL(text, { margin: 1, width: 240 });
}

module.exports = { generateQrDataUrl };
