/* MIDI to Pocket Operator Sync (a form of Analogue Sync)
 * rfarley3@github
 * Started 21 Sept 2022
 *
 * Significant documentation in README.md
 *
 * Reads:
 *   - MIDI in, via http://arduinomidilib.sourceforge.net/a00001.html
 *   - if you connect pin #2 to ground during boot, then it will ignore stops until reset
 * Writes:
 *   - analogue sync to pin #3
 *     - v-trig: 5V logic high, 20 msec trigger
 *     - PPEN (Pulse per Eigth Note) based on MIDI in clock
 *     - aka 2 PPQN (Pulse per Quarter note)
 *   - blinks built-in LED labelled #1 on the start of every measure while pulsing
 *   - voltage and current too low to power LB gate or CV
 *
 * Leach power from MIDI at your own risk
 *  - Optoisolated4N35Schem.png is a original spec proper MIDI circuit
 *    - https://i.stack.imgur.com/WIJf4.png as Optoisolated6N137Schem.png
 *    - https://tigoe.github.io/SoundExamples/midi-serial.html as Optoisolated6N138.png
 *  - ATTiny85 can run down to 1.7v, and is happy at 3v (if you wanted to leach off Pocket Operator batteries)
 *    - Alternate version of this uses PO power and an optoisolator on the MIDI input to uncouple the DC/ground
 *  - Powering from MIDI should send MIDI pullup voltage to "3V" or "5V" pin on trinket
 *    - not USB, not Bat or else you loose current to voltage regulators
 *  - I'm no expert at reducing ATTiny power consumption, TODO someone optimize code/serial read/etc
 *    - But it should minimize the impact inputs and outputs have on it
 *    - Recommend that you also remove the green power LED labelled ON
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
 *  - But previous examples didn't read MIDI clock messages, just note-on/off or wrote clocks
 */
/*
 * Idea bin:
 *   - littleBits sync mods
 *     - consider mod with alternate frequency divisions (pulse per half, whole, 16th, 32nd, etc)
 *     - consider switch to disable stop (ignore stops) 
 *     - consider button to adjust (inject fake) clock ticks
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
 *  4      2        Y     resv USB, best for dwrite/awrite, dread/aread may conflict when USB writing to it, can use timer1 to not interfere with milli
 */
/* MIDI
 * Given this pin number use SoftwareSerial and MIDI.h to set up Rx
 * Do not set Tx to a pin that is used by other things
 * You should use the datasheet optocoupler method:
 * MIDI Vref (wire 4) ---|---[optocoupler]---- (pulled up) Rx (IN_D_MIDI)
 *                       ^d1      |
 * MIDI Data (wire 5) ---|--------|
 * But if you don't care about your equipment (ground loops/voltage isolation), then you can power this from MIDI
 * MIDI Vref ------------ Vcc (physical pin 8, marked 5V or 3V on Trinket, assumes MIDI source has a resistor (such as a 220 Ohm per spec))
 * MIDI Data ------------ Rx (IN_D_MIDI)
 * MIDI Gnd  ------------ Gnd (physical pin 4)
 * I'd recommend you read https://mitxela.com/projects/midi_on_the_attiny and use the circuit (minus audio out) at https://mitxela.com/img/uploads/tinymidi/SynthCableSchem.png
 *
 * Run at reduced clock speed to survive low voltage/current environment.
 * Trinket (t85) is easy to set to 8 MHz (implemented here), but 1 MHz might be better (not tested).
 * I had to trim down this project due to lots of errors that caused spurious MIDI Stops (0xfe) and Reset (0xff)
 * - converting LED 1 pulse per beat to LB gate sync via a NPN (2N3904 1k bias, 10k collector and output, emitter direct to ground) consumed too much power
 * - outputting LB gate sync from a pin consumed too much power
 * - using millis to control pulses interferes with softwareserial
 * - delays work, but too long and midi.read will have corruptions, so eliminated as many as possible (only 1)
 * - interrupts are a bad idea, due to software serial
 * - pin4 can use timer1 (leaving timer0 for delay/milli which helps softwareserial, see previous commits with PWM_init4 and analogWrite4), but no point if not using CV
 * 
 * Here is a rabbit hole to learn about why interrupt driven millis() and other calls interfer with softwareserial https://www.best-microcontroller-projects.com/arduino-millis.html
 * Which is summarized at https://arduino.stackexchange.com/a/38575
 *
 * MIDI reads should be done on hardware serial, so atmega328, atmega32u4, etc; but their power needs are too high. Potentially eval attiny841.
 * Here is a Teensy 3.6 working as is https://little-scale.blogspot.com/2017/09/quick-and-easy-usb-midi-to-cv-gate.html
 * I tried Trinket M0 and the hardware serial was great, but the bootloader fires up the dotstar and frequently failed to boot.
 *
 * PO/Volca/Analogue Sync V-trig pulse
 * Prev volca-po-analogue-sync-divider req minimum of 30 msec pulse and refactory period
 *   - 15 in the active state, 14 low/sleep, and loop had a 1 sleep (so loop could use lower power, only poll 1/msec)
 * Littlebits can use the inverted PO (so 2.5 msecs S-trigger) but the bits themselves keyboard/sequencer/etc short for 10-30 msecs
 *   - 30 msecs will be ready to catch 30ish triggers per second, or 1800 pulses per minute. At Volca/PO's 2 PPQN, this is 900 BPM
 *   - 30 msec is too long for reading MIDI (at 24 PPQN, 1800 pulses/minute is only 75 BPM)
 * PO may only be 2.5 msecs, and Volca is 15 msecs
 *   - PO and Volca rated for 5 V 15 msec v-trig pulse, but both should work with 3.3 V (would still give 3 and 5 V logic high)
 */
