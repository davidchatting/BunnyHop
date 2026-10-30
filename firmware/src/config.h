// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Network plan
//   ESP32 soft-AP 192.168.4.1/24 - the rabbit and every web client join it.
//   During first-time pairing the ESP32 instead joins the rabbit's own
//   setup network ("NabaztagXX", rabbit at 192.168.0.1, no DHCP).
#define AP_IP_A 192
#define AP_IP_B 168
#define AP_IP_C 4
#define AP_IP_D 1
#define AP_IP_STR "192.168.4.1"
// Plain alphanumeric SSIDs: the rabbit's 2005-era Wi-Fi stack is fussy.
#define AP_SSID_PREFIX "Nabaztag"    // + last 3 bytes of the rabbit MAC, e.g. Nabaztag82DE00
#define AP_FALLBACK_SSID "BunnyHop"  // before a rabbit is paired
#define AP_CHANNEL 1
#define AP_MAX_CLIENTS 8

#define RABBIT_SETUP_SSID_PREFIX "Nabaztag"   // setup-mode SSID is "Nabaztag" + 2 hex digits
#define RABBIT_SETUP_IP "192.168.0.1"

// What we tell the rabbit its "Violet Platform" is. The rabbit's loader
// fetches http://<platform>/bc.jsp, the bootcode then fetches
// http://<platform>/locate.jsp.
#define RABBIT_PLATFORM AP_IP_STR "/vl"

// XMPP: the Violet bootcode connects directly when xmpp_domain is "ip:port".
#define XMPP_DOMAIN AP_IP_STR
#define XMPP_PORT 5222

// If the rabbit hasn't connected after this long, start looking for a
// rabbit in setup mode again (and keep looking at this interval).
#define RABBIT_RESCAN_AFTER_MS (3UL * 60UL * 1000UL)
#define RABBIT_RESCAN_INTERVAL_MS (60UL * 1000UL)

// Ears: positions 0..16 as used by Violet ambient packets
#define EAR_MAX 16

// Stale-session recovery: a rabbit that stays associated without talking
// for STALE_RABBIT_MS gets the AP switched off for KICK_AP_OFF_MS, which
// makes it reboot (verified: 30 s off -> fresh bc.jsp ~13 s after AP returns).
#define STALE_RABBIT_MS (30UL * 1000UL)
#define KICK_AP_OFF_MS (30UL * 1000UL)
#define KICK_INTERVAL_MS (120UL * 1000UL)

// How long LED colours set via the API are held (seconds). The rabbit can
// only light LEDs during a choreography; see rabbit::setLeds.
#define LED_HOLD_S 3600

// Choreography files are served on their own port (see chor_server.h)
#define CHOR_PORT 8080

// A choreography counts as "in flight" until the rabbit has fetched it, or
// this long at most (see rabbit::chor)
#define CHOR_FLIGHT_TIMEOUT_MS 3000
// Minimum time a delivered chor is left running before a queued one replaces
// it. The rabbit blanks the LEDs for ~0.7 s while it downloads each new chor,
// so faster updates would show mostly dark LEDs.
#define CHOR_SETTLE_MS 1500
