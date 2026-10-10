🌱 SIX_Q — Reproducible Arduino UNO Q Mini Greenhouse

> **Experimental prototype — not for food production.** SIX_Q is intended for supervised experimentation. Do not use this version to grow food intended for human consumption. Materials in contact with water, substrate and plants must be evaluated before any food-growing use.

A small, automated growing system built around an Arduino UNO Q board and designed to be reproduced, modified, and extended. The goal of this project is to provide a simple, working prototype that others can use as a starting point to build and experiment with their own systems. It was made using inexpensive and readily available materials, so you have no excuses! Take the best ideas, build with whatever you have on hand, and improve them wherever you are.

**Network access:** use SIX_Q only on a trusted private local network with access restricted to trusted users and devices. The application has no dashboard user authentication; clients that can reach the dashboard can request irrigation, lighting, captures and paid AI analysis when configured. Do not expose the dashboard or camera TCP port directly to the Internet, through router port forwarding or public tunnels. See [network access and security](DASHBOARD_AND_FUNCTIONS.md#network-access-and-security).

## Story

Hello everyone!
My name is Alberto, and I would like to share an idea that started as a hobby a few years ago. I am currently working on small growing systems that can operate autonomously, follow programmed routines, or be controlled remotely, as well as monitoring systems for growing environments. With this repository, I want to provide the information needed to build a similar system at home. There are several parts and steps involved, but the process ultimately comes down to patience, trial and error, and perseverance. Let’s get building, fellow enthusiasts!
Adapt these ideas and create your own version with whatever you have available.


## System Structure
SIX_Q is organized into three main levels.

1. Base
The frame can expand both horizontally and vertically. Once adjusted, it is secured using the side bars.
The growing bag is then inserted and secured inside the structure. This prototype uses approximately 2.5 kg of commercial potting soil.

2. Middle Level
This level contains the lighting and irrigation systems. For safety, the submersible pump and water reservoir are placed to one side, outside the main frame.

3. Electronics
The electronics are located at the top of the structure.
They include:

* a push button for local irrigation control;
* a switch for local lighting control;
* an Arduino UNO Q;
* environmental sensors and associated modules;
* a Nicla Vision board for image capture and visual monitoring.


## Manual and scheduled control

PROGRAM mode for irrigation and lighting is implemented and operational.

WATER NOW requests a 5-second pulse without cancelling PROGRAM. Manual and scheduled irrigation share four accepted pulses per local calendar day, with the counter resetting at 00:00. PROGRAM uses fixed local times: 4h at 00/04/08/12/16/20, 6h at 00/06/12/18, 8h at 00/08/16, 12h at 00/12, and 24h at 12:00. Selection waits for the next scheduled time.

LIGHT NOW ON/OFF cancels PROGRAM and has no daily runtime limit. LIGHT PROGRAM follows the 06:00–12:00, 06:00–14:00, 06:00–18:00 or 06:00–00:00 (18 hours) window using the local time configured in the UNO Q application. Arduino controls the relays; programs and irrigation quota counters reset on MCU restart.

See [Dashboard and functions](DASHBOARD_AND_FUNCTIONS.md#program-and-now-rules) or [Panel y funciones](PANEL_Y_FUNCIONES.md#reglas-de-program-y-now) for the full rules.

The UNO Q application uses `America/Montevideo` as its local timezone. For another location, set the `SIXQ_TIMEZONE` environment variable to an IANA timezone. The dashboard clock, last-capture timestamp and lighting windows all use this zone, independently of the browser or container default timezone. Internal records retain UTC. The 24h irrigation program runs at 12:00 noon and shares the daily quota. This time is chosen by configuration; temperature does not enable or block irrigation.

## About this project
SIX_Q is part of GerminoLab, an initiative focused on developing intelligent greenhouse monitoring systems, such as SEVEN, and small home growing-control systems, such as SIX. This version of SIX_Q is open source and includes the documentation required to assemble, build, modify, and extend the system.

The project was designed and developed with the assistance of artificial intelligence tools as part of the Hackster.io 2026 competition. The original code created specifically for this project is released under the [0BSD License](LICENSE).

The STL models, Autodesk Fusion files and part drawings in PDF format in [design/](design/) were created by Alberto Prestes and are released under the same [0BSD License](LICENSE). The license applies to these original design files as well as the original project code, permitting use, copying, modification and redistribution for any purpose, with or without fee.

Some components depend on third-party software, firmware, libraries, drivers, or development tools. These components remain subject to their respective licenses and terms. Where applicable, their dependencies and installation requirements are identified in the relevant documentation. Built to be reproduced, modified, and extended.

The bundled Arduino Camera and GC2145 drivers retain their LGPL-3.0-or-later license. Their upstream sources, local modifications and complete license texts are documented in [Arduino driver attribution and license notices](Nicla_Vision/THIRD_PARTY_NOTICES.md).


Documentation

How is it built?

- BUILD_GUIDE.md
- GUIA_DE_ENSAMBLAJE.md
- CIRCUIT_DIAGRAM.pdf
- Design/ — Technical drawings, Autodesk Fusion 360 models, mechanical design files, and STL files for 3D printing.

How does it work?

- DASHBOARD_AND_FUNCTIONS.md
- PANEL_Y_FUNCIONES.md


⸻

Alberto Prestes
admin@germinolab.com
