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
 *     - aka 2 PPQN (Pulse per Quarter note)
 *   - voltage and current too low to power LB gate or CV
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
 * - pin4 can use timer1 (leaving timer0 for delay/milli which helps softwareserial), but no point if not using CV
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
#define OUT_D_PO_SYNC 3  // 3 or 4 (since they are better for outputs) and preserves 2 for aread(1)
#define SYNC_PULSE_PERIOD 5  // 15 is ideal, see comments above, reducing to minimize impact of delay (since delay is blocking)


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


//void PWM4_init();
//void analogWrite4(uint8_t duty_value);
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
  // NOTE 16 MHz reqs 5v (not good if using MIDI Vref)
  // keep this commented out to stay at 8 MHz to reduce power requirements
  // if (F_CPU == 16000000) clock_prescale_set(clock_div_1);  // sets clock to 16 MHz, default is 8 Mhz
  clock_ticks = 0;
  beats = 0;
  pulsing = false;
  pinMode(OUT_D_PO_SYNC, OUTPUT);
  digitalWrite(OUT_D_PO_SYNC, LOW);
  pinMode(OUT_D_BEAT, OUTPUT);
  digitalWrite(OUT_D_BEAT, LOW);
  // PWM4_init();
  // analogWrite4(0);
  // https://github.com/FortySevenEffects/arduino_midi_library/wiki/Using-Callbacks
  midiPoLb.setHandleClock(handleClock);
  midiPoLb.setHandleStart(handleStart);
  midiPoLb.setHandleStop(handleStop);
  // midiPoLb.setHandleContinue(handleContinue);
  // midiPoLb.setHandleSystemReset(handleSystemReset);
  // begin(int inChannel=1)
  midiPoLb.begin();
}


//// https://learn.adafruit.com/introducing-trinket/programming-with-arduino-ide
//void PWM4_init() {
//  // Set up PWM on Trinket GPIO #4 (PB4, pin 3) using Timer 1
//  TCCR1 = _BV (CS10); // no prescaler
//  GTCCR = _BV (COM1B1) | _BV (PWM1B); // clear OC1B on compare
//  OCR1B = 127; // duty cycle initialize to 50%
//  OCR1C = 255; // frequency
//}
//
// 
//// Function to allow analogWrite on Trinket GPIO #4
//void analogWrite4(uint8_t duty_value) {
//  OCR1B = duty_value; // duty may be 0 to 255 (0 to 100%)
//}


void loop() {
  /* read(int inChannel=midiPoLb.inChannel) aka can be given chan to read, else defaults to one from begin
   * blocks as long as it takes to handle any messages
   * reads MIDI clock/sync messages and uses callbacks (setHandle*) to:
   *   - advance clock_ticks, which leads to calling pulse_*
   *   - respond to start/continue to turn on pulsing
   *   - respond to stop/reset to turn off pulsing (unless IGNORE_STOP/IGNORE_RESET)
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
    digitalWrite(OUT_D_PO_SYNC, HIGH);
    if (beats == 0) {
      digitalWrite(OUT_D_BEAT, HIGH);
    }
    delay(SYNC_PULSE_PERIOD);
    digitalWrite(OUT_D_PO_SYNC, LOW);
    if (beats == 0) {
      digitalWrite(OUT_D_BEAT, LOW);
    }
    beats = (beats + 1) % 8;  // blink led every measure to give user ability to catch drift
    // the built-in LED should blink at same time as step 1 in your sequencer
  }
  // http://lauterzeit.com/arp_lfo_seq_sync/
  if ((clock_ticks % 24) == 0) {
    // avoid eventual overflow
    clock_ticks = 0;
  }
  clock_ticks++;
}


void handleStart() {
  // 0xfa It's uncertain if there should be a pulse on start, or only the 1st clock after a start
  // this logic assumes the 1st pulse is the 1st clock after a start
  digitalWrite(OUT_D_PO_SYNC, LOW);
  digitalWrite(OUT_D_BEAT, LOW);
  clock_ticks = 0;
  beats = 0;
  pulsing = true;
}


void handleStop() {
  // 0xfe clock is going, but ignore it until started again
  pulsing = false;
  digitalWrite(OUT_D_PO_SYNC, LOW);
  digitalWrite(OUT_D_BEAT, LOW);
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
