# MIDI to Pocket Operator Sync (a form of Analogue Sync)
"""MIDI to Pocket Operator Sync (a form of Analogue Sync).
See README.md and midi-to-po-analogue-sync.ino for more docs

 * Y verify current from device is sufficient, 1 led blink, then dotstar color wheel
 * Y port code from ino
 * Y  v0 output blink clock sync on LED on d13
 * Y    - read MIDI from RX aka pin 3
 * Y    - then only blink per beat
 * Y    - ramp up while !pulsing
 *      - stuck in purple dot star with blinking red, probably stuck in bootloader
 *        low power is short purple to 2x flashing yellow every bit
 *        very low power is all red
 *        full boot is purple to yellow to run
 * Y  v1 output PO on d0 aka pin 0
 * Y    - test with LED
 * Y    - test on PO in SY2
 * >  v2 output LB sync on d2 aka pin 2, test with LED then CV bit
 *    Make MIDI optoisolator circuit and use usb battery pack
 *    v3 blink gate (note on/off) on dotstar
 *    v4 output gated, latest CV on ?pin 1? aka A? for true 10b analogue 0-3.3V
"""
import board
import busio
import pwmio
from digitalio import DigitalInOut, Direction  # , Pull
# from analogio import AnalogOut  # , AnalogIn
# import touchio
# import adafruit_dotstar as dotstar
import time
from adafruit_midi import MIDI
# control_change.mpy  control_change_values.mpy  __init__.mpy
# midi_continue.mpy  midi_message.mpy  note_off.mpy  note_on.mpy
# start.mpy  stop.mpy  system_exclusive.mpy  timing_clock.mpy
from adafruit_midi.timing_clock import TimingClock
# from adafruit_midi.channel_pressure import ChannelPressure
# from adafruit_midi.control_change import ControlChange
# from adafruit_midi.note_off import NoteOff
# from adafruit_midi.note_on import NoteOn
# from adafruit_midi.pitch_bend import PitchBend
# from adafruit_midi.polyphonic_key_pressure import PolyphonicKeyPressure
# from adafruit_midi.program_change import ProgramChange
from adafruit_midi.start import Start
from adafruit_midi.stop import Stop
# from adafruit_midi.system_exclusive import SystemExclusive
from adafruit_midi.midi_message import MIDIUnknownEvent


debug = True
midi = None
clock_ticks = 0
pulsing = False
pulse_check = 5
pulse_check_millis = 0
led = None
pulse_led = 15
pulse_led_millis = 0
sync = None
pulse_sync = 15
pulse_sync_millis = 0
pulse_gate = 15
pulse_gate_millis = 0


def print_debug(*argv):
    global debug
    if debug:
        print(*argv)


def setup():
    global debug
    global midi
    global led
    global sync
    # One pixel connected internally!
    # dot = dotstar.DotStar(board.APA102_SCK, board.APA102_MOSI, 1, brightness=0.2)

    # Built in red LED
    # led = DigitalOut(board.D13)
    # led.direction = Direction.OUTPUT
    # led.value = 0
    led = pwmio.PWMOut(board.LED, frequency=5000, duty_cycle=0)

    # PO/Volca/Analogue Sync output
    sync = DigitalInOut(board.D0)
    sync.direction = Direction.OUTPUT
    sync.value = 0

    # Digital input with pullup on D2
    # button = DigitalInOut(board.D2)
    # button.direction = Direction.INPUT
    # button.pull = Pull.UP

    # din_midi = DigitalInOut(board.D0)
    # https://github.com/BlitzCityDIY/midi_uart_experiments/blob/main/CircuitPython/1-23-22_midiUartOut.py
    # Also see "Receive MIDI Over UART and Send Over USB"
    # at https://learn.adafruit.com/midi-for-makers?view=all#midi-messages
    # https://docs.circuitpython.org/en/latest/shared-bindings/busio/#busio.UART
    # board.D0 is pin 0
    # board.RX is pin 3
    uart = busio.UART(None, board.RX, baudrate=31250, timeout=0.001)  # init UART
    # midi_in/out can be usb_midi.ports[0/1]
    # in_channel can be None for all, int, or tuple
    # out_channel defaults to 0 (UI 1)
    midi = MIDI(midi_in=uart, debug=debug)
    # Convert channel numbers at the presentation layer to the ones musicians use
    print_debug("Default output channel:", midi.out_channel + 1)
    input_chan_str = str(midi.in_channel)
    if isinstance(midi.in_channel, tuple):
        input_chan_str = str([x + 1 for x in midi.in_channel])
    print_debug("Listening on input channel:", input_chan_str)


