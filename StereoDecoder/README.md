This is a FM stereo multiplex decoder design based around the Motorola
MC1310 and the equivalent National Semiconductor LM1310. It can be
used to add stereo to older mono FM tuners that have a multiplex
output. The design is based on the circuit shown in the datasheet for
the decoder IC.

# Construction

It is simple enough that it can be built on a solderless breadboard or
proto board. The MC1310/LML1310 is no longer being manufactured, but
is still available from many sources as New Old Stock (NOS).

It accepts a multiplex output from the FM tuner. Note that you can't
use the standard audio output of a tuner as it will not contain the 19
kHz and 38 kHz components needed to decode the stereo signal. The
decoder outputs approximately line-level audio for both right and left
channels. An LED indicates when a stereo signal is being decoded. If
desired, you can add a switch to allow selecting mono output. It can
be powered from 8 to 14 Volts DC. I used a 9 Volt battery. Current
draw is only about 15 mA.

The IC uses a phase-locked loop that needs to lock onto the 19 kHz
pilot tone from the multiplex signal. You need to adjust the trimpot
in the circuit so that, with no input, the PLL VCO free-runs near
19 kHz. Ideally you should do this with a frequency counter, but the
adjustment is not critical and if you don't have access to a counter
you can adjust it using an on-air stereo signal, setting the pot to
the middle of the range where the stereo LED comes on.

# Parts List

```
R1  2.2K
R2  1K
R3  15K
R4  4.7K trimpot (used 5K)
R5  3.9K
R6  3.9K
C1  220nF (0.22uF) "224"
C2  470nF (0.47uF) "474"
C3  470pF "471"
C4  47nF (.047uF) "473"
C5  220nF (0.22uF) "224"
C6  47uF 25V tantalum or electrolytic
C7  0.1uF "104"
C8  22nF (.022uF) "223"
C9  22nF (.022uF) "223"
C10 4.7uF 25V tantalum or electrolytic
C11 4.7uF 25V tantalum or electrolytic
C12 10uF 25V tantalum or electrolytic
D1  LED
IC1 LM1310/MC1310P IC
PB1 Mono switch pusbutton (optional)
-   9V battery clip
```

# Use With Other Tuners

While my FM tuner had a multiplex output, an adaptor can be added to
most old mono FM tuners without an output, provided that the tuner can
output the full, raw composite (multiplex) baseband signal required
for stereo decoding.

The most critical constraint is that the signal must be tapped
directly from the FM detector before it passes through the tuner's
internal de-emphasis network. A standard mono audio output filters out
frequencies above 15 kHz. However, stereo broadcasting relies on a 19
kHz pilot tone and a 38 kHz subcarrier. If the signal passes through a
mono de-emphasis circuit, these high-frequency subcarriers are no
longer present.

The tuner’s IF stages must also be wide enough to preserve modulation
up to approximately 53 kHz. Most high-quality mono tuners from the late
50s have plenty of bandwidth, but very early or cheap table radios
might have narrow IF bandwidth, resulting in poor stereo separation or
distortion.

Finally, stereo decoding inherently degrades the signal-to-noise
ratio, often introducing a noticeable background hiss on weaker
stations. You may need a better antenna setup to achieve a clean
stereo lock.

# References

1. https://www.youtube.com/watch?v=wz3aZ9gULM8
2. MC1310 data sheet.
2. LM1310 data sheet.
