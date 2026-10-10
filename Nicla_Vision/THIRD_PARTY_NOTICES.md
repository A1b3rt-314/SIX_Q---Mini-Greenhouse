# Arduino driver attribution and license notices

These notices cover the four Arduino driver source files distributed in [`third_party/`](third_party/). They identify the upstream code and the local SIX_Q adaptations separately.

The upstream project is [ArduinoCore-mbed, release 4.6.0](https://github.com/arduino/ArduinoCore-mbed/tree/4.6.0). All four files retain the original **Copyright 2021 Arduino SA** notice and are licensed under **GNU Lesser General Public License version 3 or, at your option, any later version (LGPL-3.0-or-later)**.

| Distributed file | Upstream source at release 4.6.0 | Local changes |
|---|---|---|
| [`camera.cpp`](third_party/camera.cpp) | [`libraries/Camera/src/camera.cpp`](https://github.com/arduino/ArduinoCore-mbed/blob/4.6.0/libraries/Camera/src/camera.cpp) | Added `Camera::grabFrameCropped()` and the SIX_Q attribution and change notice. |
| [`camera.h`](third_party/camera.h) | [`libraries/Camera/src/camera.h`](https://github.com/arduino/ArduinoCore-mbed/blob/4.6.0/libraries/Camera/src/camera.h) | Added the declaration of `Camera::grabFrameCropped()`, its usage comment, and the SIX_Q attribution and change notice. |
| [`gc2145.cpp`](third_party/gc2145.cpp) | [`libraries/GC2145/gc2145.cpp`](https://github.com/arduino/ArduinoCore-mbed/blob/4.6.0/libraries/GC2145/gc2145.cpp) | Unmodified copy. |
| [`gc2145.h`](third_party/gc2145.h) | [`libraries/GC2145/gc2145.h`](https://github.com/arduino/ArduinoCore-mbed/blob/4.6.0/libraries/GC2145/gc2145.h) | Unmodified copy. |

## SIX_Q Camera adaptations

The local adaptations are maintained by Alberto Prestes as part of SIX_Q / GerminoLab. The change notices in `camera.cpp` and `camera.h` date the functional changes to **2026-09-25** and the attribution notices to **2026-09-27**.

The added `Camera::grabFrameCropped()` capture path:

- validates the framebuffer, pixel format, resolution, crop geometry, capacity and alignment;
- checks that acquisition is idle before configuring the DCMI crop;
- clears the destination buffer and manages its cache coherency;
- checks DMA startup and waits, with a timeout, for DMA completion and a single frame event;
- checks acquisition errors, records the remaining DMA count before stopping, and reports failure when the capture is invalid.

The corresponding declaration and usage comment were added to `camera.h`. The existing `Camera::grabFrame()` implementation and the other upstream code were retained unchanged. The [SIX_Q Nicla sketch](camera_nicla_V7.ino) uses the added method to capture tiles while keeping the GC2145 sensor output fixed at full resolution.

These changes address intermittent RGB565 artifacts observed in this prototype. They are local project adaptations, not an official Arduino release or endorsement, and do not establish a single defect in the upstream driver as the cause of those artifacts.

## License texts and scope

The complete license texts are included locally:

- [GNU LGPL version 3](LICENSES/LGPL-3.0.txt), copied from [GNU's official text](https://www.gnu.org/licenses/lgpl-3.0.txt).
- [GNU GPL version 3](LICENSES/GPL-3.0.txt), copied from [GNU's official text](https://www.gnu.org/licenses/gpl-3.0.txt).

LGPL version 3 incorporates the terms of GPL version 3 with additional permissions, so both texts are provided. The original source headers allow version 3 or any later version; including these version 3 texts does not remove that option.

The Camera files, including the SIX_Q adaptations, remain under LGPL-3.0-or-later. The unmodified GC2145 files remain under that same license. Their original copyright, license and warranty notices are preserved. The [0BSD license for original SIX_Q work](../LICENSE) does not replace these driver licenses, and including the GPL text here does not relicense the entire repository under the GPL.

Other libraries and platform components obtained through the Arduino toolchain retain their own licenses. This inventory concerns the four driver source files copied into this repository.
