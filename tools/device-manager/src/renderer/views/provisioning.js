// provisioning.js -- §11.6 "Provisioning" tab / §8's "AP-mode-aware setup
// wizard (detects Covio-Setup-* SSID, walks the operator through
// connecting, submitting config, and reconnecting)".
//
// IMPORTANT DESIGN NOTE: DM-Phase 2's POST /api/v1/config (the frozen §13
// JSON contract this app must use) does NOT live-test the submitted WiFi
// credentials before writing+rebooting -- only the firmware's own HTML
// captive-portal form does that (a documented DM-Phase 2 design decision:
// the JSON path's contract has no live-test error code defined). So a
// 200 {"success":true} here means "the device accepted the write and is
// rebooting", NOT "the WiFi password was correct". This wizard therefore
// verifies success indirectly, by watching for the device to reappear via
// mDNS discovery (already running continuously, see app.js) after
// submitting -- if it doesn't reappear within a generous timeout, the most
// likely explanation is a wrong password sent the device back into
// AP-fallback mode, and the wizard says so and offers a retry (§8 test
// case 3: "wrong-password retry handled gracefully in the wizard").

import { el } from '../util.js';

const DEFAULT_AP_IP = '192.168.4.1'; // §3.1's diagram: the ESP32 SoftAP's default gateway
const REAPPEAR_TIMEOUT_MS = 90000;   // AP_FALLBACK_TIMEOUT_MS (15s) + reboot + reconnect, generous margin
const REAPPEAR_POLL_MS = 3000;

const STEPS = ['detect', 'connect', 'configure', 'verify'];

