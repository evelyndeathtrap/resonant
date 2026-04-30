#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <AL/al.h>
#include <AL/alc.h>

// --- Configuration ---
#define CHUNK 512
#define RATE 44100
#define BUFFER_SIZE (1024 * 10)

// --- Resonance Controls ---
float PITCH_SAMPLES = 800.0f; 
float RESONANCE_DECAY = 0.98f;
float DAMPING = 0.5f;

// --- AGC (Automatic Gain Control) Controls ---
float TARGET_RMS = 0.15f;     // Desired average volume (0.0 to 1.0)
float AGC_SPEED = 0.005f;     // How fast the volume adjusts per chunk
float current_gain = 0.5f;    // Starting gain multiplier
float MAX_GAIN = 4.0f;        // Don't boost silence too much

// --- Global Audio State ---
float feedback_buffer[BUFFER_SIZE] = {0};
int write_ptr = 0;

void process_audio(short* input, short* output, int count) {
    float sum_sq = 0;

    for (int i = 0; i < count; i++) {
        // 1. Calculate Fractional Read Pointer
        float read_ptr = (float)write_ptr - PITCH_SAMPLES;
        while (read_ptr < 0) read_ptr += BUFFER_SIZE;
        
        int i0 = (int)read_ptr % BUFFER_SIZE;
        int i1 = (i0 + 1) % BUFFER_SIZE;
        float frac = read_ptr - (int)read_ptr;
        
        // Linear Interpolation for smooth ringing
        float delayed_sample = (1.0f - frac) * feedback_buffer[i0] + frac * feedback_buffer[i1];

        // 2. Feedback Loop with 1-pole Low Pass (Damping)
        float input_f = (float)input[i];
        float val_to_write = input_f + (delayed_sample * RESONANCE_DECAY);
        
        int prev_ptr = (write_ptr - 1 + BUFFER_SIZE) % BUFFER_SIZE;
        feedback_buffer[write_ptr] = (1.0f - DAMPING) * val_to_write + DAMPING * feedback_buffer[prev_ptr];

        // 3. Track Signal Energy (for AGC)
        // We normalize to -1.0 to 1.0 range for RMS calculation
        float sample_norm = delayed_sample / 32768.0f;
        sum_sq += sample_norm * sample_norm;

        // 4. Apply Gain and Clip
        float final_val = delayed_sample * current_gain;
        
        if (final_val > 32767.0f) final_val = 32767.0f;
        if (final_val < -32768.0f) final_val = -32768.0f;
        
        output[i] = (short)final_val;
        write_ptr = (write_ptr + 1) % BUFFER_SIZE;
    }

    // 5. Update AGC Gain for the next chunk
    float rms = sqrtf(sum_sq / count);
    if (rms > 0.0001f) { // Only adjust if there's actually sound
        if (rms < TARGET_RMS) {
            current_gain += AGC_SPEED;
        } else if (rms > TARGET_RMS) {
            current_gain -= AGC_SPEED;
        }
    }

    // Keep gain in a safe range
    if (current_gain < 0.05f) current_gain = 0.05f;
    if (current_gain > MAX_GAIN) current_gain = MAX_GAIN;
}

int main() {
    // --- Setup OpenAL Playback ---
    ALCdevice *play_dev = alcOpenDevice(NULL);
    if (!play_dev) return 1;
    ALCcontext *ctx = alcCreateContext(play_dev, NULL);
    alcMakeContextCurrent(ctx);

    // --- Setup OpenAL Capture (Microphone) ---
    ALCdevice *cap_dev = alcCaptureOpenDevice(NULL, RATE, AL_FORMAT_MONO16, CHUNK * 4);
    if (!cap_dev) return 1;
    alcCaptureStart(cap_dev);

    // Generate Source and Buffers
    ALuint source;
    alGenSources(1, &source);
    ALuint buffers[3];
    alGenBuffers(3, buffers);

    short input_data[CHUNK];
    short output_data[CHUNK];
    int buffers_filled = 0;

    printf("* Resonance active with AGC. Target RMS: %.2f\n", TARGET_RMS);
    printf("* Press Ctrl+C to stop.\n");

    while (1) {
        ALint samples_available;
        alcGetIntegerv(cap_dev, ALC_CAPTURE_SAMPLES, 1, &samples_available);

        if (samples_available >= CHUNK) {
            // Grab mic data
            alcCaptureSamples(cap_dev, input_data, CHUNK);
            
            // Apply effect and AGC
            process_audio(input_data, output_data, CHUNK);

            ALint processed;
            alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

            ALuint buffer_id;
            if (processed > 0) {
                alSourceUnqueueBuffers(source, 1, &buffer_id);
            } else if (buffers_filled < 3) {
                buffer_id = buffers[buffers_filled++];
            } else {
                // Wait for hardware to finish playing a buffer
                continue; 
            }

            alBufferData(buffer_id, AL_FORMAT_MONO16, output_data, CHUNK * sizeof(short), RATE);
            alSourceQueueBuffers(source, 1, &buffer_id);

            // Start playing if we stopped (underflow) or just started
            ALint state;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) alSourcePlay(source);
        }
    }

    // --- Cleanup ---
    alcCaptureStop(cap_dev);
    alcCaptureCloseDevice(cap_dev);
    alDeleteSources(1, &source);
    alDeleteBuffers(3, buffers);
    alcDestroyContext(ctx);
    alcCloseDevice(play_dev);

    return 0;
}
