#include "store.h"
#include <Preferences.h>

namespace store {

static Preferences prefs;

void begin() { prefs.begin("nabbridge", false); }
String get(const char *key, const String &def) { return prefs.getString(key, def); }
void set(const char *key, const String &value) { prefs.putString(key, value); }
void remove(const char *key) { prefs.remove(key); }
void clearAll() { prefs.clear(); }

}  // namespace store
