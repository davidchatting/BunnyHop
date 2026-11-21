# Prompt log

Prompts given to Claude Code (Opus 5.5) that changed the code, in order.
Questions, confirmations and documentation-only requests are left out.
Clarifying answers are included where they changed the direction of the work.

---

## 1. Project brief (2026-10-30)

> # Project: ESP32 Nabaztag V1 Bridge
>
> ## Objective
>
> Build a self-contained system that connects a Nabaztag V1 rabbit (including RFID) to an ESP32 and exposes all accessible rabbit inputs and outputs through a local web interface.
>
> The ESP32 acts as a bridge between the rabbit and multiple clients (phones, tablets and computers) connected to its Wi-Fi network.
>
> The web interface must be a modular p5.js project, with HTML, CSS and JavaScript kept in separate files.
>
> The system must work locally without cloud services or the original Nabaztag servers.
>
> ## Available hardware
>
> - A Nabaztag V1, locally available and currently in Wi-Fi pairing mode.
> - An ESP32 connected to the development computer via USB.
> - A USB camera pointed at the rabbit, available for observing and verifying its physical behaviour.
>
> **Critical constraint:** Do not modify, replace or flash the rabbit's firmware. Only change its network configuration and other accessible settings.
>
> ## 1. Discovery and network setup
>
> On first startup, the ESP32 should:
>
> 1. Scan for Wi-Fi networks broadcast by Nabaztag rabbits in pairing mode.
> 2. Identify the locally available rabbit.
> 3. Connect to its temporary network.
> 4. Investigate and use the original provisioning mechanism to configure the rabbit to connect to a new Wi-Fi network.
> 5. Retrieve identifying information from the rabbit where possible.
> 6. Create its own Wi-Fi access point, using a unique SSID derived from the rabbit's identifying information.
>
> The rabbit and multiple web clients should connect to this access point.
>
> On subsequent startups, the ESP32 should restore the network configuration, automatically reconnect to the rabbit and start the server.
>
> If a rabbit does not connect, periodically scan for rabbits in pairing mode.
>
> Investigate the ESP32's Wi-Fi mode limitations and ensure the transition between pairing and access point operation is technically viable.
>
> ## 2. ESP32 server and rabbit communication
>
> The ESP32 should host a web server that provides bidirectional communication between the rabbit and connected clients.
>
> Research the original Nabaztag V1 protocols and existing implementations before developing this layer.
>
> Expose all accessible rabbit inputs and outputs, including where supported:
>
> **Inputs**
> - RFID reader and tag identification.
> - Buttons.
> - Microphone and audio-related events.
> - Other available sensors.
>
> **Outputs**
> - Independent left and right ear positioning.
> - LEDs and lighting.
> - Audio playback.
> - Other available actuators.
>
> Use a consistent API and real-time communication (preferably WebSockets) to expose these capabilities.
>
> Support multiple simultaneous clients, event broadcasting, connection monitoring, reconnection and diagnostic logging.
>
> Do not assume every feature is accessible through the original protocol. Verify and document actual capabilities.
>
> ## 3. Web interface
>
> The ESP32 must serve a responsive p5.js application accessible through a browser without installing anything.
>
> Keep the project modular, with separate files for:
>
> - HTML entry point.
> - CSS and styling.
> - Main p5.js sketch.
> - Rabbit communication/API.
> - Controls and interface components.
> - Incoming events and event handling.
>
> The interface should include:
>
> - Rabbit connection status.
> - A visual representation of the rabbit.
> - Ear controls.
> - LED controls.
> - Audio controls.
> - Live RFID events.
> - Live event log.
> - Manual command interface.
> - Communication debugging tools.
>
> The interface should be designed as a creative-coding environment, not simply a settings panel. Rabbit inputs should be usable to drive p5.js sketches, and sketches should be able to control rabbit outputs.
>
> Keep the rabbit API independent of the p5.js application so other interfaces and projects can use it.
>
> ## 4. Research and implementation
>
> Prioritise existing open-source repositories, libraries, protocol documentation and community reverse-engineering work.
>
> In particular, distinguish between Nabaztag V1 and later hardware revisions.
>
> Before implementation, establish:
> - The actual pairing and provisioning mechanism.
> - The rabbit's communication protocol.
> - Which inputs and outputs are accessible.
> - ESP32 hardware and networking constraints.
> - Any technical blockers.
>
> Work incrementally, implementing and testing the networking and communication before building the complete interface.
>
> Use the USB camera to help verify physical responses to commands.
>
> Distinguish between features that are physically tested, protocol-verified and merely inferred.
>
> Do not invent protocol commands or claim untested functionality works.
>
> ## 5. Deliverables
>
> Produce a complete, organised project containing:
>
> - ESP32 firmware.
> - Modular p5.js web application.
> - Rabbit communication layer and documented API.
> - Automated discovery and initial provisioning.
> - Wi-Fi access point configuration.
> - Real-time bidirectional communication.
> - README with installation, setup, operation, architecture, API documentation and troubleshooting.
> - Known limitations and future development opportunities.
>
> Credit all third-party repositories, libraries, code and research in the README, including links to original sources and relevant licence information.
>
> ## 6. Working approach
>
> Do not stop at an architectural proposal or code skeleton. Implement the project and test it using the available hardware.
>
> Start by inspecting the connected hardware and researching the Nabaztag V1. Present a concise architecture and identify uncertainties before implementation.
>
> Then work through:
>
> 1. Rabbit discovery and provisioning.
> 2. ESP32 access point setup.
> 3. Rabbit communication.
> 4. API implementation.
> 5. p5.js interface.
> 6. End-to-end testing and documentation.
>
> If an assumption proves incorrect, revise the approach rather than building on it.
>
> The goal is a locally operating Nabaztag V1 that can be monitored, controlled from any device connected to the ESP32 network, without modifying the rabbit's firmware.
>
> Keep a log of the prompts in the repo