#define IN_D_MIDI 0
#define OUT_D_BEAT 1  // on board LED is on pin 1
#define IN_D_NOSTOP 2
#define OUT_D_PO_SYNC 3  // 3 or 4 (since they are better for outputs) and preserves 2 for aread(1)
// see notes in handle_clock for alternate use for pin 4
#define SYNC_PULSE_PERIOD 5  // 15 is ideal across the most platforms, see comments above, reducing to minimize impact of delay (since delay is blocking)


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


bool peekToggle(int pin);
void digitalWriteHighWeak(int pin);
void digitalWriteLowWeak(int pin);
void handleClock();
void handleStart();
void handleStop();
//void handleContinue();
//void handleSystemReset();


unsigned int clock_ticks = 0;
unsigned int beats = 0;
bool pulsing = false;


/*************************************************
 * read MIDI from d0
 *   blink built-in LED (D1) at first clock of a measure
 *   pulse V-trig (D3) at 2 PPQN
 */
void setup() {
  // NOTE 16 MHz reqs 5v (not good if using MIDI Voltage, which could be 3ish volt)
  // The lower the MHz the better to help with low current environment, the software serial means you can't sleep/low power
  // Use 8 MHz for the below, TODO try 1 MHz
  clock_ticks = 0;
  beats = 0;
  pulsing = false;
  pinMode(OUT_D_PO_SYNC, OUTPUT);
  digitalWrite(OUT_D_PO_SYNC, LOW);
  pinMode(OUT_D_BEAT, OUTPUT);
  digitalWrite(OUT_D_BEAT, LOW);
  bool respect_stops = peekToggle(IN_D_NOSTOP);
  // https://github.com/FortySevenEffects/arduino_midi_library/wiki/Using-Callbacks
  midiPoLb.setHandleClock(handleClock);
  midiPoLb.setHandleStart(handleStart);
  if (respect_stops) {
    midiPoLb.setHandleStop(handleStop);
    // midiPoLb.setHandleSystemReset(handleSystemReset);
  }
  // midiPoLb.setHandleContinue(handleContinue);
  digitalWriteHighWeak(OUT_D_BEAT);
  delay(10);
  digitalWriteLowWeak(OUT_D_BEAT);
  delay(50);
  digitalWriteHighWeak(OUT_D_BEAT);
  delay(10);
  digitalWriteLowWeak(OUT_D_BEAT);
  midiPoLb.begin();  // channel doesn't matter for clocks
}


void digitalWriteHighWeak(int pin) {
  // can accept pin in any state (but need to verify if dwrite HIGH is ok)
  pinMode(pin, INPUT_PULLUP);
  // implies digitalWrite(pin, HIGH) but through pullup in order to use less current
}


void digitalWriteLowWeak(int pin) {
  // can accept a pin in any state (input/output, high/pullup/low)
  digitalWrite(pin, LOW); // dwrite low on an input turns off pullup
  // floating inputs waste more current than pullups, changing to output avoids that
  pinMode(pin, OUTPUT);
}


