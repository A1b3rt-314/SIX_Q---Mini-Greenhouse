# Assembly Guide — SIX_Q Mini Greenhouse

> **Experimental prototype — not for food production.** SIX_Q is intended for supervised experimentation. Do not use this version to grow food intended for human consumption. Materials in contact with water, substrate and plants must be evaluated before any food-growing use.

**Network access:** use only on a trusted private local network with restricted access. The dashboard has no user authentication and must not be exposed directly to the Internet. Read the [network access and security notes](DASHBOARD_AND_FUNCTIONS.md#network-access-and-security) before enabling network access.

When selecting the materials, I tried to keep everything extremely inexpensive and easy to obtain, so there would be as few excuses as possible not to reproduce, modify, and improve the project.

## Hardware

- Arduino UNO Q
- Arduino 4 Relay Shield
- AHT20 sensor
- Nicla Vision
- LM2596 DC-DC converter
- AC/DC adapter, ALIOV (input: AC100V–240V, 50/60Hz; output: DC12V, 10A, 120W), model ALT-1210
- TOPAI 12V cool-white COB LED strip, 6000K, 16.4 ft, dotless IP65 waterproof, 480 LEDs/m, CRI >90, 4000 lm, cuttable, 0.315 in wide, self-adhesive. Approximately 120 cm were used.
- 5V submersible water pump
- NKK BB-25 pushbutton and NKK A-12 switch
- Two sets of Molex plastic connectors, six circuits, 14–20 AWG, 2.13 mm diameter, keyed locking, MLX type



- Small wooden pieces, mainly: six 300 × 20 × 5 mm pieces, four 200 × 20 × 5 mm pieces, and two 175 × 20 × 5 mm pieces
- Two 10 mm-diameter, 175 mm-long aluminum tubes for the base and frame of the lower section
- Two 8 mm-diameter, 180 mm-long aluminum tubes for the base and frame of the lower section
- Four 10 mm-diameter, 240 mm-long aluminum tubes for the lighting and irrigation support in the middle section
- One 10 mm-diameter, 300 mm-long aluminum tube to distribute irrigation water in the middle section, with small holes every 50 mm along its middle portion
- 1200 mm-long, 7 mm-diameter PVC hose
- 30 mm and 40 mm screws
- Fischer wall plugs, sizes 8 and 6
- Two 3 mm-diameter screws with washers and nuts
- Transparent nylon bags, 45 × 67 cm
- Ten colored-tie garbage bags, used to obtain the plastic ties
- Small screws for mounting the electronics: M2 × 8 and M2.5 × 6
- 2.5 kg of commercial horticultural soil mix
- A 6-liter container of drinking water

## Software

- Arduino App Lab, to update the firmware and upload code to the Arduino UNO Q
- Arduino IDE, to update the Nicla Vision firmware
- Arduino Web Editor, to upload code to the Nicla Vision
- Arduino Cloud Agent, for macOS
- Autodesk Fusion, to generate 3D-printing models and files

The assembly is divided into three sections: the lower section (frame and substrate), the middle section (lighting and irrigation), and the upper section (electronics).

![052.webp](images/052.webp)

## Lower section

This section contains the main support structure and the bag that holds the growing substrate.

![025.jpg](images/025.jpg)
![001.jpg](images/001.jpg)

As shown in the images, the base is made up of:

- Four wooden pieces used as pillars, 300 × 20 × 5 mm each (3D-printable file: [Pilar_x4.stl](design/Pilar_x4.stl)). Each one has a 3 mm hole at the top and bottom, as well as three pairs of slots between 1 and 2 mm wide for fastening cords, nylon ties, or cable ties. The lower slot pair secures the substrate bag, the middle pair secures the lighting and irrigation system, and the upper pair secures the electronics.

![054.png](images/054.png)

- Two 300 × 20 × 5 mm wooden pieces with two 10 × 20 mm side slots so they can be fixed transversely (3D-printable file: [Barra_lateral.stl](design/Barra_lateral.stl)).

![015.png](images/015.png)

- Four 200 × 20 × 5 mm wooden pieces, with 3 mm holes at both ends and two small 45-degree cuts at one end (3D-printable file: [Base_x4.stl](design/Base_x4.stl)).

![043.png](images/043.png)

- 30 mm and 40 mm screws, with the corresponding Fischer no. 6 and no. 8 wall plugs.
- Two 10 mm-diameter, 175 mm-long aluminum tubes, plus two 8 mm-diameter, 180 mm-long aluminum tubes.
- Two 3 mm-diameter screws with nuts and washers.

The goal is to create a structure that can fold and be adjusted along its horizontal axis.

![020.jpg](images/020.jpg)
![030.jpg](images/030.jpg)

A nylon strip under the substrate bag helps preserve the structure's total length and keeps both ends under tension.

![006.jpg](images/006.jpg)
![008.jpg](images/008.jpg)

To contain the substrate, I used a transparent nylon bag approximately 45 cm long and 67 cm high, together with a plastic strip from a 55 × 65 cm garbage bag.

Place the garbage-bag strip around the outside of the transparent bag's upper edge. Then roll the edge outward and downward, together with the strip, until the container reaches an approximate height of 30 cm.

![055.jpg](images/055.jpg)
![003.jpg](images/003.jpg)
![013.jpg](images/013.jpg)
![059.jpg](images/059.jpg)

Next, position the bag as a rectangle, keeping its long axis aligned. Using scissors and taking care not to cut the colored strip, make an opening at each of the four corners to pull the strip through.

![009.jpg](images/009.jpg)

![016.jpg](images/016.jpg)
![032.jpg](images/032.jpg)

These four evenly distributed cuts create a rectangular structure. The strips are passed through the openings and used to fasten the bag to the lower slots in the side pillars.

![014.jpg](images/014.jpg)

![021.jpg](images/021.jpg)

![028.jpg](images/028.jpg)
![039.jpg](images/039.jpg)
![040.jpg](images/040.jpg)

![042.jpg](images/042.jpg)

The structure is then tensioned by installing the side wooden bars at the front and back.

![041.jpg](images/041.jpg)
![004.jpg](images/004.jpg)

The growing substrate consists of 2.5 kg of commercial horticultural soil mix. For plant protection, shape it like a raised bed: make a higher ridge along the long axis of the bag and keep the sides lower so excess water collects away from the plant.

## Middle section

This section contains the lighting and irrigation systems, also attached to the main frame with nylon ties.

![044.jpg](images/044.jpg)
![022.jpg](images/022.jpg)

This section consists of:

- Two 175 × 20 × 5 mm wooden boards, each with four 3 mm holes and one 10 mm hole for irrigation. Starting from the ends and working toward the center, place one hole 10 mm from the end and 5 mm from the upper edge. Place the second hole 20 mm from the end and 5 mm from the upper edge. Repeat the same pattern at both ends. On one side, leave a 10 mm hole 20 mm beyond the last hole for irrigation (3D-printable file: [Soporte_luz_y_riego_x2.stl](design/Soporte_luz_y_riego_x2.stl)).

![017.png](images/017.png)

- Four 10 mm-diameter, 240 mm-long aluminum tubes, four Fischer no. 8 wall plugs, and eight 40 mm screws.
- The irrigation line is made from 7 mm-diameter PVC tubing and a 10 mm-diameter, 300 mm-long aluminum tube with small slots every 50 mm along its middle portion to distribute the water. A small clamp with a long screw indicates and allows adjustment of the direction of the irrigation water.

![011.jpg](images/011.jpg)

- Four nylon ties, one at each corner, to secure this section to the main side pillars, following the same principle used to attach the substrate bag.

![056.jpg](images/056.jpg)
![036.jpg](images/036.jpg)

Once the structure is assembled, install a 1170 mm waterproof LED strip along the lower edge. Secure it with small plastic cable ties.

![047.jpg](images/047.jpg)
![034.jpg](images/034.jpg)

The ends of the LED strip must be properly insulated, for example with electrical tape. Remove the protective backing from the adhesive and install the strip facing downward, toward the growing substrate.

## Upper section — Electronics

This section contains the system's main electronic components.

I used an Arduino UNO Q, an Arduino 4 Relay Shield, and an AHT20 temperature and humidity sensor connected through I²C using the Qwiic port.

![027.jpg](images/027.jpg)
![061.jpg](images/061.jpg)

I also used an NKK A-12 switch and an NKK BB-25 pushbutton. I have had good experiences with this brand; I would describe its switches as robust, precise, and minimalist. In embedded systems, I usually choose dual-circuit pushbuttons so that the same mechanical event can report a given state to two boards.

![057.jpg](images/057.jpg)

I used an Arduino Nicla Vision. The physical controls were mounted on a perforated prototyping board.

![002.jpg](images/002.jpg)
![018.jpg](images/018.jpg)
![058.jpg](images/058.jpg)
![007.jpg](images/007.jpg)

When joining the different electronic modules, I encountered some problems while trying to 3D-print a custom support piece.

For the project submission, I used a 2 mm-thick perforated sheet of Swedish cardboard as a rapid prototype and joined the different sections with screws. Some parts, such as the LM2596 DC-DC converter, were secured with plastic cable ties.

I later made a correctly dimensioned support for the electronic components, available in two thicknesses: 2 mm and 2.5 mm (3D-printable files: [Soporte_electronica_2mm.stl](design/Soporte_electronica_2mm.stl) and [Soporte_electronica_2.5mm.stl](design/Soporte_electronica_2.5mm.stl)).

![049.jpg](images/049.jpg)
![012.jpg](images/012.jpg)
![035.jpg](images/035.jpg)
![038.jpg](images/038.jpg)
![046.jpg](images/046.jpg)
![026.jpg](images/026.jpg)
![023.jpg](images/023.jpg)
![045.jpg](images/045.jpg)

![050.jpg](images/050.jpg)
![005.jpg](images/005.jpg)

A second version of the 3D-printed electronics mounting support is now available, with thicknesses of 2 mm and 2.5 mm.

![072.jpg](images/072.jpg)

The complete electronic assembly is suspended by four longer nylon ties, using the same method as for the other sections.

![062.jpg](images/062.jpg)

## Irrigation system

This system deserves a separate section. For safety, the water container is kept outside the system. It should contain a moderate amount of water, because an accidental, indefinitely activated pump must not endanger the crop.

The irrigation system uses a small 5V submersible water pump placed inside a standard 6-liter container. The container was later cut down, reducing its capacity to less than half.

![024.jpg](images/024.jpg)
![053.jpg](images/053.jpg)

Water travels from the reservoir to the growing area through 7 mm-diameter PVC tubing. This connects to the 10 mm-diameter, 300 mm-long aluminum tube mounted on the lighting support in the middle section. Holes along the middle portion of the aluminum tube distribute the irrigation water.

The tube has a screw that indicates the direction of the slots.

![010.jpg](images/010.jpg)

During the first tests, I configured each irrigation cycle to last 30 seconds. However, once the final pump was connected, that duration proved excessive: in approximately 30 seconds, the pump delivered more than 700 mL of water, far too much for the size of the growing container. I had to add more substrate to compensate for the excess water; that substrate is included in the final 2.5 kg total.

For this reason, I reduced the duration of each irrigation cycle from 30 to 5 seconds. During these tests, the substrate received 700 mL on day 1, a 5-second irrigation cycle at day 6, and then a 5-second cycle every 3 days on average.

With the current configuration, each accepted irrigation request works as a discrete event:

```text
1 IRRIGATION request
        ↓
     Pump on
        ↓
    5 seconds
        ↓
    Pump off
```

Both the physical IRRIGATION button and the web panel request the same irrigation cycle.

The button does not work as a “hold to water” control. A short press is enough to request a complete cycle, and releasing the button does not stop the pump before the 5-second interval has elapsed.

To prevent accidental overwatering, the MCU also rejects new IRRIGATION requests while a cycle is already active. This prevents repeated presses from restarting or extending the current cycle.

In the prototype, the system is limited to four accepted irrigation cycles per local calendar day, with the counter resetting at 00:00. The physical pushbutton, NOW and PROGRAM share this quota.

That means:

- 1 cycle = 5 seconds
- Maximum = 4 cycles
- Maximum pump runtime = 20 seconds / 24 h

The exact volume delivered during those 5 seconds depends on the pump, tube length, reservoir level, and installation height. Therefore, the irrigation time must be recalibrated if any of these elements change. The current estimate is 120 mL in 5 seconds.

The important point is that irrigation is treated as a limited, repeatable event rather than leaving the pump under continuous manual control.

This also follows the general SIX_Q control principle:

```text
Physical button / Web panel
            ↓
     IRRIGATION request
            ↓
          UNO Q
            ↓
      Rule evaluation
            ↓
          Relay R2
            ↓
       5V water pump
```

The interface requests the action, but the UNO Q remains responsible for deciding when the pump may run and when it must stop.

## Manual and scheduled control

PROGRAM mode for irrigation and lighting is implemented and operational.

- WATER PROGRAM: a 5-second pulse at fixed local times. Selection waits for the next scheduled time; it does not start an elapsed-time interval.
- WATER NOW: requests a single pulse without cancelling or shifting PROGRAM, subject to the shared quota. Requests during an active pulse are rejected; blocked scheduled slots are skipped, never queued.
- WATER OFF: cancels PROGRAM and lets an accepted pulse finish.
- LIGHT PROGRAM: windows of 06:00–12:00, 06:00–14:00, 06:00–18:00 or 06:00–00:00 (18 hours) using the local time configured in the UNO Q application.
- LIGHT NOW ON/OFF: cancels PROGRAM, including when using the physical control. Manual lighting can remain on for 24 hours, with no daily quota.

Local irrigation times (5-second pulses, subject to the daily quota):

| Program | Times |
|---|---|
| 4h | 00:00 · 04:00 · 08:00 · 12:00 · 16:00 · 20:00 |
| 6h | 00:00 · 06:00 · 12:00 · 18:00 |
| 8h | 00:00 · 08:00 · 16:00 |
| 12h | 00:00 · 12:00 |
| 24h | 12:00 |

The 4h program offers six slots, but this configuration permits four accepted irrigation pulses in total. NOW also consumes quota. Blocked slots are skipped, never queued. The counter resets at midnight even if a pulse is active: that pulse belongs to the day it was accepted. Backwards clock corrections do not replay slots or grant quota again. Normal scheduling latency within the first minute is allowed; earlier slots are not recovered.

Arduino decides when to activate the relays. Programs continue with the browser closed; restarting the MCU resets programs and the quota. Full rules are available in [Dashboard and functions](DASHBOARD_AND_FUNCTIONS.md#program-and-now-rules).

The UNO Q application uses `America/Montevideo` as its local timezone. For another location, set the `SIXQ_TIMEZONE` environment variable to an IANA timezone. The dashboard clock, last-capture timestamp and lighting windows all use this zone, independently of the browser or container default timezone. Internal records retain UTC. The 24h irrigation program runs at 12:00 noon and shares the daily quota. This time is chosen by configuration; temperature does not enable or block irrigation.

## Arduino App Lab setup

For the UNO Q application, I used Arduino App Lab 0.10.0 for macOS. First, I configured the local Wi-Fi connection over SSH from the Mac. Then I loaded the application and enabled it in DEFAULT mode.

I selected the WebUI - HTML and Database - SQL Bricks and added the SparkFun Qwiic Humidity AHT20 1.0.3 library for the temperature and humidity sensor.

![071.jpg](images/071.jpg)

For the AI function, I initially tried creating a custom Brick but eventually chose to integrate the Python (`.py`) code into the main application.

**Configuration files:** each board has its own secrets file. The Nicla Vision project, uploaded through Arduino Web Editor, uses `secrets.h` alongside the `.ino` sketch and the camera's `.cpp` and `.h` files.

For the UNO Q application, copy `secrets_auth.example.py` as `secrets_auth.py` inside the application's `python` folder in Arduino App Lab. Set `AUTH_SHARED_KEY` to the same value used in the Nicla Vision's `secrets.h` file.

If you plan to use AI analysis, also fill in `OPENAI_API_KEY`; otherwise, leave it as an empty string (`OPENAI_API_KEY = ""`). The `secrets_auth.py` file must exist for the application to start, even if you do not use AI. Do not publish your keys on GitHub.

The name `secrets_auth.py` avoids a conflict with Python's standard `secrets` module. When I tried using the name `secrets.py`, Arduino App Lab reported an error and prevented the app from loading. Be sure to keep the filename shown above.
