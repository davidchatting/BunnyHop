// SPDX-License-Identifier: GPL-2.0-or-later
#include "store.h"
#include <Preferences.h>

namespace store {

static Preferences prefs;

// Namespace kept from the project's first name (nabaztag-bridge) so existing
// pairings survive the rename to BunnyHop.
void begin() { prefs.begin("nabbridge", false); }
String get(const char *key, const String &def) { return prefs.getString(key, def); }
void set(const char *key, const String &value) { prefs.putString(key, value); }
void remove(const char *key) { prefs.remove(key); }
void clearAll() { prefs.clear(); }

}  // namespace store
