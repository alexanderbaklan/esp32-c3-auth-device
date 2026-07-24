// =============================================================================
//  ESP32-C3  —  TOTP authentication device
// =============================================================================
//
//  A merge of two earlier prototypes:
//   * TOTP generation + OLED UI       — from the esp32-first project
//       (Base32 -> lucadentella/TOTP, Adafruit SSD1306, account navigation,
//        large code readout, 30s period progress bar).
//   * Multi-device BLE keyboard + WiFi + NTP — from the esp32-sec project
//       (raw NimBLEHIDDevice, one MAC per slot via esp_base_mac_addr_set + reboot
//        to switch; event-driven WiFi with AP cache; a small non-blocking NTP
//        client with background re-sync).
//
//  The device acts as a BLE HID keyboard: on a button press it "types" the
//  current TOTP code into the connected host. Multiple slots = multiple hosts
//  bonded independently (each slot has its own BLE MAC; the host reconnects
//  on its own).
//
//  Buttons (short to GND, external 4.7k pull-up to 3.3V -> idle = HIGH):
//     GPIO0  BTN_PREV  — previous account
//     GPIO1  BTN_NEXT  — next account
//     GPIO2  BTN_GEN   — type the current TOTP code over BLE
//     GPIO3  BTN_SLOT  — short press: next slot (save + reboot)
//                        hold 2s: clear the current slot's bond -> re-pair
//
//  NOTE on C3 strapping pins: GPIO0/GPIO2 are sampled at boot. They idle HIGH
//  (pull-up) so boot proceeds normally. Do not hold these buttons pressed
//  during reset/power-on.
//
//  Display: SSD1306/SSD1315 over I2C. On the C3 SuperMini I2C defaults to
//  SDA=8, SCL=9.
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Preferences.h>
#include <esp_mac.h>              // esp_read_mac / esp_base_mac_addr_set

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <WiFi.h>
#include <WiFiUdp.h>
#include <TOTP.h>
#include <time.h>
#include <sys/time.h>

// Private config: WiFi credentials + TotpAccount/ACCOUNTS (gitignored).
// Copy include/secrets.example.h to include/secrets.h and fill in your values.
#include "secrets.h"

// ==================== WiFi / NTP ====================
#define NTP_SERVER     "162.159.200.1"   // Cloudflare NTP (IP -> no DNS lookup)
#define GMT_OFFSET_SEC 7200              // UTC+2 (display clock only)
#define DST_OFFSET_SEC 0

// ==================== Display ====================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_ADDRESS  0x3C
#define I2C_SDA_PIN   8
#define I2C_SCL_PIN   9

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ==================== Buttons ====================
#define BTN_PREV   0    // GPIO0 — previous account
#define BTN_NEXT   1    // GPIO1 — next account
#define BTN_GEN    2    // GPIO2 — type code over BLE
#define BTN_SLOT   3    // GPIO3 — short: switch slot; hold: clear slot

// ==================== BLE multi-device ("one identity per slot") ============
// Each slot = a unique BLE MAC (esp_base_mac_addr_set before the BLE stack
// init), so every host stores its bond independently. Switching a slot changes
// the MAC -> done by saving the slot number to NVS and ESP.restart() (changing
// the MAC on the fly is unreliable in NimBLE-Arduino). A bonded host reconnects
// on its own via normal advertising as soon as we bring "its" slot up.
//
// NUM_SLOTS: 1 to 8 (upper bound = CONFIG_BT_NIMBLE_MAX_BONDS in platformio.ini).
// Each slot has a hardcoded name from SLOT_LABELS, shown on the display and used
// in the BLE device name ("TOTP Phone"). To add/remove a slot: change NUM_SLOTS
// and SLOT_LABELS so there are at least NUM_SLOTS names.
static const uint8_t NUM_SLOTS = 2;

