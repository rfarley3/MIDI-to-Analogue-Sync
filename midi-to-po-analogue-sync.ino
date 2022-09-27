/* MIDI to Pocket Operator Sync (a form of Analogue Sync)
 * rfarley3@github
 * 21 Sept 2022
 * 
 * Reads MIDI clock and note on/off, outputs analogue sync and CV.
 * For chip limitations, this does not output gate, trig, mod (pitch), aftertouch, etc.
 * Also, this is written with Pocket Operators and littleBits in mind. PO just needs a clock sync.
 * LB can work fine with a separate gate and/or trig, but LB sync can fill in for gate/trig in most instances.
 * LB CV also works great as CV and gate (CV on only when gate on), esp since s-trig (env particularly) is covered by these on-off-on events
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
 *     - consider mod with alternate frequency divisions (pulse per half, whole, 16th, 32nd, etc)
 *     - consider mod with fractional duty cycle percentage (per pulse, only on 50%, with potential offset start by x%); or just be creative with envelope bit
 *   - CV (0-5V, Volt per Octave) with several options to configure, uses a gate and note algorithm setting
 *     - Depending on the options you enable/disable below you can change if the gate/note off turns off CV, and/or if there are retrigger events at note change or clock ticks
 *     - Depending on the option you chose for the note algorithm, you can do lowest, highest, latest
 *     - You can choose arp (any > 1 notes; up, down, up-down, rand) to handle multiple notes, falling back to alg once all notes are off
 *     - Default is CV gated (by key press) glissando (ignore note on retriggers) (no clock retrigger), latest note, up-down arp
 */
#include <MIDI.h>



/* The 8-pin ATTinys (25/45/85) all have 5 usable digital output pins 0-4
 * The Adafruit Trinket 5V is a t85, but pins 3-4 are shared with USB
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
 */
#define IN_D_MIDI 0  
// Status LED
#define OUT_D_BOARD_LED 1
#define LED_BLINK_PERIOD 100
unsigned long LED_BLINK_ON_MILLIS = 0;
// CV output
#define OUT_A_CV 1  // if not CV then OUT_D_GATE
/* PO/Volca/Analogue Sync V-trig pulse
 * Prev volca-po-analogue-sync-divider req minimum of 30 msec pulse and refactory period
 *   - 15 in the active state, 14 low/sleep, and loop had a 1 sleep (so loop could use lower power, only poll 1/msec)
 * Littlebits can use the inverted PO (so 2.5 msecs S-trigger) but the bits themselves keyboard/sequencer/etc short for 10-30 msecs
 *   - 30 msecs will be ready to catch 30ish triggers per second, or 1800 pulses per minute. At Volca/PO's 2 PPQN, this is 900 BPM
 *   - 30 msec is too long for reading MIDI (at 24 PPQN, 1800 pulses/minute is only 75 BPM)
 * PO may only be 2.5 msecs, and Volca is 15 msecs
 *   - PO and Volca rated for 5 V 15 msec v-trig pulse, but both should work with 3.3 V (would still give 3 and 5 V logic high)
 */
#define OUT_D_PO_SYNC 2
#define SYNC_PULSE_PERIOD 15
unsigned long SYNC_PULSE_ON_MILLIS = 0;
// littleBits Sync (5v to 0v s-trig)
#define OUT_D_LB_SYNC 3  // shared with USB, has pullup on it
// #define            4 see aread in next line, aread 2 is dread 4
// #define IN_A_NOTEALG/CHAN 2  // if not CV then IN_A_FREQ/CHAN 2
// #define OUT_D_GATE_OFF 4  // if CV RC DAC circuit lingers beyond gate add RT switch with bias HIGH when gate is off, but loose IN_A_* 2

// from Examples MIDI Bench
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


void handleClock(void);
void handleStart(void);
void handleContinue(void);
void handleStop(void);
void handleSystemReset(void);
void pulse_beat(void);
void pulse_po(void);
void led_pin_on(void);
void led_pin_off(void);
void sync_pin_on(void);
void sync_pin_off(void);


unsigned int clock_ticks = 0;
bool pulsing = false;


void setup() {
  //setup_blink();
  setup_v1();
}


void loop() {
  //loop_blink();
  loop_v1();
}

/*************************************************
 * read MIDO from d0
 * v0 is to blink clock sync on LED on d1, then only blink per beat
 * v1 blink beat on LED on d1, output PO on d2
 * v2 output LB sync as LED on d1; or output LB to d3
 * v3 blink clock sync on d1 on LED, output PO on d2, and LB sync on d3
 * v4 blink gate (note on/off) on LED on d1
 * v5 output gated, latest CV on d1
 */

 void setup_blink() {
  pinMode(OUT_D_BOARD_LED, OUTPUT);
  digitalWrite(OUT_D_BOARD_LED, LOW);
  pinMode(OUT_D_PO_SYNC, OUTPUT);
  digitalWrite(OUT_D_PO_SYNC, LOW);
 }