**Clarifications (Claude's questions, the user's answers):**
- Brief says V1, but RFID/microphone are V2 features and the rabbit was earlier identified as V2 - which? -> "it's a nabaztagtag" (Nabaztag:tag, V2)
- New repo, or continue the earlier BunnyHop experiment? -> "New repo from scratch"

## 2.

> no it should restart automatically with the new details. this is a 2005 implementatiojn of wifi so check the network, names, etc are all within the old specs

> it must be an open network

(Led to: 802.11b-only AP, hyphen-free SSID, and finding that `b.htm` needs `z=Update+and+Start`.)

## 3.

> the camera isn't mirrored, pressing the button now

## 4.

> moved left ear

(Checked against the camera: the rabbit reports ears from Violet's point of view, so incoming left and right were swapped in the firmware.)

## 5.

> go with GPL-2.0-or-later for everything

## 6.

> can tyou test the webinteface?

## 7.

> the web inteface is much too much, sketch interface is unnecesary, commands makes no sense and the interactive rabbit doesn't seem to work

## 8.

> this project and repo should now be called BunnyHop update everytthing

(Renamed from nabaztag-bridge.)

## 9.

> ok, assuming this esp32 can do this, I want to create two wireless networks, a private side that is open but mac address locked to the nabaztag and a public side that hosts the server and joins the local wifi network, if the credits for the public network are unset or the network is unavailable then it will be another self hosted network and captive portal. the webapp will allow the local network credentials to be set. once on the local network it should be discoverable as bunnyhop.local

(The ESP32 can host only one network; asked how the fallback should work.) Answer: "option 1 but the webapp still needs to be accesible, so that we don't have to join the local wifi if we don't want" - i.e. fallback opens the rabbit's network (MAC lock lifted) with a captive portal to the full web app.

> also private means open (no password) but not adevrtised

## 10.

> does the interactive rabbit work with touch events on ipad?

## 11.

> update the hello message to something more fun, also better chime, big ben? also ipad still doesn't seem to work?

> no that message is not fun, just say Hello I'm nabaztag, that'll be fine

## 12.

> works great - but there is no text i the save wifi creditals button?

(Home Wi-Fi confirmed working; the button text was invisible in iPad dark mode.)

## 13.

> you didn't make the esp32 flasher as a git action

(The next prompts developed the rabbit drawing as a separate prototype before it went into the web app.)

## 14.

> can you see the new pdf? can you extract the rabite character and structure it so the ears can be moved like in the different views in the document?

## 15.

> the ears don't move correctly yet, they are constrained to occupy the 90 degree angle point outwards, they can point forward and back as they rotate

## 16.

> just want one control per ear, suggust a simple 0 - 90 animation, where cross hatching is faded in to show depth when going back

## 17.

> also place the LEDS

## 18.

> nice but no hard edges on the leds, also bottom led should be the bottom edge

## 19.

> bottom edge should mask the led, also needs to be wider. use a grey smudge fill instead of crosshatch, so thjat it match es the style of the leds

## 20.

> grey represents depth so at 90 dgrees it'll be back at starting depth. also mask the bottom led the other way

## 21.

> move the bottom led up a bit so it better floows the edge and remove the hard centre. explain how the ear rotation works. far way grey is too dark

## 22.

> the 45 degree ears have some peaks, not smooth curves

## 23.

> shapes are now very good draw ears infront (in terms of z) of the body

## 24.

> right ear at 90 degrees should match the example, not go to the exact angle

## 25.

> slightly highere wit the bottom led

## 26.

> make the nose led slightly lower down

## 27.

> bit more and the chest leds can go down by a unit of their height

## 28.

> put this rabbit in the BunnyHop web app

## 29.

> (Answer to how ear positions 0-16 map onto the drawing:) Full turn, fwd then back

## 30.

> if the rabbit is not connected, use the ghost outline and turn all the leds off

## 31.

> there needs to be a grey outlined circle as a target for each led in the webapp

## 32.

> when ghost no circles as no interaction possible,then show screenshot

## 33.

> ok, instead of using the Nabaztag mac address use the esp32 mac address the same way

## 34.

> we're going to repair the rabbit its ok, buit now the network should be called BunnyHopXXXXXX

## 36.

> no that's not right, if there is no home wifi set (and working via ping of router) then the the only way to reconfigure is via an unhidden wifi network

## 37.

> ok fix everything, also can the webpages redirect to the next place, assuming the wifi networks are changed (say which on screen)