static const char* SLOT_LABELS[] = {
  "Laptop",
  "Phone",
  "Slot 3",
  "Slot 4",
  "Slot 5",
  "Slot 6",
  "Slot 7",
  "Slot 8",
};
static_assert(NUM_SLOTS >= 1 && NUM_SLOTS <= 8, "NUM_SLOTS must be 1..8");
static_assert(sizeof(SLOT_LABELS) / sizeof(SLOT_LABELS[0]) >= NUM_SLOTS,
              "SLOT_LABELS must have at least NUM_SLOTS entries");

NimBLEServer*         pServer  = nullptr;
NimBLEHIDDevice*      pHid     = nullptr;
NimBLECharacteristic* pInput   = nullptr;   // Input Report (keys -> host)
NimBLEAdvertising*    pAdv     = nullptr;

volatile bool     gConnected  = false;
uint8_t           gSlot       = 0;
bool              gPairingMode = false;     // active slot has no bond yet

// Stored identity address of the bonded host per slot — used for deleteBond
// when clearing a slot. Not needed for reconnect (the host finds us by MAC).
bool    slotHasPeer[NUM_SLOTS];
uint8_t slotPeerAddr[NUM_SLOTS][6];
uint8_t slotPeerType[NUM_SLOTS];

Preferences prefs;               // NVS namespace "kbd"

// WiFi
bool     gWifiUp     = false;
bool     gTimeSynced = false;

// Access-point cache for a fast direct-connect (no all-channel scan).
struct ApHint {
  uint8_t bssid[6];
  uint8_t channel;
  bool    valid;
};
ApHint gApHint = {{0}, 0, false};

// HID Report Map for a standard keyboard (Report ID = 1)
static const uint8_t REPORT_MAP[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop)
  0x09, 0x06,        // Usage (Keyboard)
  0xA1, 0x01,        // Collection (Application)
  0x85, 0x01,        //   Report ID (1)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0xE0,        //   Usage Minimum (0xE0 = Left Ctrl)
  0x29, 0xE7,        //   Usage Maximum (0xE7 = Right GUI)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x08,        //   Report Count (8)
  0x81, 0x02,        //   Input (Data,Var,Abs) — modifier byte
  0x95, 0x01,        //   Report Count (1)
  0x75, 0x08,        //   Report Size (8)
  0x81, 0x01,        //   Input (Const) — reserved
  0x95, 0x06,        //   Report Count (6)
  0x75, 0x08,        //   Report Size (8)
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x65,        //   Logical Maximum (101)
  0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
  0x19, 0x00,        //   Usage Minimum (0)
  0x29, 0x65,        //   Usage Maximum (101)
  0x81, 0x00,        //   Input (Data,Array)
  0xC0               // End Collection
};

static const uint8_t KEY_MOD_LSHIFT = 0x02;
static const uint8_t KEY_ENTER = 0x28;
static const uint8_t KEY_SPACE = 0x2C;

// ==================== TOTP accounts ====================
// TotpAccount and ACCOUNTS[] live in include/secrets.h (gitignored).
const int NUM_ACCOUNTS = sizeof(ACCOUNTS) / sizeof(ACCOUNTS[0]);

int  currentAccount = 0;             // currently selected account
char currentCode[7] = "------";      // current TOTP code (6 digits)
long lastCodeTimestamp = 0;          // timestamp of the last generation

// ----------------------------- Prototypes -----------------------------------
void startAdvertising();
// Single screen render. note != nullptr -> the text is shown on the bottom line
// (the one place for all status messages) instead of the button hints.
void updateDisplay(const char* note = nullptr);

// ==================== Base32 decoder ====================
int base32Decode(const char* encoded, uint8_t* output, int outLen) {
  static const char* BASE32_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  int buffer = 0, bitsLeft = 0, count = 0;

  for (int i = 0; encoded[i] != '\0' && encoded[i] != '='; i++) {
    char c = toupper(encoded[i]);
    if (c == ' ' || c == '\n' || c == '\r') continue;
    const char* p = strchr(BASE32_CHARS, c);
    if (p == NULL) continue;
    int val = p - BASE32_CHARS;
    buffer = (buffer << 5) | val;
    bitsLeft += 5;
    if (bitsLeft >= 8) {
      if (count < outLen) output[count++] = (buffer >> (bitsLeft - 8)) & 0xFF;
      bitsLeft -= 8;
    }
  }
  return count;
}

