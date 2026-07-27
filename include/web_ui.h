#pragma once

const char WEB_UI_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 TOTP Settings</title>
  <style>
    :root {
      --bg-gradient: linear-gradient(135deg, #0f172a 0%, #1e1b4b 100%);
      --card-bg: rgba(30, 41, 59, 0.65);
      --card-border: rgba(255, 255, 255, 0.08);
      --text-main: #f8fafc;
      --text-muted: #94a3b8;
      --accent: #6366f1;
      --accent-hover: #4f46e5;
      --danger: #ef4444;
      --danger-hover: #dc2626;
      --success: #10b981;
      --input-bg: rgba(15, 23, 42, 0.6);
      --input-border: rgba(255, 255, 255, 0.1);
      --font-stack: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    }

    * {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }

    body {
      font-family: var(--font-stack);
      background: var(--bg-gradient);
      color: var(--text-main);
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }

    /* Containers */
    .container {
      background: var(--card-bg);
      backdrop-filter: blur(16px);
      -webkit-backdrop-filter: blur(16px);
      border: 1px solid var(--card-border);
      border-radius: 16px;
      width: 100%;
      max-width: 600px;
      padding: 30px;
      box-shadow: 0 10px 40px rgba(0, 0, 0, 0.4);
      display: none; /* Controlled by JS */
    }

    .container.active {
      display: block;
    }

    h1 {
      font-size: 1.6rem;
      background: linear-gradient(to right, #a5b4fc, #818cf8);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      margin-bottom: 25px;
      text-align: center;
      font-weight: 700;
    }

    /* Tabs */
    .tabs {
      display: flex;
      border-bottom: 1px solid var(--card-border);
      margin-bottom: 20px;
      overflow-x: auto;
      gap: 10px;
    }

    .tab-btn {
      background: none;
      border: none;
      color: var(--text-muted);
      padding: 10px 14px;
      font-size: 0.9rem;
      font-weight: 500;
      cursor: pointer;
      position: relative;
      transition: color 0.2s;
      white-space: nowrap;
    }

    .tab-btn:hover {
      color: var(--text-main);
    }

    .tab-btn.active {
      color: var(--accent);
    }

    .tab-btn.active::after {
      content: '';
      position: absolute;
      bottom: -1px;
      left: 0;
      right: 0;
      height: 2px;
      background: var(--accent);
      border-radius: 2px;
    }

    .tab-content {
      display: none;
    }

    .tab-content.active {
      display: block;
    }

    /* Forms */
    .form-group {
      margin-bottom: 16px;
      display: flex;
      flex-direction: column;
    }

    .form-row {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 16px;
    }

    label {
      font-size: 0.8rem;
      color: var(--text-muted);
      margin-bottom: 6px;
      text-transform: uppercase;
      letter-spacing: 0.05em;
      font-weight: 600;
    }

    input, select {
      background: var(--input-bg);
      border: 1px solid var(--input-border);
      border-radius: 8px;
      padding: 10px 12px;
      color: var(--text-main);
      font-family: inherit;
      font-size: 0.95rem;
      transition: all 0.2s ease;
    }

    input:focus, select:focus {
      outline: none;
      border-color: var(--accent);
      box-shadow: 0 0 0 3px rgba(99, 102, 241, 0.25);
    }

    /* Buttons */
    .btn {
      background: var(--accent);
      color: #fff;
      border: none;
      border-radius: 8px;
      padding: 10px 18px;
      font-size: 0.95rem;
      font-weight: 600;
      cursor: pointer;
      transition: all 0.2s ease;
      display: inline-flex;
      justify-content: center;
      align-items: center;
      gap: 8px;
    }

    .btn:hover {
      background: var(--accent-hover);
    }

    .btn-danger {
      background: var(--danger);
    }

    .btn-danger:hover {
      background: var(--danger-hover);
    }

    .btn-secondary {
      background: rgba(255, 255, 255, 0.08);
      border: 1px solid var(--card-border);
      color: var(--text-main);
    }

    .btn-secondary:hover {
      background: rgba(255, 255, 255, 0.12);
    }

    .btn-full {
      width: 100%;
      padding: 12px;
    }

    /* Accounts Management */
    .accounts-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 12px;
    }

    .accounts-list {
      max-height: 250px;
      overflow-y: auto;
      margin-bottom: 15px;
      border: 1px solid var(--card-border);
      border-radius: 8px;
      padding: 8px;
      background: rgba(15, 23, 42, 0.25);
    }

    .account-item {
      display: grid;
      grid-template-columns: 2fr 3fr auto;
      gap: 8px;
      align-items: center;
      margin-bottom: 8px;
      padding-bottom: 8px;
      border-bottom: 1px solid rgba(255, 255, 255, 0.04);
    }

    .account-item:last-child {
      margin-bottom: 0;
      padding-bottom: 0;
      border-bottom: none;
    }

    /* Slot Visibility */
    .slot-labels-container {
      margin-top: 10px;
      border: 1px solid var(--card-border);
      border-radius: 8px;
      padding: 12px;
      background: rgba(15, 23, 42, 0.25);
    }

    .slot-row {
      display: flex;
      align-items: center;
      gap: 10px;
      margin-bottom: 8px;
    }

    .slot-row:last-child {
      margin-bottom: 0;
    }

    .slot-row label {
      margin-bottom: 0;
      width: 80px;
      flex-shrink: 0;
    }

    .slot-row input {
      flex-grow: 1;
    }

    /* Toast Notification */
    .toast {
      position: fixed;
      bottom: 20px;
      left: 50%;
      transform: translateX(-50%) translateY(100px);
      padding: 12px 24px;
      border-radius: 8px;
      color: #fff;
      font-weight: 500;
      box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4);
      z-index: 1000;
      opacity: 0;
      transition: all 0.3s cubic-bezier(0.175, 0.885, 0.32, 1.275);
    }

    .toast.show {
      transform: translateX(-50%) translateY(0);
      opacity: 1;
    }

    .toast-success { background: var(--success); }
    .toast-error { background: var(--danger); }

    /* Actions footers */
    .footer-actions {
      display: flex;
      justify-content: space-between;
      margin-top: 25px;
      padding-top: 15px;
      border-top: 1px solid var(--card-border);
    }

    /* Utilities */
    .hidden {
      display: none !important;
    }
  </style>
