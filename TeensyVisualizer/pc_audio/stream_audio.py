import sounddevice as sd
import serial
import numpy as np

SERIAL_PORT = 'COM3'  # Update with your serial port
BAUD_RATE = 115200
AUDIO_SAMPLE_RATE = 44100
AUDIO_BLOCK_SIZE = 1024
SERIAL_TIMEOUT = 1  # seconds
AUDIO_DURATION = 10  # seconds
SENSITIVITY = 10.0  # Adjust sensitivity as needed
DISPLAY_WIDTH = 128
#SERIAL_START_BYTE = b'\xAA'

ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=SERIAL_TIMEOUT)

def audio_callback(indata, frames, time, status):
    
    # # indata[:,0] grabs the first channel (mono)
    # samples = indata[:,0]

    # # Compute RMS (volume)
    # rms = np.sqrt(np.mean(samples**2))

    # # Scale RMS to 0-255 for Teensy
    # volume_byte = int(np.clip(rms * 255 * SENSITIVITY, 0, 255))

    # # Send single byte
    # ser.write(bytes([volume_byte]))

    samples = indata[:,0].astype(np.float32)

    amplified = samples * SENSITIVITY

    # Split into 128 groups
    grouped = amplified.reshape(DISPLAY_WIDTH, -1)

    # Peak detection for each group
    peaks = grouped.max(axis=1)
    troughs = grouped.min(axis=1)

    # Normalize to 0–255
    peaks = ((peaks + 1) * 127.5).astype(np.uint8)
    troughs = ((troughs + 1) * 127.5).astype(np.uint8)

    # Send start byte
    ser.write(b'\xAA')

    # Interleave: [peak0, trough0, peak1, trough1, ...]
    buf = bytearray(DISPLAY_WIDTH * 2)
    for i in range(DISPLAY_WIDTH):
        buf[2*i] = peaks[i]
        buf[2*i+1] = troughs[i]

    ser.write(buf)




stream = sd.InputStream(
    samplerate=AUDIO_SAMPLE_RATE,
    channels=1,
    blocksize=AUDIO_BLOCK_SIZE,
    callback=audio_callback
)

print("Starting audio stream...")

with stream:
    while True:
        while ser.in_waiting >= 2:
            b = ser.read(1)
            if b == b'\xFE': # this packet is for sensitivity
                new_sensitivity = ser.read(1)[0]
                SENSITIVITY = new_sensitivity / 32
                print(f"New sensitivity: {SENSITIVITY}")