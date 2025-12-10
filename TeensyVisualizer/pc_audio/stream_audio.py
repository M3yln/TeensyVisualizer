# stream_audio.py
import sounddevice as sd
import serial
import numpy as np
import time

SERIAL_PORT = 'COM3'  # update as needed
BAUD_RATE = 115200
AUDIO_SAMPLE_RATE = 44100
AUDIO_BLOCK_SIZE = 1024   # must be a multiple of DISPLAY_WIDTH
SERIAL_TIMEOUT = 0.01
SENSITIVITY = 0.5        # starting sensitivity 
DISPLAY_WIDTH = 128       # must match Teensy display width
ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=SERIAL_TIMEOUT)

current_mode = 1  # 0 = bargraph, 1 = waveform

# --- packet send helpers (tags are 4 ASCII bytes) ---
def send_waveform(peaks, troughs):
    # peaks & troughs are uint8 arrays length 128
    payload = bytearray()
    for p, t in zip(peaks, troughs):
        payload.append(int(p))
        payload.append(int(t))
    try:
        ser.write(b"WAVE" + payload)
    except Exception as e:
        print("Serial write fail (WAVE):", e)
        pass

def send_bargraph(vol):
    try:
        ser.write(b"BAR " + bytes([int(vol)]))
    except Exception:
        pass

def send_pot(val):
    try:
        ser.write(b"POT " + bytes([int(val)]))
    except Exception:
        pass

def send_mode(mode):
    try:
        ser.write(b"MODE" + bytes([int(mode)]))
    except Exception:
        pass

# --- audio callback: single callback, chooses based on current_mode ---
def audio_callback(indata, frames, time_info, status):
    global current_mode, SENSITIVITY
    # keep everything float32 in range -1..1 (sounddevice usually gives -1..1)
    samples = indata[:, 0].astype(np.float32)

    if current_mode == 0:
        # bargraph mode: use RMS
        rms = np.sqrt(np.mean(samples * samples))
        volume_byte = int(np.clip(rms * 255 * SENSITIVITY, 0, 255))
        send_bargraph(volume_byte)
        # debug:
        print("Sent BAR", volume_byte)
    else:
        # waveform mode: compute peaks/troughs per column
        # amplified then clipped
        amplified = samples * SENSITIVITY
        amplified = np.clip(amplified, -1.0, 1.0)

        # ensure frames divides evenly into DISPLAY_WIDTH
        # grouped shape -> (DISPLAY_WIDTH, samples_per_col)
        try:
            grouped = amplified.reshape(DISPLAY_WIDTH, -1)
        except Exception:
            # fallback: if blocksize doesn't match, compute groups manually
            chunk = len(amplified) // DISPLAY_WIDTH
            if chunk < 1:
                # not enough data to form groups; skip
                return
            grouped = amplified[:DISPLAY_WIDTH*chunk].reshape(DISPLAY_WIDTH, chunk)

        peaks = grouped.max(axis=1)
        troughs = grouped.min(axis=1)

        # map -1..1 -> 0..255
        peaks_u = ((peaks + 1.0) * 127.5).astype(np.uint8)
        troughs_u = ((troughs + 1.0) * 127.5).astype(np.uint8)

        send_waveform(peaks_u, troughs_u)
        # debug:
        print("Sent WAVE")


# def audio_callback(indata, frames, time_info, status):
#     s = indata[:,0].astype(np.float32)
#     print("Max:", np.max(s), "Min:", np.min(s), "RMS:", np.sqrt(np.mean(s*s)))

# --- open audio stream once and keep it running ---
stream = sd.InputStream(
    samplerate=AUDIO_SAMPLE_RATE,
    channels=1,
    blocksize=AUDIO_BLOCK_SIZE,
    callback=audio_callback
)

print("Starting audio stream... (CTRL-C to exit)")
with stream:
    try:
        # main loop: read serial messages from Teensy (POT / MODE)
        # use a small sleep to avoid burning CPU
        while True:
            # if there are at least 4 bytes, treat them as a tag
            if ser.in_waiting >= 4:
                tag = ser.read(4)
                if tag == b"POT ":
                    # expect 1 byte
                    while ser.in_waiting < 1:
                        time.sleep(0.001)
                    val = ord(ser.read(1))
                    # original behavior: SENSITIVITY = pot / 32
                    # keep previous scaling: pot 0..255 -> sensitivity
                    try:
                        SENSITIVITY = max(0.1, val / 32.0)
                    except Exception:
                        pass
                    # print("New sensitivity from Teensy:", SENSITIVITY)
                elif tag == b"MODE":
                    while ser.in_waiting < 1:
                        time.sleep(0.001)
                    mode = ord(ser.read(1))
                    current_mode = int(mode)
                    print(f"Switched to {'bargraph' if mode == 0 else 'waveform'} mode (from Teensy)")
                else:
                    # Unknown tag from Teensy. If Teensy ever sends other info, handle here.
                    # read and discard a single byte to avoid lock (optional)
                    # If unknown tag is expected to have payload, you can extend protocol.
                    # For now, ignore.
                    pass
            else:
                time.sleep(0.005)
    except KeyboardInterrupt:
        print("Exiting")
    except Exception as e:
        print("Exception in main loop:", e)