</head>
<body>

  <!-- LOGIN CONTAINER -->
  <div id="login-container" class="container">
    <h1>TOTP Device Config</h1>
    <form id="login-form">
      <div class="form-group">
        <label for="login-pass">Device Password</label>
        <input type="password" id="login-pass" required placeholder="Enter device password">
      </div>
      <button type="submit" class="btn btn-full">Unlock Config</button>
    </form>
  </div>

  <!-- MAIN CONFIG CONTAINER -->
  <div id="config-container" class="container">
    <h1>TOTP Portal Config</h1>
    
    <div class="tabs">
      <button class="tab-btn active" data-tab="wifi-ntp">Wi-Fi & Time</button>
      <button class="tab-btn" data-tab="slots">BLE Slots</button>
      <button class="tab-btn" data-tab="accounts">TOTP Accounts</button>
      <button class="tab-btn" data-tab="security">Security</button>
    </div>

    <form id="config-form">
      
      <!-- TAB: WIFI & NTP -->
      <div id="wifi-ntp" class="tab-content active">
        <h2>Network & Sync</h2>
        <div class="form-group">
          <label for="wifi-ssid">Wi-Fi SSID</label>
          <input type="text" id="wifi-ssid" placeholder="Enter SSID">
        </div>
        <div class="form-group">
          <label for="wifi-pass">Wi-Fi Password</label>
          <input type="password" id="wifi-pass" placeholder="Enter Password">
        </div>
        <div class="form-row">
          <div class="form-group">
            <label for="ntp-server">NTP Server</label>
            <input type="text" id="ntp-server" placeholder="162.159.200.1">
          </div>
          <div class="form-group">
            <label for="gmt-offset">Timezone (GMT Offset Sec)</label>
            <select id="gmt-offset">
              <option value="-43200">UTC-12</option>
              <option value="-39600">UTC-11</option>
              <option value="-36000">UTC-10</option>
              <option value="-32400">UTC-9</option>
              <option value="-28800">UTC-8</option>
              <option value="-25200">UTC-7</option>
              <option value="-21600">UTC-6</option>
              <option value="-18000">UTC-5</option>
              <option value="-14400">UTC-4</option>
              <option value="-12600">UTC-3:30</option>
              <option value="-10800">UTC-3</option>
              <option value="-7200">UTC-2</option>
              <option value="-3600">UTC-1</option>
              <option value="0">UTC+0 (GMT)</option>
              <option value="3600">UTC+1 (CET)</option>
              <option value="7200">UTC+2 (EET)</option>
              <option value="10800">UTC+3 (MSK)</option>
              <option value="12600">UTC+3:30</option>
              <option value="14400">UTC+4 (GST)</option>
              <option value="16200">UTC+4:30</option>
              <option value="18000">UTC+5</option>
              <option value="19800">UTC+5:30 (IST)</option>
              <option value="20700">UTC+5:45</option>
              <option value="21600">UTC+6</option>
              <option value="25200">UTC+7 (WIB)</option>
              <option value="28800">UTC+8 (SGT / HKT)</option>
              <option value="32400">UTC+9 (JST)</option>
              <option value="34200">UTC+9:30 (ACST)</option>
              <option value="36000">UTC+10 (AEST)</option>
              <option value="39600">UTC+11</option>
              <option value="43200">UTC+12 (NZST)</option>
            </select>
          </div>
        </div>
      </div>

      <!-- TAB: BLE SLOTS -->
      <div id="slots" class="tab-content">
        <h2>BLE Multipairing Slots</h2>
        <div class="form-group">
          <label for="num-slots">Number of Slots</label>
          <select id="num-slots">
            <option value="1">1 Slot</option>
            <option value="2">2 Slots</option>
            <option value="3">3 Slots</option>
            <option value="4">4 Slots</option>
            <option value="5">5 Slots</option>
            <option value="6">6 Slots</option>
            <option value="7">7 Slots</option>
            <option value="8">8 Slots</option>
          </select>
        </div>
        <div class="form-group">
          <label>Slot Labels</label>
          <div class="slot-labels-container" id="slot-labels-list">
            <!-- Dynamically populated 8 inputs -->
          </div>
        </div>
      </div>

      <!-- TAB: TOTP ACCOUNTS -->
      <div id="accounts" class="tab-content">
        <div class="accounts-header">
          <h2>TOTP Accounts</h2>
          <button type="button" class="btn btn-secondary" id="add-account-btn">+ Add Account</button>
        </div>
        <div class="accounts-list" id="accounts-list">
          <!-- Dynamically populated list of account inputs -->
        </div>
      </div>

      <!-- TAB: SECURITY -->
      <div id="security" class="tab-content">
        <h2>Password Options</h2>
        <div class="form-group">
          <label for="current-pass">Current Web UI Password</label>
          <input type="password" id="current-pass" placeholder="Confirm current password to make changes">
        </div>
        <div class="form-row">
          <div class="form-group">
            <label for="new-pass">New Password</label>
            <input type="password" id="new-pass" placeholder="Leave blank to keep same">
          </div>
          <div class="form-group">
            <label for="confirm-pass">Confirm New Password</label>
            <input type="password" id="confirm-pass" placeholder="Confirm new password">
          </div>
        </div>
      </div>

      <!-- FOOTER ACTIONS -->
      <div class="footer-actions">
        <button type="button" class="btn btn-secondary" id="reboot-btn">Reboot Device</button>
        <button type="submit" class="btn">Save & Reboot</button>
      </div>

    </form>
  </div>

  <!-- TOAST NOTIFICATION -->
  <div id="toast" class="toast">Status message</div>

  <script>
    // Elements
    const loginContainer = document.getElementById('login-container');
    const configContainer = document.getElementById('config-container');
    const loginForm = document.getElementById('login-form');
    const loginPass = document.getElementById('login-pass');
    
    const tabBtns = document.querySelectorAll('.tab-btn');
    const tabContents = document.querySelectorAll('.tab-content');
    
    const configForm = document.getElementById('config-form');
    const addAccountBtn = document.getElementById('add-account-btn');
    const accountsList = document.getElementById('accounts-list');
    const numSlotsSelect = document.getElementById('num-slots');
    const slotLabelsList = document.getElementById('slot-labels-list');
    const rebootBtn = document.getElementById('reboot-btn');
    const toast = document.getElementById('toast');

    let deviceSettings = null;

    // Toast Utility
    function showToast(message, isError = false) {
      toast.textContent = message;
      toast.className = `toast show ${isError ? 'toast-error' : 'toast-success'}`;
      setTimeout(() => {
        toast.className = 'toast';
      }, 4000);
    }

    // Tabs Controller
    tabBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        tabBtns.forEach(b => b.classList.remove('active'));
        tabContents.forEach(c => c.classList.remove('active'));
        btn.classList.add('active');
        document.getElementById(btn.dataset.tab).classList.add('active');
      });
    });

    // Toggle Slot inputs depending on selected slot counts
    function updateSlotInputsVisibility() {
      const count = parseInt(numSlotsSelect.value);
      for (let i = 0; i < 8; i++) {
        const row = document.getElementById(`slot-row-${i}`);
        if (row) {
          if (i < count) {
            row.classList.remove('hidden');
          } else {
            row.classList.add('hidden');
          }
        }
      }
    }
    numSlotsSelect.addEventListener('change', updateSlotInputsVisibility);

    // Initial slot generation (always 8 inputs, but shown/hidden dynamically)
    function initializeSlotInputs() {
      slotLabelsList.innerHTML = '';
      for (let i = 0; i < 8; i++) {
        const row = document.createElement('div');
        row.className = 'slot-row';
        row.id = `slot-row-${i}`;
        row.innerHTML = `
          <label>Slot ${i + 1}</label>
          <input type="text" id="slot-lbl-input-${i}" placeholder="Slot Name ${i + 1}" maxlength="12">
        `;
        slotLabelsList.appendChild(row);
      }
    }

    // Dynamic Account List Management
    function addAccountRow(name = '', secret = '') {
      const row = document.createElement('div');
      row.className = 'account-item';
      row.innerHTML = `
        <input type="text" class="acc-name" placeholder="Name (e.g. Google)" required maxlength="12" value="${name}">
        <input type="text" class="acc-sec" placeholder="Base32 Secret" required pattern="^[A-Za-z2-7= ]+$" value="${secret}">
        <button type="button" class="btn btn-danger" onclick="this.parentElement.remove()">Delete</button>
      `;
      accountsList.appendChild(row);
    }

    addAccountBtn.addEventListener('click', () => addAccountRow());

    // Load Settings
    async function loadSettings() {
      try {
        const response = await fetch('/api/settings');
        if (response.status === 401) {
          loginContainer.classList.add('active');
          configContainer.classList.remove('active');
          return;
        }
        if (!response.ok) throw new Error('Failed to load settings');
        
        deviceSettings = await response.json();
        
        // Show main panel
        loginContainer.classList.remove('active');
        configContainer.classList.add('active');
        
        // Populate inputs
        document.getElementById('wifi-ssid').value = deviceSettings.wifi_ssid || '';
        document.getElementById('wifi-pass').value = deviceSettings.wifi_pass || '';
        document.getElementById('ntp-server').value = deviceSettings.ntp_server || '';
        document.getElementById('gmt-offset').value = deviceSettings.gmt_offset !== undefined ? deviceSettings.gmt_offset : '7200';
        numSlotsSelect.value = deviceSettings.num_slots || '2';
        
        // Populate slots
        for (let i = 0; i < 8; i++) {
          const input = document.getElementById(`slot-lbl-input-${i}`);
          if (input) {
            input.value = (deviceSettings.slot_labels && deviceSettings.slot_labels[i]) || `Slot ${i + 1}`;
          }
        }
        updateSlotInputsVisibility();

        // Populate accounts
        accountsList.innerHTML = '';
        if (deviceSettings.accounts && deviceSettings.accounts.length > 0) {
          deviceSettings.accounts.forEach(acc => {
            addAccountRow(acc.name, acc.secret_b32);
          });
        }
      } catch (err) {
        showToast('Error loading settings from device: ' + err.message, true);
        // Fallback to login just in case
        loginContainer.classList.add('active');
        configContainer.classList.remove('active');
      }
    }

    // Login Form Submit
    loginForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      const password = loginPass.value;
      try {
        const response = await fetch('/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ password })
        });
        if (response.ok) {
          showToast('Login successful!');
          loginPass.value = '';
          loadSettings();
        } else {
          showToast('Incorrect password. Try again.', true);
        }
      } catch (err) {
        showToast('Network error during login: ' + err.message, true);
      }
    });

    // Config Form Submit
    configForm.addEventListener('submit', async (e) => {
      e.preventDefault();
      
      const pass = document.getElementById('current-pass').value;
      if (!pass) {
        showToast('Confirming current password is required to save changes', true);
        document.getElementById('current-pass').focus();
        return;
      }

      const newPassVal = document.getElementById('new-pass').value;
      const confirmPassVal = document.getElementById('confirm-pass').value;
      if (newPassVal && newPassVal !== confirmPassVal) {
        showToast('New passwords do not match!', true);
        return;
      }

      // Gather accounts
      const accounts = [];
      const nameInputs = accountsList.querySelectorAll('.acc-name');
      const secInputs = accountsList.querySelectorAll('.acc-sec');
      for (let i = 0; i < nameInputs.length; i++) {
        const name = nameInputs[i].value.trim();
        const secret = secInputs[i].value.replace(/\s+/g, '').toUpperCase();
        if (name && secret) {
          accounts.push({ name, secret_b32: secret });
        }
      }

      // Gather slot labels
      const slotLabels = [];
      for (let i = 0; i < 8; i++) {
        const input = document.getElementById(`slot-lbl-input-${i}`);
        slotLabels.push(input ? input.value.trim() : `Slot ${i + 1}`);
      }

      // Build JSON payload
      const payload = {
        current_pass: pass,
        wifi_ssid: document.getElementById('wifi-ssid').value.trim(),
        wifi_pass: document.getElementById('wifi-pass').value,
        ntp_server: document.getElementById('ntp-server').value.trim() || '162.159.200.1',
        gmt_offset: parseInt(document.getElementById('gmt-offset').value),
        num_slots: parseInt(numSlotsSelect.value),
        slot_labels: slotLabels,
        accounts: accounts
      };

      if (newPassVal) {
        payload.new_pass = newPassVal;
      }

      try {
        const response = await fetch('/api/settings', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(payload)
        });

        if (response.status === 401) {
          showToast('Invalid current password verification!', true);
          return;
        }

        if (response.ok) {
          showToast('Settings saved successfully! Rebooting device...');
          document.getElementById('current-pass').value = '';
          document.getElementById('new-pass').value = '';
          document.getElementById('confirm-pass').value = '';
          
          // Trigger reboot
          setTimeout(async () => {
            await fetch('/api/reboot', { method: 'POST' });
          }, 1500);
        } else {
          const errText = await response.text();
          showToast('Failed to save settings: ' + errText, true);
        }
      } catch (err) {
        showToast('Error saving settings: ' + err.message, true);
      }
    });

    // Manual Reboot
    rebootBtn.addEventListener('click', async () => {
      if (confirm('Reboot the ESP32 device?')) {
        try {
          showToast('Rebooting...');
          await fetch('/api/reboot', { method: 'POST' });
        } catch (err) {
          showToast('Reboot command sent.');
        }
      }
    });

    // Start App
    initializeSlotInputs();
    loadSettings();
  </script>
</body>
</html>
)rawhtml";
