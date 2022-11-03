# MIDI to Pocket Operator Sync (a form of Analogue Sync)
"""MIDI to Pocket Operator Sync (a form of Analogue Sync).
See README.md and midi-to-po-analogue-sync.ino for more docs

- verify current meets requirements, 1 led blink, then dotstar color
- port code from ino
 * read MIDI from d0
 *   v0 is to blink clock sync on LED on ?d1? (built-in LED), then only blink per beat
 *   v1 output PO on ?d1?
 *   v2 output LB sync on ?d3?
 *   v3 blink gate (note on/off) on LED on ?d4?
 *   v4 output gated, latest CV on ?d4?
"""
import board
import busio
import pwmio
# from digitalio import DigitalInOut, Direction  # , Pull
# from analogio import AnalogOut  # , AnalogIn
# import touchio
# from adafruit_hid.keyboard import Keyboard
# from adafruit_hid.keycode import Keycode
# import adafruit_dotstar as dotstar
import time
import adafruit_midi
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


led = None
midi = None
clock_ticks = 0
pulsing = False
pulse_check = 5
pulse_check_millis = 0
pulse_led = 15
pulse_led_millis = 0
pulse_sync = 15
pulse_sync_millis = 0
pulse_gate = 15
pulse_gate_millis = 0


def setup():
    global led
    global midi
    # One pixel connected internally!
    # dot = dotstar.DotStar(board.APA102_SCK, board.APA102_MOSI, 1, brightness=0.2)

    # Built in red LED
    # led = DigitalOut(board.D13)
    # led.direction = Direction.OUTPUT
    # led.value = 0
    led = pwmio.PWMOut(board.LED, frequency=5000, duty_cycle=0)

    # Digital input with pullup on D2
    # button = DigitalInOut(board.D2)
    # button.direction = Direction.INPUT
    # button.pull = Pull.UP

    # din_midi = DigitalInOut(board.D0)
    # https://github.com/BlitzCityDIY/midi_uart_experiments/blob/main/CircuitPython/1-23-22_midiUartOut.py
    # Also see "Receive MIDI Over UART and Send Over USB"
    # at https://learn.adafruit.com/midi-for-makers?view=all#midi-messages
    # https://docs.circuitpython.org/en/latest/shared-bindings/busio/#busio.UART
    uart = busio.UART(None, board.D0, baudrate=31250, timeout=0.001)  # init UART
    # print(usb_midi.ports)
    # midi_in/out can be usb_midi.ports[0/1]
    # in_channel can be None for all, int, or tuple
    # out_channel defaults to 0 (UI 1)
    midi = adafruit_midi.MIDI(midi_in=uart)  # , debug=True)
    # Convert channel numbers at the presentation layer to the ones musicians use
    print("Default output channel:", midi.out_channel + 1)
    input_chan_str = str(midi.in_channel)
    if isinstance(midi.in_channel, tuple):
        input_chan_str = str([x + 1 for x in midi.in_channel])
    print("Listening on input channel:", input_chan_str)


# HELPERS ##############################

# Helper to convert analog input to voltage
def getVoltage(pin):
    return (pin.value * 3.3) / 65536

# Helper to give us a nice color swirl
def wheel(pos):
    # Input a value 0 to 255 to get a color value.
    # The colours are a transition r - g - b - back to r.
    if (pos < 0):
        return (0, 0, 0)
    if (pos > 255):
        return (0, 0, 0)
    if (pos < 85):
        return (int(pos * 3), int(255 - (pos*3)), 0)
    elif (pos < 170):
        pos -= 85
        return (int(255 - pos*3), 0, int(pos*3))
    else:
        pos -= 170
        return (0, int(pos*3), int(255 - pos*3))

# MAIN LOOP ##############################


def loop(idx):
    global pulsing
    global pulse_check_millis
    global pulse_check
    global pulse_led_millis
    global pulse_led
    global midi
    global led
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

    # if (i % 16) < 8:
    #     led.value = False
    # else:
    #     led.value = True
    # i = (i+1) % 256  # run from 0 to 255
    # time.sleep(0.1)
    if pulsing:
        now = time.monotonic() * 1000
        if (now - pulse_check_millis) > pulse_check:
            if pulse_led_millis and (now - pulse_led_millis) > pulse_led:
                led_pin_off()
            # TODO sync, gate
            pulse_check_millis = now
    else:
        led.duty_cycle = idx % 2048

    msg_in = midi.receive()  # non-blocking
    if msg_in is not None:
        print("Received:", msg_in, "at", time.monotonic())
        if isinstance(msg_in, TimingClock):
            if pulsing:
                print("TimingClock")
            handle_clock()
        elif isinstance(msg_in, Start):
            print("Start")
            handle_start()
        elif isinstance(msg_in, Stop):
            print("Stop")
            handle_stop()
        elif isinstance(msg_in, MIDIUnknownEvent):
            # Message are only known if they are imported
            print("Unknown MIDI event status ", msg_in.status)
        else:
            print("Shouldn't be here, unhandled message")


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
        pulse_half_beat()
    # http://lauterzeit.com/arp_lfo_seq_sync/
    if (clock_ticks % 24) == 0:
        # avoid eventual overflow
        clock_ticks = 0
        #                   0         1         2
        # clock_ticks (in)  0123456789012345678901234
        # call pulse?       ynnnnnnnnnnnnnnnnnnnnnnny
        # clock_ticks (out) 1234567890123456789012341
        pulse_beat()
    clock_ticks += 1


def pulse_beat():
    # called once per 24 MIDI clocks (once per beat)
    led_pin_on()
    # s-trig LB sync
    # lb_pin_low()


def pulse_half_beat():
    # called once per 12 MIDI clocks (half a beat)
    led_pin_on(dim=True)
    # sync_pin_on()


def led_pin_on(dim=False, bright=False):
    global led
    global pulse_led_millis
    level = 0.5
    if dim:  # non-beat sync
        level = 0.01
    if bright:  # start/stop
        level = 1
    # led.value = 65535 * level
    led.duty_cycle = int(65535 * level)
    pulse_led_millis = time.monotonic() * 1000


def led_pin_off():
    global led
    global pulse_led_millis
    # led.value = 0
    led.duty_cycle = 0
    pulse_led_millis = 0


def handle_start():
    # 0xfa It's uncertain if there should be a pulse on start, or only the 1st clock after a start
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
