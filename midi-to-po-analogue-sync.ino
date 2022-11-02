/* MIDI to Pocket Operator Sync (a form of Analogue Sync)
 * rfarley3@github
 * Started 21 Sept 2022
 *
 * Significant documentation in README.md
 *
 * Reads:
 *   - MIDI in, via http://arduinomidilib.sourceforge.net/a00001.html
 * Writes:
 *   - analogue sync
 *     - v-trig: 5V logic high, 20 msec trigger
 *     - PPEN (Pulse per Eigth Note) based on MIDI in clock
 *   - littleBits sync
 *     - s-trig: 5V constant/resting state, 0V trigger (serves as trigger/note change, and clock)
 *       - some bits are trailing edge some are rising edge
 *       - some bits ignore 5V (like envelope), others (sequencer) control subsequent bits and should get 5V (such as VCO, filter, keyboard)
 *     - PPQN (Pulse per Quarter Note) based on MIDI in clock
 *
 * Leach power from MIDI at your own risk
 *  - Optoisolated4N35Schem.png is a original spec proper MIDI circuit
 *    - https://i.stack.imgur.com/WIJf4.png as Optoisolated6N137Schem.png
 *    - https://tigoe.github.io/SoundExamples/midi-serial.html as Optoisolated6N138.png
 *
 * Great source for MIDI schematics and spec is at:
 *  - https://mitxela.com/other/midi_spec
 *
 * Idea is a mixture of things, but lots of credit is due to:
 *  - https://mitxela.com/projects/polyphonic_synth_cable
 *    - which includes the image within this repo that shows power leaching the attiny from midi
 *    - https://mitxela.com/img/uploads/tinymidi/SynthCableSchem.png
 *    - And this amazing write-up https://mitxela.com/projects/midi_on_the_attiny
 *    - And this, which is fantastic https://mitxela.com/projects/smallest_midi_synth
 */
/*
 * Idea bin:
 *   - littleBits sync mods
 *     - consider mod with alternate frequency divisions (pulse per half, whole, 16th, 32nd, etc)
 *     - consider mod with fractional duty cycle percentage (per pulse, only on 50%, with potential offset start by x%); or just be creative with envelope bit
 *   - TODO CV (0-5V, Volt per Octave) with several options to configure, uses a gate and note algorithm setting
 *     - Depending on the options you enable/disable below you can change if the gate/note off turns off CV, and/or if there are retrigger events at note change or clock ticks
 *     - Depending on the option you chose for the note algorithm, you can do lowest, highest, latest, loudest
 *     - You can choose arp (any > 1 notes; up, down, up-down, rand) to handle multiple notes, falling back to alg once all notes are off
 *     - Default is CV gated (by key press) glissando (ignore note on retriggers) (no clock retrigger), latest note, up-down arp
 */
#include <avr/power.h>
#include <MIDI.h>



/* The 8-pin ATTinys (25/45/85) all have 5 usable digital output pins 0-4
 * The Adafruit Trinket 5V is a t85, but pins 3-4 are shared with USB
 * 3.3V t85 Trinket tested, but no benefits (couldn't self-regulate Vref)
 * per docs, they are best for outputs (ex: pin 3 has a pullup resistor that would make aread/dread weird)
 * pin | aread | awrite | note
 *  0      N        Y   
 *  1      N        Y     has the built-in LED as well
 *  2      1        N
 *  3      3        N     resv USB, has pullup resistor, best for dwrite
 *  4      2        Y     resv USB, best for dwrite/awrite, dread/aread may conflict when USB writing to it
 */
