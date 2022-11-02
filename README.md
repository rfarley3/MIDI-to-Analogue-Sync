# MIDI-to-Analogue-Sync
Convert MIDI DIN to Analogue Sync for Pocket Operator and littleBits, should cover Volca and most modular.

Reads MIDI clock and note on/off, outputs analogue sync and CV.
For chip limitations, this does not output gate, trig, mod (pitch), aftertouch, 
etc.

Also, this is written with Pocket Operators and littleBits in mind.
PO just needs a clock sync.
LB can work fine with a separate gate and/or trig, but LB sync can fill in for 
gate/trig in most instances.
LB CV also works great as CV and gate (CV on only when gate on), esp since 
s-trig (env particularly) is covered by these on-off-on events.


## Ins and Outs
Reads:
	- MIDI in, via http://arduinomidilib.sourceforge.net/a00001.html

Writes:
- analogue sync
	- v-trig: 5V logic high, 20 msec trigger
		- PPEN (Pulse per Eigth Note) based on MIDI in clock
- littleBits sync
	- s-trig: 5V constant/resting state, 0V trigger
		- (serves as trigger/note change, and clock)
		- some bits are trailing edge some are rising edge
		- some bits ignore 5V (like envelope), others (sequencer) control 
		- subsequent bits and should get 5V (such as VCO, filter, keyboard)
	- PPQN (Pulse per Quarter Note) based on MIDI in clock
	- See ino source for discussion on inter-event timing/s-trig periods

This does not send MIDI, nor use the USB for MIDI
- See https://github.com/jokkebk/TrinketMIDI to send MIDI over Trinket USB
	- https://blog.adafruit.com/2019/10/09/trinketmidi-updated-with-volume-control-midi-trinket/
- Here is a project that reads MIDI from Serial and then sends it over USB
	- https://github.com/mildsunrise/trinket-midi-adapter


## Leach power from MIDI at your own risk
Optoisolated4N35Schem.png is a original spec proper MIDI circuit:
- https://i.stack.imgur.com/WIJf4.png as Optoisolated6N137Schem.png
- https://tigoe.github.io/SoundExamples/midi-serial.html as Optoisolated6N138.png

Great source for MIDI schematics and spec is at:
- https://mitxela.com/other/midi_spec
- I'd recommend you read https://mitxela.com/projects/midi_on_the_attiny and 
use the circuit (minus audio out) at 
https://mitxela.com/img/uploads/tinymidi/SynthCableSchem.png

The datasheet optocoupler method roughly looks like:
```
MIDI Vref (wire 4) ---|---[optocoupler]---- (pulled up) Rx (IN_D_MIDI)
                      ^d1      |
MIDI Data (wire 5) ---|--------|
```

But if you don't care about your equipment (ground loops/voltage isolation), 
then you can power this from MIDI:
```
MIDI Vref ------------ Vcc (physical pin 8, marked 5V or 3V on Trinket)
MIDI Data ------------ Rx (IN_D_MIDI)
MIDI Gnd  ------------ Gnd (physical pin 4)
```


## Credit Where Credit is Due
Idea is a mixture of things, but lots of credit is due to:
- https://mitxela.com/projects/polyphonic_synth_cable
   - which includes the image within this repo that shows power leaching the attiny from midi
   - https://mitxela.com/img/uploads/tinymidi/SynthCableSchem.png
   - And this amazing write-up https://mitxela.com/projects/midi_on_the_attiny
   - And this, which is fantastic https://mitxela.com/projects/smallest_midi_synth

Image for Trinket 5V is from Adafruit
- https://learn.adafruit.com/introducing-trinket

Image for Trinket M0 is from Adafruit
- https://learn.adafruit.com/adafruit-trinket-m0-circuitpython-arduino/pinouts


