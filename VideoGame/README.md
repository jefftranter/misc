This is a retro video game design based around the AY-3-8500-1 chip
from General Instrument. This chip was used on many first generation
video game consoles in the 1970s. It can play four different games,
for one or two players, controlled by paddles.

It is still a work in progress - the basic circuit has been
breadboarded but the PCB has not yet been verified as working
and some part values may still need to be adjusted.

Construction Notes

The board is straightforward to assemble using all through-hole
components. Parts can be obtained from sources like Mouser of Digikey.
The AY-3-8500-1 is no longer made but NOS chips are available from
Veswin Electronics. Make sure you select AY-3-8500-1 (NTSC video) or
AY-3-5800 (PAL video) depending on the desired video format for your
region.

If desired you can use the provided PCB design files - mine was
manufactured at low cost by JLCPCB.

The unit can be run as a standalone circuit board or built into a
case. If used standalone, the board has on-board switches for
controlling game operation. If put in a case, external switches can be
used. A header provides all signals needed to run to the external
switches. The game selection would typically be a four position rotary
switch. A power switch should be provided if mounted in a case.

Holes are provided to mount the PCB in a case or use nylon standoffs
for feet.

The paddles are 1 Megohm linear taper potentiometers. They should be
connected via two conductor shielded cables (up to 8 feet long).

It is recommended to use sockets for the ICs.

It can be powered from a 9 volt battery, 6 1.5 volt cells, or any
unregulated source of 8 to 15 volts DC (e.g. wall wart).

Video and audio outputs should be connected to a TV or monitor with
composite video and analog audio input. The design is for NTSC video
but should also work in PAL video mode if the AY-3-8500 chip is used
instead. To work with a TV with RF input an external RF modulator will
be needed.

Using the DIP switch, select one of the four games to play. Select
game options: speed fast/slow, paddle size large/small, ball angle
high/low, and serve mode manual/auto. In auto serve mode the ball is
automatically served; in manual mode you need to press the serve
button.

Switch wiring for external case:

```
Function  Type          Label
--------  -----         -----
Power     SPST toggle   Power: On/Off
Game      4-pos rotary  Tennis, Soccer, Squash, Practice
Angle     SPST toggle   Ball Angle: High/Low
Size      SPST toggle   Paddle Size: Small/Large
Speed     SPST toggle   Speed: Slow/Fast
Serve     SPST toggle   Serve Mode: Manual/Auto
Serve     Momentary     Serve (when in manual mode)
Reset     Momentary     Reset
```

Assembly Notes

Recommended order of assembly is for lowest height components to
highest:

- resistors
- IC sockets
- pushbuttons
- LED
- DIP switches
- headers
- capacitors
- phono jacks
- crystal
- LM317
- standoffs

Observe the correct polarity when installing the LED and electrolytic
capacitor.

Initially leave the three ICs out of their sockets. Verify that with 9
volts or more applied to the power input you see approximately 7 volts
(independent of the input voltage). The power LED should light up.

Insert U3 and verify the 2 MHz clock signal is present using an
oscilloscope or frequency counter.

Install the remaining ICs and check for correct audio and video output
and game operation.

Not using shielded wires for the paddles may cause erratic operation.

R1 can be adjusted in value if audio to too low or high for your
monitor.

Circuit Notes

The circuit is based on the sample design on the AY-3-8500-1
datasheet. Line level audio (approximately 1V RMS) is provided, rather
than an internal speaker, as most televisions or monitors support
audio.

An LM317 regulator provides a constant voltage of approximately 7
volts. The game takes approximately 60 mA when using a 9 volt
battery. The power LED could be omitted to reduce power consumption
slightly.

There is an optional circuit for two "rifle" games, described in the
datasheet. This is not included, but the relevant pins to the game IC
are brought out to header pins in case you want to implement it with
external circuitry.

References

1. https://oshwlab.com/tranter/ball-paddle-video-game
2. AY-3-8500-1 data sheet
3. https://en.wikipedia.org/wiki/AY-3-8500
4. https://en.wikipedia.org/wiki/First_generation_of_video_game_consoles
