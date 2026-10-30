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
#define AP_SSID_PREFIX "Nabaztag-"   // + last 3 bytes of the rabbit MAC
#define AP_FALLBACK_SSID "Nabaztag-Bridge"
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
