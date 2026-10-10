
Before programming, the firmware must be updated. I used the Arduino IDE on macOS for this step. Then, for programming, I used the Arduino Web Editor.

You must also include the `camera.cpp`, `camera.h`, `gc2145.cpp` and `gc2145.h` source files located in the `third_party` folder. These drivers originate from Arduino and retain their original copyright and LGPL notices. The local Camera adaptations are described below.

## Camera driver adaptations in SIX_Q V7

The local [`camera.h`](third_party/camera.h) and [`camera.cpp`](third_party/camera.cpp) are based on the Camera library from **ArduinoCore-mbed 4.6.0**. They were adapted for SIX_Q V7 on **2026-09-25** to address intermittent RGB565 image corruption, most frequently observed in tiles beginning at X=640.

- **`camera.h`:** adds the declaration of `Camera::grabFrameCropped()`.
- **`camera.cpp`:** implements this additional capture path. It validates the buffer and crop geometry, configures DCMI cropping while acquisition is idle, checks DMA startup, and waits with a timeout for DMA completion and the frame event before stopping acquisition. It also records the remaining DMA count before stopping and handles framebuffer cache coherency.

The original `Camera::grabFrame()` remains unchanged. The [Nicla V7 sketch](camera_nicla_V7.ino) uses the added method to capture tiles while keeping the GC2145 sensor output fixed at full resolution. This avoids repeatedly moving the sensor's output window for each tile.

The operator reported consecutive captures without visible corruption after installing this V7 implementation. This supports the combined capture changes, but does not establish that a single defect in the original Arduino library caused all previous artifacts. These are local project adaptations, not an official Arduino release or endorsement.

## Copyright and license

The original driver sources retain **Copyright 2021 Arduino SA**. As stated in their headers, `camera.h` and `camera.cpp`, including the SIX_Q adaptations, and the unmodified `gc2145.h` and `gc2145.cpp` remain under the **GNU Lesser General Public License, version 3 or any later version (LGPL-3.0-or-later)**. The project's 0BSD license does not replace their LGPL license.

The original copyright and license notices are preserved in all four files. The two Camera files also contain a description and dates of the local changes. SIX_Q is maintained by Alberto Prestes; this attribution does not replace Arduino's original authorship. This README supplements the notices in the source files.

See [Third-party notices](THIRD_PARTY_NOTICES.md) for the exact upstream sources and the changes to each file. Complete copies of [LGPL version 3](LICENSES/LGPL-3.0.txt) and [GPL version 3](LICENSES/GPL-3.0.txt) are included for offline reference.

References: [original Arduino camera.cpp](https://github.com/arduino/ArduinoCore-mbed/blob/4.6.0/libraries/Camera/src/camera.cpp), [original Arduino camera.h](https://github.com/arduino/ArduinoCore-mbed/blob/4.6.0/libraries/Camera/src/camera.h), and [GNU LGPL v3](https://www.gnu.org/licenses/lgpl-3.0.html).
