#pragma once
#include <Arduino.h>
#include "packets.h"

// Minimal XMPP server for the original Violet Nabaztag:tag bootcode.
// Mirrors the subset OpenJabNab implements (server/lib/xmpphandler.cpp,
// plugins/system/auth/plugin_auth.cpp): violet:iq:register, SASL
// DIGEST-MD5, bind/unbind/session, violet:iq:sources, presence echo,
// <button>/<ears> messages from the rabbit, <packet> messages to it.
namespace xmpp {

void begin();
void loop();
bool connected();     // a rabbit has authenticated and bound a resource
String resource();    // current resource ("boot", "idle", "asleep", "streaming"...)
bool sendPacket(const packets::Bytes &trame);
void disconnect();
bool sendRaw(const String &xml);            // debugging: any stanza to the rabbit
bool queryRabbit(const char *node);         // ad-hoc command: "getrunningstate" | "getconfig"


}  // namespace xmpp