// ==================== TOTP generation ====================
void generateTOTP() {
  if (!gTimeSynced) {
    strcpy(currentCode, "NOSYNC");
    return;
  }

  uint8_t hmacKey[32];
  int keyLen = base32Decode(ACCOUNTS[currentAccount].secret_b32, hmacKey, sizeof(hmacKey));
  if (keyLen <= 0) {
    strcpy(currentCode, "ERROR");
    return;
  }

  // System clock is kept in UTC (TZ=UTC0) -> time() yields the UTC epoch for TOTP.
  time_t now;
  time(&now);

  TOTP totp(hmacKey, keyLen);
  char* code = totp.getCode((long)now);
  if (code != NULL) {
    strncpy(currentCode, code, 6);
    currentCode[6] = '\0';
    lastCodeTimestamp = now;
    Serial.printf("TOTP [%s]: %s (t=%ld)\n",
                  ACCOUNTS[currentAccount].name, currentCode, (long)now);
  } else {
    strcpy(currentCode, "FAIL");
  }
}

// ==================== Sending keys to the host ====================
// 8-byte report: [mod, reserved, k0..k5]
static void sendReport(uint8_t modifier, uint8_t keycode) {
  if (!gConnected || pInput == nullptr) return;
  uint8_t rpt[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
  pInput->setValue(rpt, sizeof(rpt));
  pInput->notify();
}

static void releaseKeys() {
  if (!gConnected || pInput == nullptr) return;
  uint8_t rpt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  pInput->setValue(rpt, sizeof(rpt));
  pInput->notify();
}

static void tapKey(uint8_t modifier, uint8_t keycode) {
  sendReport(modifier, keycode);
  delay(8);
  releaseKeys();
  delay(8);
}

// ASCII -> (modifier, keycode). Returns false for unsupported characters.
static bool asciiToKey(char c, uint8_t& mod, uint8_t& key) {
  mod = 0;
  if (c >= 'a' && c <= 'z') { key = 0x04 + (c - 'a'); return true; }
  if (c >= 'A' && c <= 'Z') { key = 0x04 + (c - 'A'); mod = KEY_MOD_LSHIFT; return true; }
  if (c >= '1' && c <= '9') { key = 0x1E + (c - '1'); return true; }
  switch (c) {
    case '0':  key = 0x27; return true;
    case ' ':  key = KEY_SPACE; return true;
    case '\n': key = KEY_ENTER; return true;
    case '\t': key = 0x2B; return true;
    default:   return false;
  }
}

static void typeText(const char* text) {
  if (!gConnected) { updateDisplay("! BLE not connected"); return; }
  for (const char* p = text; *p; ++p) {
    uint8_t mod, key;
    if (asciiToKey(*p, mod, key)) tapKey(mod, key);
  }
}

// ==================== BLE slots: persistence ====================
// NVS namespace "kbd":
//   "slot"                 -> currently active slot (uint8)
//   "acct"                 -> selected TOTP account (uint8, survives reboot)
//   "hasp%d" (bool)        -> whether the slot has a bonded peer
//   "peer%d" (bytes[6])    -> host identity address
//   "ptype%d" (uint8)      -> type of that address
//   "apb"/"apc"            -> cached AP BSSID/channel

void loadSlotState() {
  prefs.begin("kbd", true);      // read-only
  gSlot = prefs.getUChar("slot", 0);
  if (gSlot >= NUM_SLOTS) gSlot = 0;

  currentAccount = prefs.getUChar("acct", 0);
  if (currentAccount >= NUM_ACCOUNTS) currentAccount = 0;

  for (int i = 0; i < NUM_SLOTS; i++) {
    char key[8];
    snprintf(key, sizeof(key), "hasp%d", i);
    slotHasPeer[i] = prefs.getBool(key, false);
    slotPeerType[i] = BLE_ADDR_PUBLIC;
    memset(slotPeerAddr[i], 0, 6);
    if (slotHasPeer[i]) {
      snprintf(key, sizeof(key), "peer%d", i);
      if (prefs.getBytes(key, slotPeerAddr[i], 6) != 6) slotHasPeer[i] = false;
      snprintf(key, sizeof(key), "ptype%d", i);
      slotPeerType[i] = prefs.getUChar(key, BLE_ADDR_PUBLIC);
    }
  }
  prefs.end();
}

void savePeer(int slot) {
  prefs.begin("kbd", false);
  char key[8];
  snprintf(key, sizeof(key), "hasp%d", slot);
  prefs.putBool(key, slotHasPeer[slot]);
  if (slotHasPeer[slot]) {
    snprintf(key, sizeof(key), "peer%d", slot);
    prefs.putBytes(key, slotPeerAddr[slot], 6);
    snprintf(key, sizeof(key), "ptype%d", slot);
    prefs.putUChar(key, slotPeerType[slot]);
  }
  prefs.end();
}

// ==================== Server callbacks ====================
class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    gConnected = true;
    s->updateConnParams(desc->conn_handle, 12, 24, 0, 200);  // responsive typing
    Serial.println("BLE: connected");
    updateDisplay();
  }

  void onDisconnect(NimBLEServer* s) override {
    gConnected = false;
    Serial.println("BLE: disconnected");
    startAdvertising();          // available again for reconnect
    updateDisplay();
  }

  // "Just Works" pairing without a passkey.
  uint32_t onPassKeyRequest() override { return 0; }
  bool onConfirmPIN(uint32_t) override { return true; }

  // On bonding completion, remember the host identity address in the current
  // slot (for a future deleteBond) and leave pairing mode.
  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    if (!desc->sec_state.bonded) {
      Serial.println("BLE: auth complete, NOT bonded");
      return;
    }
    memcpy(slotPeerAddr[gSlot], desc->peer_id_addr.val, 6);
    slotPeerType[gSlot] = desc->peer_id_addr.type;
    slotHasPeer[gSlot] = true;
    savePeer(gSlot);
    gPairingMode = false;

    NimBLEAddress id(desc->peer_id_addr);
    Serial.printf("BLE: slot %u bonded with %s\n",
                  (unsigned)(gSlot + 1), id.toString().c_str());
    updateDisplay("BLE Paired!");
    delay(800);
    updateDisplay();
  }
};