/* MIDI
 * Given this pin number use SoftwareSerial and MIDI.h to set up Rx
 * Do not set Tx to a pin that is used by other things
 * You should use the datasheet optocoupler method:
 * MIDI Vref (wire 4) ---|---[optocoupler]---- (pulled up) Rx (IN_D_MIDI)
 *                       ^d1      |
 * MIDI Data (wire 5) ---|--------|
 * But if you don't care about your equipment (ground loops/voltage isolation), then you can power this from MIDI
 * MIDI Vref ------------ Vcc (physical pin 8, marked 5V or 3V on Trinket)
 * MIDI Data ------------ Rx (IN_D_MIDI)
 * MIDI Gnd  ------------ Gnd (physical pin 4)
 * I'd recommend you read https://mitxela.com/projects/midi_on_the_attiny and use the circuit (minus audio out) at https://mitxela.com/img/uploads/tinymidi/SynthCableSchem.png
 *
 * I still got lots of errors that caused spurious MIDI Stops (0xfe) and Reset (0xff), which is why I added IGNORE_STOP and IGNORE_RESET
 * Problem is that software serial is just very very slow, combine that with heavy use of millis (their own interrupts)
 * and you have high chance for corruption. The power from this vampire circuit isn't enough to increase clock speed to 16 MHz.
 * Here is a rabbit hole to learn about why interrupt driven millis() and other calls interfer with softwareserial https://www.best-microcontroller-projects.com/arduino-millis.html
 * Which is summarized at https://arduino.stackexchange.com/a/38575
 *
 * MIDI reads should be done on hardware serial, so atmega328, atmega32u4, etc; but their power needs are too high.
 * Potentially eval attiny841.
 * Here is a Teensy 3.6 working as is https://little-scale.blogspot.com/2017/09/quick-and-easy-usb-midi-to-cv-gate.html
 * Potentially remove all interrupt driven code for timers, as long as all timers are less than baudrate (so no bits are missed).
 *
 * Potentially code in assembly instead of c/ino Arduino sketch
 *   - https://emalliab.wordpress.com/2019/03/02/attiny85-midi-to-cv/
 *     - They were able to do note on/off for CV (and potentially Trigger)
 *     - But not clock reads for sync
 *   - https://arduino.stackexchange.com/a/46110 mentions missing messages, although I also saw corrupt messages
 *   - Mitxela has note on/off/cv but not clock
 *     - https://git.mitxela.com/synthcable#files
 *     - https://github.com/mitxela/synthcable/blob/master/SynthCable.asm
 *     - Including for a Korg device https://mitxela.com/projects/midi_monotron
 *     - https://git.mitxela.com/MidiMonotron
 *   - General tool pipeline for AVR ASM https://www.nongnu.org/avr-libc/user-manual/inline_asm.html
 *
 * Switching to Trinket M0 (Atmel ATSAMD21) for hardware UART.
 */
#define IN_D_MIDI 0
#define IGNORE_STOP 0  // use RST to stop manually and arm for next auto-start
#define IGNORE_RESET 1  // use RST to force it all off
#define IGNORE_PARSE_ERROR 1  // if MIDI.h fails to parse a byte, used for debug
#define PULSE_CHECK_PERIOD 5  // only poll if pulses should be off this often
unsigned long PULSE_CHECK_MILLIS = 0;
/* PO/Volca/Analogue Sync V-trig pulse
 * Prev volca-po-analogue-sync-divider req minimum of 30 msec pulse and refactory period
 *   - 15 in the active state, 14 low/sleep, and loop had a 1 sleep (so loop could use lower power, only poll 1/msec)
 * Littlebits can use the inverted PO (so 2.5 msecs S-trigger) but the bits themselves keyboard/sequencer/etc short for 10-30 msecs
 *   - 30 msecs will be ready to catch 30ish triggers per second, or 1800 pulses per minute. At Volca/PO's 2 PPQN, this is 900 BPM
 *   - 30 msec is too long for reading MIDI (at 24 PPQN, 1800 pulses/minute is only 75 BPM)
 * PO may only be 2.5 msecs, and Volca is 15 msecs
 *   - PO and Volca rated for 5 V 15 msec v-trig pulse, but both should work with 3.3 V (would still give 3 and 5 V logic high)
 */
#define OUT_D_PO_SYNC 1  // on board LED is on pin 1
#define SYNC_PULSE_PERIOD 15
unsigned long SYNC_PULSE_ON_MILLIS = 0;
/* Input
 * Using Pin 2, which is dread 2, but aread 1
 * Possible inputs:
 *   manually adjust/offset beat tracking by n clocks
 * - auto vs manual pulsing=true (see IGNORE_STOP, IGNORE_RESET)
 *   LB frequency pulses per beat change (1/8, 1/4/, 1/2, 1, 2, 4, 8) default 1
 *   LB sync duty cycle percentage, default 100%
 *   CV channel select 0-15
 * - CV note choice algorithm (latest, highest, lowest, loudest, arp up, down, up-down, rand), default latest
 *   CV is only gate (aka force always 5v) vs PWM DAC as % of Octave per Volt by note number
 *   CV is gated while note on, vs always on last setting that makes sense
 *   CV is glissando vs s-triggers when a note is changed
 *   CV is ignores clock vs s-triggers at LB pulses per beat
 * #define IN_A_NOTEALG/CHAN 1  // if not CV then IN_A_FREQ/CHAN 2
 * #define OUT_D_GATE_OFF 2  // if CV RC DAC circuit lingers beyond gate add RT switch with bias HIGH when gate is off, but loose IN_A_* 2
 */
// littleBits Sync (5v to 0v s-trig)
#define OUT_D_LB_SYNC 3  // shared with USB, has 1.5k pullup on it per board spec
#define LB_STRIG_PERIOD 15
unsigned long LB_STRING_MILLIS = 0;
// CV output
//  Default CV is gated (by key press) glissando (ignore note on retriggers) (no clock retrigger), latest note, up-down arp
#define OUT_A_CV 4  // if not CV then OUT_D_GATE


