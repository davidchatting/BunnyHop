# Prompt log

Prompts given to Claude Code (Opus 5.5) while building this project, in order.
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
- New repo, or continue the earlier BunnyHop experiment? -> "New repo from scratch" (that earlier experiment is now BunnyHop-old; this project later took the BunnyHop name)

## 2.

> try the esp32 now

(The ESP32's USB-serial bridge had failed USB enumeration; it was recovered from software - see docs/TROUBLESHOOTING.md.)

## 3.

> yes clear the unused packages, nabaztag-bridge is fine

(Permission to free disk space by removing unused PlatformIO packages; repo name confirmed.)

## 4.

> it's in setup mode now, lights are blue

## 5.

> no it should restart automatically with the new details. this is a 2005 implementatiojn of wifi so check the network, names, etc are all within the old specs

> it must be an open network

(Led to: 802.11b-only AP, hyphen-free SSID, and finding that `b.htm` needs `z=Update+and+Start`.)

## 6.

> I turned the volume from off to max

(The rabbit's volume wheel had been at zero, which is why audio programs were silent.)

## 7.

> do you have a real-time serial interafce to send messages without recompiling the esp32 code?

(Yes - every API command can be sent as JSON over the USB serial console; see docs/USING.md, "Serial console".)

## 8.

> the camera isn't mirrored, pressing the button now

## 9.

> moved left ear

## 10.

> moved right ear

## 11.

> look for rfid now

> i don't think I have the right type

## 12.

> tell me about GPL-2

## 13.

> go with GPL-2.0-or-later for everything

## 14.

> push it to github as a private repo

## 15.

> can tyou test the webinteface?

## 16.

> the web inteface is much too much, sketch interface is unnecesary, commands makes no sense and the interactive rabbit doesn't seem to work

## 17.

> this project and repo should now be called BunnyHop update everytthing

(Renamed from nabaztag-bridge. The earlier, unrelated BunnyHop experiment had already been renamed BunnyHop-old.)

## 18.

> ok, assuming this esp32 can do this, I want to create two wireless networks, a private side that is open but mac address locked to the nabaztag and a public side that hosts the server and joins the local wifi network, if the credits for the public network are unset or the network is unavailable then it will be another self hosted network and captive portal. the webapp will allow the local network credentials to be set. once on the local network it should be discoverable as bunnyhop.local

(The ESP32 can host only one network; asked how the fallback should work.) Answer: "option 1 but the webapp still needs to be accesible, so that we don't have to join the local wifi if we don't want" - i.e. fallback opens the rabbit's network (MAC lock lifted) with a captive portal to the full web app.

> also private means open (no password) but not adevrtised

## 19.

> does the interactive rabbit work with touch events on ipad?

## 20.

> update the hello message to something more fun, also better chime, big ben? also ipad still doesn't seem to work?

> no that message is not fun, just say Hello I'm nabaztag, that'll be fine

## 21.

> that works well

(Confirmed on a real iPad: tapping lights and dragging ears.)

## 22.

> works great - but there is no text i the save wifi creditals button?

(Home Wi-Fi confirmed working; the button text was invisible in iPad dark mode.)

## 23.

> ok summarise progress and raise any issues, git push and sort github

## 24.

> the chime is fine

## 25.

> you didn't make the esp32 flasher as a git action

(Prompts 26-43 developed the rabbit drawing from `network-actions-rabbits.pdf`, a set of 2018 diagrams, as a separate prototype before it went into the web app.)

## 26.

> can you see the new pdf? can you extract the rabite character and structure it so the ears can be moved like in the different views in the document?

## 27.

> the ears don't move correctly yet, they are constrained to occupy the 90 degree angle point outwards, they can point forward and back as they rotate

## 28.

> just want one control per ear, suggust a simple 0 - 90 animation, where cross hatching is faded in to show depth when going back

## 29.

> also place the LEDS

## 30.

> nice but no hard edges on the leds, also bottom led should be the bottom edge

## 31.

> bottom edge should mask the led, also needs to be wider. use a grey smudge fill instead of crosshatch, so thjat it match es the style of the leds

## 32.

> grey represents depth so at 90 dgrees it'll be back at starting depth. also mask the bottom led the other way

## 33.

> move the bottom led up a bit so it better floows the edge and remove the hard centre. explain how the ear rotation works. far way grey is too dark

## 34.

> the 45 degree ears have some peaks, not smooth curves

## 35.

> shapes are now very good draw ears infront (in terms of z) of the body

## 36.

> right ear at 90 degrees should match the example, not go to the exact angle

## 37.

> slightly highere wit the bottom led

## 38.

> that's good we can work wit this

## 39.

> make the nose led slightly lower down

## 40.

> bit more and the chest leds can go down by a unit of their height

## 41.

> put this rabbit in the BunnyHop web app

## 42.

> (Answer to how ear positions 0-16 map onto the drawing:) Full turn, fwd then back

## 43.

> if the rabbit is not connected, use the ghost outline and turn all the leds off

## 44.

> suggest a new readme structure where the top readme page simply describes the project and ways to use it, authors and licenses there too

## 45.

> David Chatting is the author and illustrated the rabbit, I don't need the pdf in the repo, we don't need to worry about where that comes from

## 46.

> should say waht clude model was used

## 47.

> rabbit image with only a red nose, no other leds

## 48.

> highlevel setup instructions need to include how to put tit on thyour local home network

## 49.

> green led on image

## 50.

> can the svg go in the title text?
