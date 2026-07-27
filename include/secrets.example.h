// =============================================================================
//  secrets.example.h  —  template for private configuration
// =============================================================================
//  Copy this file to secrets.h and fill in your own values:
//      cp include/secrets.example.h include/secrets.h
//  secrets.h is gitignored, so your real credentials stay out of the repo.
// =============================================================================
#pragma once

// ==================== WiFi ====================
#define WIFI_SSID  "your-wifi-ssid"
#define WIFI_PASS  "your-wifi-password"

// ==================== Web UI Credentials ====================
#define WEB_PASSWORD "admin"

// ==================== TOTP accounts ====================
// Secrets are Base32 (as exported by Google Authenticator).
struct TotpAccount {
  const char* name;        // display name
  const char* secret_b32;  // Base32 secret
};

static const TotpAccount ACCOUNTS[] = {
  {"Example", "JBSWY3DPEHPK3PXP"},
};