// from MIDI Bench Example
#if defined(ARDUINO_SAM_DUE) || defined(USBCON)
    // Print through USB and bench with Hardware serial
    MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, midiBench);
#else
    #include <SoftwareSerial.h>
    SoftwareSerial midiSerial(IN_D_MIDI, IN_D_MIDI); // Rx, Tx
    // in SoftwareSerial.cpp constructor, setRX happens after setTX, so Rx overrides Tx, and Rx == Tx is ok
    // see https://github.com/PaulStoffregen/SoftwareSerial/blob/63f9b1aae6564d301d7ba31261d1f2390e2a7359/SoftwareSerial.cpp#L578
    MIDI_CREATE_INSTANCE(SoftwareSerial, midiSerial, midiPoLb);
#endif


void handleClock();
void handleStart();
void handleContinue();
void handleStop();
void handleSystemReset();
void handleError();
void pulse_beat();
void pulse_half_beat();
void start_lb();
void stop_lb();
void lb_pin_low();
void lb_pin_high(unsigned long now=0);
void sync_pin_on();
void sync_pin_off(unsigned long now=0);


unsigned int clock_ticks = 0;
bool pulsing = false;


/*************************************************
 * read MIDI from d0
 *   v0 is to blink clock sync on LED on d1 (built-in LED), then only blink per beat
 *   v1 output PO on d1 (which will blink sync on built-in LED)
 * > v2 output LB sync on d3
 * Following will not be implemented, moving to chip with hardware UART
 *   v3 blink gate (note on/off) on LED on d4
 *   v4 output gated, latest CV on d4
 */
void setup() {
  // default Arduino named function called once, before loop
  // NOTE 16 MHz reqs 5v (not good if using MIDI Vref)
  if (F_CPU == 16000000) clock_prescale_set(clock_div_1);
  clock_ticks = 0;
  pulsing = false;
  pinMode(OUT_D_PO_SYNC, OUTPUT);
  digitalWrite(OUT_D_PO_SYNC, LOW);
  SYNC_PULSE_ON_MILLIS = 0;
  pinMode(OUT_D_LB_SYNC, OUTPUT);
  digitalWrite(OUT_D_LB_SYNC, LOW);
  LB_STRING_MILLIS = 0;
  // https://github.com/FortySevenEffects/arduino_midi_library/wiki/Using-Callbacks
  midiPoLb.setHandleClock(handleClock);
  midiPoLb.setHandleStart(handleStart);
  midiPoLb.setHandleContinue(handleContinue);
  if (!IGNORE_STOP) midiPoLb.setHandleStop(handleStop);
  if (!IGNORE_RESET) midiPoLb.setHandleSystemReset(handleSystemReset);
  if (!IGNORE_PARSE_ERROR) midiPoLb.setHandleError(handleError);
  // begin(int inChannel=1)
  midiPoLb.begin();
}


void loop() {
  // default Arduino named function called repeatedly
  /* read(int inChannel=midiPoLb.inChannel) aka can be given chan to read, else defaults to one from begin
   * blocks as long as it takes to handle any messages
   * reads MIDI clock/sync messages and uses callbacks (setHandle*) to:
   *   - advance clock_ticks, which leads to calling pulse_*
   *   - respond to start/continue to turn on pulsing
   *   - respond to stop/reset to turn off pulsing (unless IGNORE_STOP/IGNORE_RESET)
   *   Do not use delay() in combo with softwareserial read
   */
  midiPoLb.read();
  /* millis() are interrupt driven, which competes with software serial
   * Reduce millis calls to prioritize midi.read/reduce its errors:
   *   - Only call once here and pass value
   *   - Reduce non-midi.read calls, by only doing them if its been x msecs
   */
  unsigned long now = millis();
  if ((now - PULSE_CHECK_MILLIS) > PULSE_CHECK_PERIOD) {
    sync_pin_off(now);  // undo vtrig if it's been enough time
    lb_pin_high(now);  // undo strig if it's been enough time
    PULSE_CHECK_MILLIS = now;
  }
}


/* Helper functions
 * Turning on and off pins, setting timers, global bools/flags, etc
 */
void pulse_beat() {
  // called once per 24 MIDI clocks (once per beat)
  // used to be where the built-in LED was blinked per beat, but now that pin is for PO sync so LED flashes twice per beat
  // s-trig LB sync
  lb_pin_low();
}


void pulse_half_beat() {
  // called once per 12 MIDI clocks (half a beat)
  sync_pin_on();
}


void start_lb() {
  digitalWrite(OUT_D_LB_SYNC, HIGH);
  LB_STRING_MILLIS = 0;
  // notes/cv
}