// ==================== BLE: slot address and startup ====================
// Unique base MAC for the slot: factory BT MAC with the low nibble of the last
// byte set to the slot number. Must be called BEFORE NimBLEDevice::init().
static void setSlotMac(uint8_t slot) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BT);
  mac[5] = (uint8_t)((mac[5] & 0xF0) | (slot & 0x0F));
  esp_base_mac_addr_set(mac);
}

void startAdvertising() {
  if (pAdv == nullptr) return;
  if (pAdv->isAdvertising()) pAdv->stop();
  pAdv->start();
}

static void initBle() {
  char name[24];
  snprintf(name, sizeof(name), "TOTP %s", SLOT_LABELS[gSlot]);

  setSlotMac(gSlot);                                  // MAC — strictly before init()

  NimBLEDevice::init(name);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setSecurityAuth(true, false, true);   // bonding, no MITM, SC
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCB());

  pHid = new NimBLEHIDDevice(pServer);
  pInput = pHid->inputReport(1);                      // Report ID = 1
  pHid->manufacturer()->setValue("DIY");
  pHid->pnp(0x02, 0xE502, 0xA111, 0x0210);            // sig, vid, pid, version
  pHid->hidInfo(0x00, 0x01);                          // country=0, remote wake
  pHid->reportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
  pHid->startServices();
  pHid->setBatteryLevel(100);

  pAdv = NimBLEDevice::getAdvertising();
  pAdv->setAppearance(0x03C1);                        // HID Keyboard
  pAdv->addServiceUUID(pHid->hidService()->getUUID());
  pAdv->setScanResponse(true);

  // Empty slot => pairing mode (same advertising, just no bond yet).
  gPairingMode = !slotHasPeer[gSlot];
  startAdvertising();

  Serial.printf("BLE: slot %u '%s', peer=%s\n",
                (unsigned)(gSlot + 1), name, slotHasPeer[gSlot] ? "yes" : "none");
}

