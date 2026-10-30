// ============================================================================
// relocate_url.h  —  which server addresses a board may be MOVED to remotely
// ----------------------------------------------------------------------------
// Pure, dependency-free (Covio D-315). The config poll may name a new home for
// a board that was registered under one company and now belongs to another
// (Wire Line 1's Machine Station board, registered under covio-demo-factory
// on 26 Oct 2026, belongs to miki-wire). Changing `server_url` used to need a
// USB console; this lets covio.in say it, and ONLY this:
//
//     https://covio.in/fleet/<company slug>
//
// A slug is 1-64 of [a-z0-9-], not starting or ending with '-'. Anything else
// -- another host, http, a path, a query, a trailing slash -- is refused and
// the board keeps the address it has. The board's identity does not move with
// it: the key is the same, and the new company must already hold it (covio.in
// moves the key when it sees the board arrive), so a forged address can at
// worst make a board's pushes be refused, never deliver them elsewhere.
// ============================================================================
#pragma once
#include <string.h>

static const char kRelocatePrefix[] = "https://covio.in/fleet/";

inline bool isAllowedRelocateUrl(const char* url) {
  if (!url) return false;
  const size_t prefix = sizeof(kRelocatePrefix) - 1;
  if (strncmp(url, kRelocatePrefix, prefix) != 0) return false;
  const char* slug = url + prefix;
  size_t n = strlen(slug);
  if (n == 0 || n > 64) return false;
  if (slug[0] == '-' || slug[n - 1] == '-') return false;
  for (size_t i = 0; i < n; i++) {
    char c = slug[i];
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) return false;
  }
  return true;
}