export function renderProvisioning(root, app) {
  let step = 'detect';
  let connectedHost = DEFAULT_AP_IP;
  let deviceInfo = null; // { hardware_id, model, ... } from the AP-mode device's GET /api/v1/info
  let disposed = false;
  let verifyTimer = null;

  const wrap = el('div');
  wrap.appendChild(el('h1', { text: 'Provisioning' }));
  wrap.appendChild(el('p', { class: 'subtitle', text: 'Set up a brand-new device, or reconfigure one already in setup mode -- no cable required.' }));
  const stepsBar = el('div', { class: 'wizard-steps' });
  const body = el('div');
  wrap.append(stepsBar, body);
  root.appendChild(wrap);

  function renderStepsBar() {
    stepsBar.innerHTML = '';
    const labels = { detect: '1. Find Setup Network', connect: '2. Connect', configure: '3. Configure', verify: '4. Verify' };
    const idx = STEPS.indexOf(step);
    STEPS.forEach((s, i) => {
      stepsBar.appendChild(el('div', {
        class: `wizard-step ${i === idx ? 'active' : i < idx ? 'done' : ''}`,
        text: labels[s],
      }));
    });
  }

  function goTo(nextStep) {
    if (verifyTimer) { clearTimeout(verifyTimer); verifyTimer = null; }
    step = nextStep;
    renderStepsBar();
    body.innerHTML = '';
    if (step === 'detect') renderDetect();
    else if (step === 'connect') renderConnect();
    else if (step === 'configure') renderConfigure();
    else if (step === 'verify') renderVerify();
  }

  // ---- Step 1: detect -------------------------------------------------------
  async function renderDetect() {
    body.appendChild(el('p', {
      text: 'A brand-new (or newly reset) device broadcasts an open WiFi network named "Covio-Setup-XXXX". '
        + 'Connect your PC to that network using Windows\' own WiFi settings, then continue below.',
    }));
    const scanResult = el('div', { class: 'notice notice-info', text: 'Scanning for nearby setup networks...' });
    body.appendChild(scanResult);

    app.covio.wifi.scanNearbySetupNetworks().then((res) => {
      if (disposed) return;
      scanResult.innerHTML = '';
      if (!res.supported) {
        scanResult.className = 'notice notice-info';
        scanResult.textContent = 'Automatic network detection is only available on Windows -- connect manually and continue.';
      } else if (res.ssids.length === 0) {
        scanResult.className = 'notice notice-info';
        scanResult.textContent = 'No "Covio-Setup-*" networks currently visible. Make sure the device is powered on and nearby, then connect manually.';
      } else {
        scanResult.className = 'notice notice-success';
        scanResult.textContent = `Nearby: ${res.ssids.join(', ')} -- connect your PC to one of these, then continue.`;
      }
    });

    body.appendChild(el('button', { text: "I've connected -- Continue", onclick: () => goTo('connect') }));
  }

  // ---- Step 2: connect --------------------------------------------------------
  function renderConnect() {
    body.appendChild(el('p', { text: 'Confirming the device is reachable at its setup-mode address (usually 192.168.4.1):' }));
    const addrInput = el('input', { type: 'text', value: connectedHost });
    const checkBtn = el('button', { text: 'Check Connection' });
    body.appendChild(el('div', { class: 'toolbar' }, [addrInput, checkBtn]));
    const status = el('div');
    body.appendChild(status);
    body.appendChild(el('button', { class: 'secondary', text: 'Back', onclick: () => goTo('detect') }));

    checkBtn.addEventListener('click', async () => {
      status.innerHTML = '';
      checkBtn.disabled = true;
      const r = await app.covio.device.getInfo(addrInput.value.trim(), 80);
      checkBtn.disabled = false;
      if (!r.ok) {
        status.appendChild(el('div', {
          class: 'notice notice-error',
          text: `Could not reach a device there (${r.error.code}: ${r.error.message}). Make sure your PC is connected to the device's setup network.`,
        }));
        return;
      }
      connectedHost = addrInput.value.trim();
      deviceInfo = r.data;
      status.appendChild(el('div', {
        class: 'notice notice-success',
        text: `Found device ${r.data.hardware_id} (${r.data.model}, firmware ${r.data.fw_version}).`,
      }));
      setTimeout(() => { if (!disposed) goTo('configure'); }, 700);
    });
  }

  // ---- Step 3: configure -------------------------------------------------------
  function renderConfigure() {
    body.appendChild(el('p', { text: `Configuring ${deviceInfo ? deviceInfo.hardware_id : 'device'}. All fields are required.` }));

    const ssidInput = el('input', { type: 'text', placeholder: 'Your WiFi network name' });
    const passInput = el('input', { type: 'password', placeholder: 'Your WiFi password' });
    const urlInput = el('input', { type: 'text', placeholder: 'https://your-server.example.com' });
    const keyInput = el('input', { type: 'password', placeholder: 'Device API key' });

    body.append(
      el('label', { text: 'WiFi network name' }), ssidInput,
      el('label', { text: 'WiFi password' }), passInput,
      el('label', { text: 'Server URL' }), urlInput,
      el('label', { text: 'API key' }), keyInput,
    );

    const status = el('div');
    const submitBtn = el('button', { text: 'Save & Connect' });
    body.appendChild(el('div', { class: 'toolbar mt-1' }, [submitBtn, el('button', { class: 'secondary', text: 'Back', onclick: () => goTo('connect') })]));
    body.appendChild(status);

    submitBtn.addEventListener('click', async () => {
      status.innerHTML = '';
      // Client-side validation before submit (§8's own stated risk
      // mitigation for "operator error from a buggy config write") --
      // the device re-validates identically regardless; this is purely to
      // give immediate feedback without a round trip.
      const fields = {
        wifi_ssid: ssidInput.value.trim(),
        wifi_pass: passInput.value,
        server_url: urlInput.value.trim(),
        api_key: keyInput.value,
      };
      if (!fields.wifi_ssid || !fields.wifi_pass || !fields.server_url || !fields.api_key) {
        status.appendChild(el('div', { class: 'notice notice-error', text: 'All four fields are required.' }));
        return;
      }
      if (!/^https?:\/\//i.test(fields.server_url)) {
        status.appendChild(el('div', { class: 'notice notice-error', text: 'Server URL must start with http:// or https://' }));
        return;
      }

      submitBtn.disabled = true;
      status.appendChild(el('div', { class: 'notice notice-info', text: 'Saving to device...' }));
      const r = await app.covio.device.postConfig(connectedHost, 80, fields);
      submitBtn.disabled = false;
      status.innerHTML = '';

      if (!r.ok) {
        // A TIMEOUT here is actually the expected shape of *success*
        // (device rebooted mid-response) -- deviceClient.js's own message
        // already explains this; still route to verify either way, since
        // "no response because it's rebooting" and "no response because
        // it's broken" look identical from here and verify's mDNS-
        // reappearance check is what actually distinguishes them.
        if (r.error.code === 'TIMEOUT') { goTo('verify'); return; }
        status.appendChild(el('div', { class: 'notice notice-error', text: `${r.error.code}: ${r.error.message}` }));
        return;
      }
      goTo('verify');
    });
  }

  // ---- Step 4: verify (indirect -- see this file's header comment) ------------
  function renderVerify() {
    const targetHardwareId = deviceInfo && deviceInfo.hardware_id;
    const submittedAt = Date.now();
    body.appendChild(el('p', { text: 'Saved. The device is rebooting and connecting to your WiFi network now.' }));
    const status = el('div', { class: 'notice notice-info', text: 'Waiting for the device to reconnect (this can take up to a minute)...' });
    body.appendChild(status);

    function checkReappeared() {
      if (disposed) return;
      const dev = targetHardwareId ? app.state.devices.get(targetHardwareId) : null;
      if (dev && dev.lastSeenMs && dev.lastSeenMs > submittedAt && (dev.source === 'mdns')) {
        body.innerHTML = '';
        body.appendChild(el('div', { class: 'notice notice-success', text: `${targetHardwareId} reconnected successfully.` }));
        body.appendChild(el('p', { text: 'If your PC is still connected to the setup network, reconnect it to your normal WiFi now.' }));
        const goLiveBtn = el('button', { text: 'View in Live Monitor' });
        goLiveBtn.addEventListener('click', () => { app.state.selectDevice(targetHardwareId); app.navigateTo('live-monitor'); });
        body.appendChild(goLiveBtn);
        body.appendChild(el('button', { class: 'secondary', text: 'Provision Another Device', onclick: () => goTo('detect') }));
        return;
      }
      if (Date.now() - submittedAt > REAPPEAR_TIMEOUT_MS) {
        body.innerHTML = '';
        body.appendChild(el('div', {
          class: 'notice notice-error',
          text: "Couldn't confirm the device reconnected. This usually means the WiFi password was incorrect, and the device has returned to setup mode.",
        }));
        body.appendChild(el('p', { text: 'Reconnect your PC to the "Covio-Setup-*" network and try again.' }));
        body.appendChild(el('button', { text: 'Try Again', onclick: () => goTo('connect') }));
        return;
      }
      verifyTimer = setTimeout(checkReappeared, REAPPEAR_POLL_MS);
    }
    checkReappeared();
  }

  goTo('detect'); // also performs the initial renderStepsBar()

  return () => { disposed = true; if (verifyTimer) clearTimeout(verifyTimer); };
}