void stop_lb() {
  digitalWrite(OUT_D_LB_SYNC, LOW);
  LB_STRING_MILLIS = 0;
  // notes/cv
}


void lb_pin_low() {
  // send a strig on the LB Sync out pin
  digitalWrite(OUT_D_LB_SYNC, LOW);
  LB_STRING_MILLIS = millis();
}


void lb_pin_high(unsigned long now=0) {
  // polled each loop, if the strig has been low long enough, turn signal high
  if (LB_STRING_MILLIS) {
    if (!now) {
      now = millis();
    }
    // attiny millis overflow is ~49 days, logic to detect that condition out of scope for this sketch
    if ((now - LB_STRING_MILLIS) > LB_STRIG_PERIOD) {
      digitalWrite(OUT_D_LB_SYNC, HIGH);
      LB_STRING_MILLIS = 0;
    }
  }
}


void sync_pin_on() {
  // send a pulse on the PO/Volca/Analogue Sync out pin
  digitalWrite(OUT_D_PO_SYNC, HIGH);
  SYNC_PULSE_ON_MILLIS = millis();
}


void sync_pin_off(unsigned long now=0) {
  // polled each loop, if the pulse has been high long enough, turn it off
  if (SYNC_PULSE_ON_MILLIS) {
    if (!now) {
      now = millis();
    }
    // attiny millis overflow is ~49 days, logic to detect that condition out of scope for this sketch
    if ((now - SYNC_PULSE_ON_MILLIS) > SYNC_PULSE_PERIOD) {
      digitalWrite(OUT_D_PO_SYNC, LOW);
      SYNC_PULSE_ON_MILLIS = 0;
    }
  }
}


/* MIDI Handlers
 * Do any of these need to be covered?
 *   - TimeCodeQuarterFrame (if 0, stop and start/set clock_ticks to 0)?
 *   - SongPosition (if 0, stop and start, set clock_ticks to 0)?
 *   - SongSelect (stop and start)?
 */
void handleClock() {
  // 0xf8 system real time clock
  if (!pulsing) {
    return;
  }
  // Assume 4/4 time, so a beat is a quarter note
  // There are 24 MIDI clocks per beat, so 24 MIDI PPQN
  // PO and Volca use 2 PPQN, so 12 MIDI pulses per 1 PO pulse
  if ((clock_ticks % 12) == 0) {
    //                   0         1         2
    // clock_ticks (in)  0123456789012345678901234
    // call pulse?       ynnnnnnnnnnnynnnnnnnnnnny
    // clock_ticks (out) 1234567890123456789012341
    pulse_half_beat();
  }
  // http://lauterzeit.com/arp_lfo_seq_sync/
  if ((clock_ticks % 24) == 0) {
    // avoid eventual overflow
    clock_ticks = 0;
    //                   0         1         2
    // clock_ticks (in)  0123456789012345678901234
    // call pulse?       ynnnnnnnnnnnnnnnnnnnnnnny
    // clock_ticks (out) 1234567890123456789012341
    pulse_beat();
  }
  clock_ticks++;
}


void handleStart() {
  // 0xfa It's uncertain if there should be a pulse on start, or only the 1st clock after a start
  // this logic assumes the 1st pulse is the 1st clock after a start
  clock_ticks = 0;  // if pulsing and fake start comes in, you may get off beat
  pulsing = true;
  start_lb();
}


void handleContinue() {
  // 0xfb assume you can't pause, so continue is same as start for PO
    if (pulsing) {
    // already started, drops any potential (unobserved) fake continue
    return;
  }
  handleStart();
}


void handleStop() {
  // 0xfe clock is going, but ignore it until started again
  // allow manual/gated key presses through
  // TODO turn off any ungated CV
  pulsing = false;
  stop_lb();
}


void handleSystemReset() {
  // 0xff system panic, turn off any outputs
  // TODO turn off any cv/notes
  pulsing = false;
  digitalWrite(OUT_D_PO_SYNC, LOW);
  SYNC_PULSE_ON_MILLIS = 0;
  digitalWrite(OUT_D_LB_SYNC, LOW);
  LB_STRING_MILLIS = 0;
}


void handleError() {
  // If MIDI.h can't parse the serial input, it'll call this
  // temporary debug, just visually show that it happened
  pulsing = false;
  stop_lb();
  SYNC_PULSE_ON_MILLIS = 0;
  digitalWrite(OUT_D_PO_SYNC, LOW);
  delay(100);
  digitalWrite(OUT_D_PO_SYNC, HIGH);
  delay(1000);
  digitalWrite(OUT_D_PO_SYNC, LOW);
  delay(100);
  digitalWrite(OUT_D_PO_SYNC, HIGH);
}