// Switch slot: save to NVS and reboot (the new MAC is applied at startup).
void switchToSlot(uint8_t slot) {
  slot %= NUM_SLOTS;
  Serial.printf("BLE: switching to slot %u (%s) -> reboot\n",
                (unsigned)slot, SLOT_LABELS[slot]);
  char msg[24];
  snprintf(msg, sizeof(msg), "-> %s", SLOT_LABELS[slot]);
  updateDisplay(msg);
  prefs.begin("kbd", false);
  prefs.putUChar("slot", slot);
  prefs.putUChar("acct", (uint8_t)currentAccount);   // survive reboot
  prefs.end();
  delay(300);
  ESP.restart();
}

// Clear the current slot: delete the host bond -> fresh pairing on this slot.
void clearSlot() {
  Serial.printf("BLE: clearing slot %u\n", (unsigned)(gSlot + 1));
  updateDisplay("Forgetting device..");

  // Drop the active connection
  if (pServer) {
    for (uint16_t h : pServer->getPeerDevices()) pServer->disconnect(h);
  }
  delay(200);

  // Delete the bonded host's bond
  if (slotHasPeer[gSlot]) {
    ble_addr_t a;
    a.type = slotPeerType[gSlot];
    memcpy(a.val, slotPeerAddr[gSlot], 6);
    NimBLEAddress addr(a);
    NimBLEDevice::deleteBond(addr);
    Serial.printf("BLE: bond %s deleted\n", addr.toString().c_str());
  }

  slotHasPeer[gSlot] = false;
  memset(slotPeerAddr[gSlot], 0, 6);
  slotPeerType[gSlot] = BLE_ADDR_PUBLIC;
  savePeer(gSlot);
  gPairingMode = true;

  startAdvertising();

  char msg[24];
  snprintf(msg, sizeof(msg), "Pairing: %s", SLOT_LABELS[gSlot]);
  updateDisplay(msg);
  delay(600);
}

// ==================== Display ====================
void updateDisplay(const char* note) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // === Line 1: slot name + BLE status + clock ===
  display.setCursor(0, 0);
  const char* status = gConnected ? "ready" : (gPairingMode ? "Pair" : "wait");
  display.printf("%s %s", SLOT_LABELS[gSlot], status);

  // Clock on the right (local = UTC + offset)
  if (gTimeSynced) {
    time_t now;
    time(&now);
    time_t local = now + GMT_OFFSET_SEC + DST_OFFSET_SEC;
    struct tm tmv;
    gmtime_r(&local, &tmv);
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
    display.setCursor(SCREEN_WIDTH - (5 * 6), 0);
    display.print(timeBuf);
  }

  // === Separator ===
  display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, SSD1306_WHITE);

  // === Line 2: [N/M] account name ===
  display.setCursor(0, 13);
  display.printf("[%d/%d] ", currentAccount + 1, NUM_ACCOUNTS);
  display.print(ACCOUNTS[currentAccount].name);

  // === Center: TOTP code in a large font "XXX XXX" ===
  display.setTextSize(2);
  char formattedCode[8];
  snprintf(formattedCode, sizeof(formattedCode), "%.3s %.3s", currentCode, currentCode + 3);
  display.setCursor((SCREEN_WIDTH - 84) / 2, 24);
  display.print(formattedCode);

  // === Progress bar (seconds until the period rolls over) ===
  display.setTextSize(1);
  time_t now;
  time(&now);
  int secondsLeft = 30 - (int)(now % 30);
  int barWidth = 100, barHeight = 6, barY = 44, barX = 0;
  int filled = (int)((float)(30 - secondsLeft) / 30.0f * barWidth);
  display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
  display.fillRect(barX, barY, filled, barHeight, SSD1306_WHITE);
  char secBuf[5];
  snprintf(secBuf, sizeof(secBuf), "%2ds", secondsLeft);
  display.setCursor(barX + barWidth + 4, barY - 1);
  display.print(secBuf);

  // === Bottom line: status message OR button hints ===
  // The single place for all status messages (note). No note -> hints.
  display.setCursor(0, 56);
  if (note) {
    display.print(note);
  } else {
    display.print("#-send code *-switch");
  }
  display.display();
}