bool peekToggle(int pin) {
  // Avoid any unnecessary, prolonged drain of input_pullup
  // Temporarily make a pin an input, read it, then turn off
  pinMode(pin, INPUT_PULLUP);
  // give chance for pullup to settle, may not be necessary
  delay(1);
  bool respect_stops = digitalRead(pin);
  // no need to waste current going to ground, we won't be checking this input anymore
  digitalWrite(pin, LOW); // dwrite low on an input turns off pullup
  // floating inputs waste more current than pullups, changing to output avoids that
  pinMode(pin, OUTPUT);
  return respect_stops;
}


void loop() {
  /* read(int inChannel=midiPoLb.inChannel) aka can be given chan to read, else defaults to one from begin
   * blocks as long as it takes to handle any messages
   * reads MIDI clock/sync messages and uses callbacks (setHandle*) to:
   *   - advance clock_ticks, which leads to calling pulse_*
   *   - respond to start to turn on pulsing (uncomment handleContinue to cover continue)
   *   - respond to stop to turn off pulsing (uncomment handleReset to cover reset)
   *     - unless pin 2 aka D_IN_NOSTOP is low during boot/setup
   * read turns off interrupts and uses delay which reads millis; millis use timer0 
   * so any other timer0 usage, interrupts, or millis will degrade softwareserial
   */
  midiPoLb.read();  
}


/* MIDI Handlers
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
    // called once per 12 MIDI clocks (half a beat)
    //                   0         1         2
    // clock_ticks (in)  0123456789012345678901234
    // call pulse?       ynnnnnnnnnnnynnnnnnnnnnny
    // clock_ticks (out) 1234567890123456789012341
    //
    // You can avoid low current/voltage at trinket by running V-trigs to an inverter/buffer
    // connect pin 1 (OUT_D_BEAT) to a NPN and use its collector as the LB gate-sync/s-trig
    // 5v to 10k Ohm to collector pulls up output, emitter direct to ground, OUT_D_BEAT to 1k Ohm to bias
    // When OUT_D_BEAT is low, collector/LB signal is pulled high, providing 5v gate
    // When OUT_D_BEAT is high, the 3ish volts and low mA on OUT_D_BEAT will allow flow from collector-emitter, shorting the signal
    // This code avoids using pin 4 to as an output for variable pulse/frequency divider
    // pin 3 is 2 PPQN (eigth notes)
    // pin 1 is 1 pulse per measure (whole notes)
    // pin 4 could be eigth, quarter, half, or whole
    // eigth: if clock_ticks % 12
    // quarter: if clock_ticks % 24 == 0
    // half: if clock_ticks % 24 == 0 && beats % 2 == 0
    // whole: if clock_ticks % 24 == 0 && beats == 0
    digitalWriteHighWeak(OUT_D_PO_SYNC);
    if ((clock_ticks % 24) == 0 && beats == 0) {
      digitalWriteHighWeak(OUT_D_BEAT);
    }
    delay(SYNC_PULSE_PERIOD);
    digitalWriteLowWeak(OUT_D_PO_SYNC);
    if ((clock_ticks % 24) == 0 && beats == 0) {
      digitalWriteLowWeak(OUT_D_BEAT);
    }
  }
  // http://lauterzeit.com/arp_lfo_seq_sync/
  if ((clock_ticks % 24) == 0) {
    // avoid eventual overflow
    clock_ticks = 0;
    beats = (beats + 1) % 4;  // blink led every measure to give user ability to catch drift
    // the built-in LED should blink at same time as step 1 in your sequencer
  }
  clock_ticks++;
}


void handleStart() {
  // 0xfa It's uncertain if there should be a pulse on start, or only the 1st clock after a start
  // this logic assumes the 1st pulse is the 1st clock after a start
  digitalWriteLowWeak(OUT_D_PO_SYNC);
  digitalWriteLowWeak(OUT_D_BEAT);
  clock_ticks = 0;
  beats = 0;
  pulsing = true;
}


void handleStop() {
  // 0xfe clock is going, but ignore it until started again
  pulsing = false;
  digitalWriteLowWeak(OUT_D_PO_SYNC);
  digitalWriteLowWeak(OUT_D_BEAT);
}


//void handleContinue() {
//  // 0xfb assume you can't pause, so continue is same as start for PO
//    if (pulsing) {
//    // already started, drops any potential (unobserved) fake continue
//    return;
//  }
//  handleStart();
//}


//void handleSystemReset() {
//  // 0xff system panic, turn off any outputs
//  handleStop();
//}
