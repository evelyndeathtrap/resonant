import pyaudio
import numpy as np

# Configuration
CHUNK = 512
RATE = 44100

# Resonance Controls
# Tweak these to change the "material" of the resonance
PITCH_SAMPLES = 800.0  # Lower = higher ringing pitch (can be a float!)
RESONANCE_DECAY = 0.98 # 0.0 (dead) to 0.99 (infinite ring)
DAMPING = 0.5          # 0.0 (metallic) to 0.5 (woody/soft)

p = pyaudio.PyAudio()

# We need a buffer larger than our chunk to handle the feedback
buffer_size = 1024 * 10
feedback_buffer = np.zeros(buffer_size, dtype=np.float32)
write_ptr = 0

stream = p.open(format=pyaudio.paInt16, channels=1, rate=RATE,
                input=True, output=True, frames_per_buffer=CHUNK)

print("* Resonance improved. Feedback loop optimized for harmonic ringing...")

try:
    while True:
        raw_data = stream.read(CHUNK, exception_on_overflow=False)
        input_samples = np.frombuffer(raw_data, dtype=np.int16).astype(np.float32)
        output_samples = np.zeros_like(input_samples)

        for i in range(CHUNK):
            # 1. Calculate Read Pointer (with fractional offset for "tuning")
            read_ptr = (write_ptr - PITCH_SAMPLES) % buffer_size
            
            # 2. Linear Interpolation (The "Smoothness" secret)
            i0 = int(read_ptr)
            i1 = (i0 + 1) % buffer_size
            frac = read_ptr - i0
            delayed_sample = (1 - frac) * feedback_buffer[i0] + frac * feedback_buffer[i1]
            
            # 3. Apply Damping (Low-pass filter in the loop)
            # This makes the resonance sound natural rather than "digital"
            val_to_write = input_samples[i] + (delayed_sample * RESONANCE_DECAY)
            
            # Simple 1-pole low pass
            feedback_buffer[write_ptr] = (1 - DAMPING) * val_to_write + DAMPING * feedback_buffer[(write_ptr - 1) % buffer_size]
            
            output_samples[i] = delayed_sample
            write_ptr = (write_ptr + 1) % buffer_size

        # Output with slight gain boost
        final_output = np.clip(output_samples * 0.8, -32768, 32767).astype(np.int16)
        stream.write(final_output.tobytes())

except KeyboardInterrupt:
    print("* Stopped.")

stream.stop_stream()
stream.close()
p.terminate()