// ==================== WiFi / NTP ====================
// Event callback: capture the moment we get an IP and cache the AP BSSID/channel.
static void onWifiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      if (!gWifiUp) {
        gWifiUp = true;
        Serial.printf("[WiFi] GOT_IP ip=%s rssi=%d ch=%d\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI(), WiFi.channel());
        uint8_t ch = WiFi.channel();
        const uint8_t* bssid = WiFi.BSSID();
        if (bssid && ch >= 1 && ch <= 14 &&
            (!gApHint.valid || gApHint.channel != ch ||
             memcmp(gApHint.bssid, bssid, 6) != 0)) {
          memcpy(gApHint.bssid, bssid, 6);
          gApHint.channel = ch;
          gApHint.valid = true;
          prefs.begin("kbd", false);
          prefs.putBytes("apb", gApHint.bssid, 6);
          prefs.putUChar("apc", gApHint.channel);
          prefs.end();
          Serial.println("[WiFi] AP hint saved");
        }
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      gWifiUp = false;
      WiFi.reconnect();
      break;
    default:
      break;
  }
}

void startWifi() {
  WiFi.persistent(true);
  WiFi.onEvent(onWifiEvent);
  WiFi.mode(WIFI_STA);
  // NOTE: the ESP32-C3 has a single radio. For WiFi+BLE coexistence WiFi MUST be
  // in modem-sleep (WIFI_PS_MIN_MODEM). WIFI_PS_NONE crashes the BT controller.
  WiFi.setSleep(WIFI_PS_MIN_MODEM);

  // AP cache -> direct connect to the BSSID on a known channel (no full scan).
  prefs.begin("kbd", true);
  size_t n = prefs.getBytes("apb", gApHint.bssid, 6);
  gApHint.channel = prefs.getUChar("apc", 0);
  prefs.end();
  gApHint.valid = (n == 6 && gApHint.channel >= 1 && gApHint.channel <= 14);

  if (gApHint.valid) {
    Serial.printf("[WiFi] direct connect ch=%d\n", gApHint.channel);
    WiFi.begin(WIFI_SSID, WIFI_PASS, gApHint.channel, gApHint.bssid);
  } else {
    Serial.println("[WiFi] full scan connect (learning AP)");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// --- Small non-blocking NTP client with fast retry and background re-sync.
static WiFiUDP        gNtpUdp;
static const uint16_t NTP_LOCAL_PORT = 2390;
static const uint16_t NTP_PORT       = 123;
static const uint32_t NTP_RETRY_MS   = 250;
static const uint32_t NTP_UNIX_DELTA = 2208988800UL;         // 1900 -> 1970
static const uint32_t NTP_RESYNC_MS  = 6UL * 60 * 60 * 1000; // re-sync every 6h
static uint32_t gLastSyncOkMs = 0;

static void ntpSendRequest() {
  uint8_t pkt[48] = {0};
  pkt[0] = 0b11100011;   // LI=3, Version=4, Mode=3 (client)
  IPAddress srv;
  srv.fromString(NTP_SERVER);
  gNtpUdp.beginPacket(srv, NTP_PORT);
  gNtpUdp.write(pkt, sizeof(pkt));
  gNtpUdp.endPacket();
}

static bool ntpParseResponse() {
  int sz = gNtpUdp.parsePacket();
  if (sz < 48) return false;
  uint8_t buf[48];
  gNtpUdp.read(buf, 48);
  uint32_t secs1900 = ((uint32_t)buf[40] << 24) | ((uint32_t)buf[41] << 16) |
                      ((uint32_t)buf[42] << 8)  |  (uint32_t)buf[43];
  time_t epoch = (time_t)(secs1900 - NTP_UNIX_DELTA);
  struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
  settimeofday(&tv, nullptr);
  return true;
}

// Non-blocking sync upkeep: first sync + background re-sync (RTC drift).
void pollNtp() {
  static bool     udpBegun   = false;
  static bool     syncing    = false;
  static uint32_t lastSendMs = 0;

  if (!gWifiUp) return;
  if (!udpBegun) { udpBegun = true; gNtpUdp.begin(NTP_LOCAL_PORT); }

  bool needFirst  = !gTimeSynced;
  bool needResync = gTimeSynced && (millis() - gLastSyncOkMs) >= NTP_RESYNC_MS;

  if ((needFirst || needResync) && !syncing) {
    syncing = true;
    ntpSendRequest();
    lastSendMs = millis();
    Serial.printf("[NTP] %s request\n", needFirst ? "first" : "resync");
  }
  if (!syncing) return;

  if (ntpParseResponse()) {
    bool wasFirst = !gTimeSynced;
    gTimeSynced   = true;
    gLastSyncOkMs = millis();
    syncing = false;
    time_t now; time(&now);
    Serial.printf("[NTP] %s ok, epoch=%ld\n", wasFirst ? "synced" : "resynced", (long)now);
    if (wasFirst) { generateTOTP(); updateDisplay(); }
    return;
  }
  if (millis() - lastSendMs >= NTP_RETRY_MS) {
    ntpSendRequest();
    lastSendMs = millis();
  }
}

// Blocking WiFi+NTP sync BEFORE starting BLE (radio is free -> fast sync).
// Timeout safeguards so we never hang without a network.
static void syncTimeBlocking(uint32_t wifiTimeoutMs, uint32_t ntpTimeoutMs) {
  setenv("TZ", "UTC0", 1);        // keep system time in UTC; apply offset on print
  tzset();

  // WARM restart: the RTC survived ESP.restart() and time is already valid ->
  // no NTP needed.
  if (time(nullptr) > 1700000000) {          // ~2023-11
    gTimeSynced = true;
    gLastSyncOkMs = millis();
    Serial.println("[NTP] RTC time still valid -> skip NTP");
    return;
  }

  uint32_t t0 = millis();
  while (!gWifiUp && (millis() - t0) < wifiTimeoutMs) delay(5);
  if (!gWifiUp) { Serial.println("[boot] WiFi timeout -> continue without time"); return; }

  gNtpUdp.begin(NTP_LOCAL_PORT);
  ntpSendRequest();
  uint32_t lastSend = millis(), tn = millis();
  while (!gTimeSynced && (millis() - tn) < ntpTimeoutMs) {
    if (ntpParseResponse()) {
      gTimeSynced   = true;
      gLastSyncOkMs = millis();
      time_t now; time(&now);
      Serial.printf("[NTP] (blocking) synced epoch=%ld\n", (long)now);
      return;
    }
    if (millis() - lastSend >= NTP_RETRY_MS) { ntpSendRequest(); lastSend = millis(); }
    delay(2);
  }
  if (!gTimeSynced) Serial.println("[boot] NTP timeout -> will retry in loop");
}

// ==================== Button handling ====================
struct Button {
  uint8_t  pin;
  bool     lastStable;     // true = released (HIGH)
  bool     lastRead;
  uint32_t lastChangeMs;
  uint32_t pressedAtMs;
};

static Button gButtons[] = {
  {BTN_PREV, true, true, 0, 0},
  {BTN_NEXT, true, true, 0, 0},
  {BTN_GEN,  true, true, 0, 0},
  {BTN_SLOT, true, true, 0, 0},
};
static const uint8_t  NUM_BTN      = sizeof(gButtons) / sizeof(gButtons[0]);
static const uint32_t DEBOUNCE_MS  = 25;
static const uint32_t LONGPRESS_MS = 2000;

// 0 = nothing, 1 = short press, 2 = long hold.
static uint8_t pollButton(Button& b) {
  uint8_t event = 0;
  bool level = digitalRead(b.pin);        // HIGH = released, LOW = pressed
  uint32_t now = millis();

  if (level != b.lastRead) { b.lastRead = level; b.lastChangeMs = now; }

  if ((now - b.lastChangeMs) > DEBOUNCE_MS && level != b.lastStable) {
    b.lastStable = level;
    if (level == LOW) {
      b.pressedAtMs = now;
    } else if (b.pressedAtMs != 0) {
      event = 1;                          // release -> short press
      b.pressedAtMs = 0;
    }
  }
  if (b.lastStable == LOW && b.pressedAtMs != 0 &&
      (now - b.pressedAtMs) > LONGPRESS_MS) {
    event = 2;                            // long-press
    b.pressedAtMs = 0;                    // don't repeat until released
  }
  return event;
}

static void handleButton(uint8_t pin, uint8_t event) {
  switch (pin) {
    case BTN_PREV:
      if (event == 1) {
        currentAccount = (currentAccount - 1 + NUM_ACCOUNTS) % NUM_ACCOUNTS;
        generateTOTP();
        updateDisplay();
      }
      break;

    case BTN_NEXT:
      if (event == 1) {
        currentAccount = (currentAccount + 1) % NUM_ACCOUNTS;
        generateTOTP();
        updateDisplay();
      }
      break;

    case BTN_GEN:
      if (event == 1) {
        generateTOTP();
        if (gConnected) {
          typeText(currentCode);
          Serial.printf("BLE sent: %s\n", currentCode);
          updateDisplay(">>> SENT via BLE <<<");
          delay(500);
        } else {
          Serial.println("BLE not connected — code not sent");
          updateDisplay("! BLE not connected");
          delay(1000);
        }
        updateDisplay();
      }
      break;

    case BTN_SLOT:
      if (event == 1) {
        switchToSlot((gSlot + 1) % NUM_SLOTS);   // reboot
      } else if (event == 2) {
        clearSlot();
        updateDisplay();
      }
      break;
  }
}

// ==================== setup / loop ====================
void setup() {
  Serial.begin(115200);
  // Native USB-CDC (C3): wait for the host to bring the port up, max ~2s.
  unsigned long serialWait = millis();
  while (!Serial && (millis() - serialWait < 2000)) delay(10);
  Serial.println("\n=== ESP32 TOTP Device ===");

  // --- Buttons (external 4.7k pull-up) ---
  for (uint8_t i = 0; i < NUM_BTN; ++i) pinMode(gButtons[i].pin, INPUT);

  // --- I2C + OLED ---
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("SSD1306/SSD1315 not found!");
    while (true) delay(100);
  }
  display.ssd1306_command(SSD1306_SETDISPLAYCLOCKDIV);
  display.ssd1306_command(0xF0);
  updateDisplay("Starting...");
  delay(300);

  // --- Load slots from NVS ---
  loadSlotState();

  // --- WiFi as early as possible + blocking time sync BEFORE BLE ---
  updateDisplay("Connecting WiFi..");
  startWifi();
  syncTimeBlocking(6000, 3000);

  // --- BLE for the active slot ---
  updateDisplay("Starting BLE..");
  initBle();

  // --- First code + screen ---
  generateTOTP();
  updateDisplay();
}

void loop() {
  // Buttons
  for (uint8_t i = 0; i < NUM_BTN; ++i) {
    uint8_t ev = pollButton(gButtons[i]);
    if (ev) handleButton(gButtons[i].pin, ev);
  }

  // Time upkeep (first sync + background re-sync)
  pollNtp();

  // Auto-refresh TOTP when the 30-second period rolls over
  if (gTimeSynced) {
    time_t now; time(&now);
    if ((now / 30) != (lastCodeTimestamp / 30)) {
      generateTOTP();
    }
  }

  // Refresh the display once a second (progress bar)
  static uint32_t lastDisplay = 0;
  uint32_t now = millis();
  if (now - lastDisplay > 1000) {
    lastDisplay = now;
    updateDisplay();
  }
  delay(5);
}