def loop(idx):
    global debug
    global pulsing
    global pulse_check_millis
    global pulse_check
    global pulse_led_millis
    global pulse_led
    global midi
    global led
    global sync
    # spin internal LED around! autoshow is on
    # dot[0] = wheel(i & 255)

    # set analog output to 0-3.3V (0-65535 in increments)
    # aout.value = i * 256

    # Read analog voltage on D0
    # print("D0: %0.2f" % getVoltage(analog1in))

    # use D3 as capacitive touch to turn on internal LED
    # if touch.value:
    #     print("D3 touched!")

    # if not button.value:
    #     print("Button on D2 pressed!")

    if pulsing:
        now = time.monotonic() * 1000
        if (now - pulse_check_millis) > pulse_check:
            if pulse_led_millis and (now - pulse_led_millis) > pulse_led:
                led_pin_off()
            if pulse_sync_millis and (now - pulse_sync_millis) > pulse_sync:
                sync_pin_off()
            # TODO gate
            pulse_check_millis = now
    else:
        led.duty_cycle = idx % 2048

    msg_in = midi.receive()  # non-blocking
    if msg_in is not None:
        print_debug("Received:", msg_in, "at", time.monotonic())
        if isinstance(msg_in, TimingClock):
            if pulsing:
                print_debug("TimingClock")
                handle_clock()
        elif isinstance(msg_in, Start):
            print_debug("Start")
            handle_start()
        elif isinstance(msg_in, Stop):
            print_debug("Stop")
            handle_stop()
        elif debug:
            if isinstance(msg_in, MIDIUnknownEvent):
                # Message are only known if they are imported
                print_debug("Unknown MIDI event status ", msg_in.status)
            else:
                print_debug("Shouldn't be here, unhandled message")


def handle_clock():
    global pulsing
    global clock_ticks
    # 0xf8 system real time clock
    if not pulsing:
        return
    # Assume 4/4 time, so a beat is a quarter note
    # There are 24 MIDI clocks per beat, so 24 MIDI PPQN
    # PO and Volca use 2 PPQN, so 12 MIDI pulses per 1 PO pulse
    if (clock_ticks % 12) == 0:
        #                   0         1         2
        # clock_ticks (in)  0123456789012345678901234
        # call pulse?       ynnnnnnnnnnnynnnnnnnnnnny
        # clock_ticks (out) 1234567890123456789012341
        # pulse_half_beat()
        led_pin_on(level=0.001)
        # v-trig PO sync
        sync_pin_on()
    # http://lauterzeit.com/arp_lfo_seq_sync/
    if (clock_ticks % 24) == 0:
        # avoid eventual overflow
        clock_ticks = 0
        #                   0         1         2
        # clock_ticks (in)  0123456789012345678901234
        # call pulse?       ynnnnnnnnnnnnnnnnnnnnnnny
        # clock_ticks (out) 1234567890123456789012341
        # pulse_beat()
        led_pin_on(level=0.2)
        # s-trig LB sync
        # lb_pin_low()
    clock_ticks += 1


def led_pin_on(level=0.5):
    # give a visual indication that a pulse is happening
    global led
    global pulse_led_millis
    # led.value = 65535 * level
    led.duty_cycle = int(65535 * level)
    pulse_led_millis = time.monotonic() * 1000


def led_pin_off():
    # polled each loop, if the pulse has been high long enough, turn it off
    global led
    global pulse_led_millis
    # led.value = 0
    led.duty_cycle = 0
    pulse_led_millis = 0


def sync_pin_on():
    # send a pulse on the PO/Volca/Analogue Sync out pin
    global sync
    global pulse_sync_millis
    sync.value = True
    pulse_sync_millis = time.monotonic() * 1000


def sync_pin_off():
    # polled each loop, if the pulse has been high long enough, turn it off
    global sync
    global pulse_sync_millis
    sync.value = False
    pulse_sync_millis = 0


def handle_start():
    # 0xfa It's uncertain if there should be a pulse on start
    # or only the 1st clock after a start
    # this logic assumes the 1st pulse is the 1st clock after a start
    global clock_ticks
    global pulsing
    clock_ticks = 0
    pulsing = True
    # digitalWrite(OUT_D_LB_SYNC, HIGH);
    # LB_STRING_MILLIS = 0;
    # // notes/cv


def handle_stop():
    # 0xfe clock is going, but ignore it until started again
    # allow manual/gated key presses through
    global pulsing
    pulsing = False
    # digitalWrite(OUT_D_LB_SYNC, LOW);
    # LB_STRING_MILLIS = 0;
    # // notes/cv
    # TODO turn off any ungated CV


# Boiler organization
def main():
    idx = 0
    while True:
        loop(idx)
        idx = (idx + 1) % 65536


setup()
main()
