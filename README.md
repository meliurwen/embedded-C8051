# Embedded C8051

Embedded System's course project with a 8-bit `C8051F020-DK` board connected to
a custom board via I²C protocol.

## Requirements

> **Notice:** Unfortunately the compiler is proprietary and the free limited
> version is walled behind a registration form, so good luck getting it.

* **Silicon Labs IDE**
* **Keil® PK51 Developer’s Kit** - contains the 8051's C compliler, linker and
  assembler.
* `C8051F020-DK` board
* Custom board built in-house at UniMiB, connected to the `C0` port via I²C
  protocol, with the following components:
  * `MMA7660` accelerometer
  * `LM76` thermometer
  * `MCCOG21605B6` display

## Functionalities

* Inclination sampling on the 3 axis form the accelerometer every 100ms
  * Last 8 samples stored in a buffer
* Temperature sampling every 1000ms
* Display udpated every 300ms showing:
  * 3-axis inclination (signals smoothed calculating the mean of the buffered
    samples)
  * Temperature
* Depending on how the `P3.7` button is pressed it is possible to:
  * Quick press: display backlight on/off
  * Long press (more than 1s): backlight regulation mode; the display
    brightness starts to "breathe" until the pressure on the button is released

## Sources

* [Synlab Software Toolchain](https://www.silabs.com/developers/8-bit-8051-microcontroller-software-studio)
* [C8051F02x Datasheet (archived)](https://web.archive.org/web/20190418015658/https://www.silabs.com/documents/public/data-sheets/C8051F02x.pdf)
* [C8051F02x Development Kit Datasheet (archived)](https://web.archive.org/web/20170301082031/https://www.silabs.com/documents/public/user-guides/C8051F02x-DK.pdf)