void loop_blink() {
  unsigned long now = millis();
  if (now % 1000 > 100 && now % 1000 < 200) {
    digitalWrite(OUT_D_PO_SYNC, HIGH);
  }
  else {
    digitalWrite(OUT_D_PO_SYNC, LOW);
  }
  if (now % 1000 < 100) {
    digitalWrite(OUT_D_BOARD_LED, HIGH);
    }
  else {
    digitalWrite(OUT_D_BOARD_LED, LOW);
  }
}

void setup_v1() {
  clock_ticks = 0;
  pulsing = false;
  pinMode(OUT_D_BOARD_LED, OUTPUT);
  digitalWrite(OUT_D_BOARD_LED, LOW);
  LED_BLINK_ON_MILLIS = 0;
  pinMode(OUT_D_PO_SYNC, OUTPUT);
  digitalWrite(OUT_D_PO_SYNC, LOW);
  SYNC_PULSE_ON_MILLIS = 0;
  // IN_D_MIDI 
  //MIDI_CREATE_DEFAULT_INSTANCE();
  //MIDI.begin();           // Launch MIDI, by default listening to channel 1. // 31250
  // initialize MIDI serial
  //SoftwareSerial midiSerial(0, 2); // RX, TX
  //MIDI_CREATE_INSTANCE(SoftwareSerial, midiSerial, polbmidi);
  // https://github.com/FortySevenEffects/arduino_midi_library/wiki/Using-Callbacks
  // midiPoLb.setHandleNoteOn(handleNoteOn);
  midiPoLb.setHandleClock(handleClock);
  midiPoLb.setHandleStart(handleStart);
  midiPoLb.setHandleContinue(handleContinue);
  midiPoLb.setHandleStop(handleStop);
  midiPoLb.setHandleSystemReset(handleSystemReset);
  midiPoLb.begin();
}

void pulse_beat(void) {
  led_pin_on();
  // s-trig LB sync
}

void pulse_po(void) {
  sync_pin_on();
}


void loop_v1() {
    midiPoLb.read();
    led_pin_off();
    sync_pin_off();
}

void handleClock(void) {
  if (!pulsing) {
    return;
  }
  // Assume 4/4 time, so a beat is a quarter note
  // There are 24 MIDI clocks per beat, so 24 MIDI PPQN
  // PO and Volca use 2 PPQN, so 12 MIDI pulses per 1 PO pulse
  if ((clock_ticks % 12) == 0) {
    pulse_po();
  }
  // http://lauterzeit.com/arp_lfo_seq_sync/
  //                   0         1         2
  // clock_ticks (in)  0123456789012345678901234
  // call pulse?       ynnnnnnnnnnnnnnnnnnnnnnny
  // clock_ticks (out) 1234567890123456789012341
  if ((clock_ticks % 24) == 0) {
    // avoid eventual overflow
    clock_ticks = 0;
    pulse_beat();
  }
  clock_ticks++;
}

void handleStart(void) {
  // It's uncertain if there should be a pulse on start, or only the 1st clock after a start
  // this logic assumes the 1st pulse is the 1st clock after a start
  clock_ticks = 0;
  pulsing = true;
}

void handleContinue(void) {
  // assume you can't pause, so continue is same as start for PO
  handleStart();
}

void handleStop(void) {
  // turn off any CV
  // allow manual/gated key presses through
  pulsing = false;
}

void handleSystemReset(void) {
  // system panic, turn off any notes
  handleStop();
// TODO
//  digitalWrite(OUT_D_BOARD_LED, LOW);
//  pulse_millis[OUT_D_BOARD_LED] = 0;
//  digitalWrite(OUT_D_PO_SYNC, LOW);
//  pulse_millis[OUT_D_PO_SYNC] = 0;
  // TODO turn off any cv/notes/lb-sync
}

// TODO
// TimeCodeQuarterFrame (if 0, stop and start/set clock_ticks to 0)?
// SongPosition (if 0, stop and start, set clock_ticks to 0)?
// SongSelect (stop and start)?

void led_pin_on() {
  digitalWrite(OUT_D_BOARD_LED, HIGH);
  LED_BLINK_ON_MILLIS = millis();
}

void sync_pin_on() {
  digitalWrite(OUT_D_PO_SYNC, HIGH);
  SYNC_PULSE_ON_MILLIS = millis();
}

void led_pin_off() {
  if (LED_BLINK_ON_MILLIS) {
    // attiny millis overflow is ~49 days, logic to detect that condition out of scope for this sketch
    if ((millis() - LED_BLINK_ON_MILLIS) > LED_BLINK_PERIOD) {
      digitalWrite(OUT_D_BOARD_LED, LOW);
      LED_BLINK_ON_MILLIS = 0;
    }
  }
}

void sync_pin_off() {
  if (SYNC_PULSE_ON_MILLIS) {
    // attiny millis overflow is ~49 days, logic to detect that condition out of scope for this sketch
    if ((millis() - SYNC_PULSE_ON_MILLIS) > SYNC_PULSE_PERIOD) {
      digitalWrite(OUT_D_PO_SYNC, LOW);
      SYNC_PULSE_ON_MILLIS = 0;
    }
  }
}
