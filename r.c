#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <AL/al.h>
#include <AL/alc.h>

// Configuration
#define CHUNK 512
#define RATE 44100
#define BUFFER_SIZE (1024 * 10)

// Resonance Controls
float PITCH_SAMPLES = 800.0f; 
float RESONANCE_DECAY = 0.98f;
float DAMPING = 0.5f;
float VOLUME = 0.8f; // New Volume Control

float feedback_buffer[BUFFER_SIZE] = {0};
int write_ptr = 0;

void process_audio(short* input, short* output, int count) {
    for (int i = 0; i < count; i++) {
        // 1. Calculate Read Pointer (Fractional)
        float read_ptr = (float)write_ptr - PITCH_SAMPLES;
        while (read_ptr < 0) read_ptr += BUFFER_SIZE;
        
        // 2. Linear Interpolation
        int i0 = (int)read_ptr % BUFFER_SIZE;
        int i1 = (i0 + 1) % BUFFER_SIZE;
        float frac = read_ptr - (int)read_ptr;
        float delayed_sample = (1.0f - frac) * feedback_buffer[i0] + frac * feedback_buffer[i1];

        // 3. Apply Damping and Feedback
        float input_f = (float)input[i];
        float val_to_write = input_f + (delayed_sample * RESONANCE_DECAY);
        
        // 1-pole Low Pass
        int prev_ptr = (write_ptr - 1 + BUFFER_SIZE) % BUFFER_SIZE;
        feedback_buffer[write_ptr] = (1.0f - DAMPING) * val_to_write + DAMPING * feedback_buffer[prev_ptr];

        // 4. Volume and Clipping
        float final_val = delayed_sample * VOLUME;
        if (final_val > 32767.0f) final_val = 32767.0f;
        if (final_val < -32768.0f) final_val = -32768.0f;
        
        output[i] = (short)final_val;
        write_ptr = (write_ptr + 1) % BUFFER_SIZE;
    }
}

int main() {
    // Initialize OpenAL Playback
    ALCdevice *play_dev = alcOpenDevice(NULL);
    ALCcontext *ctx = alcCreateContext(play_dev, NULL);
    alcMakeContextCurrent(ctx);

    // Initialize OpenAL Capture (Input)
    ALCdevice *cap_dev = alcCaptureOpenDevice(NULL, RATE, AL_FORMAT_MONO16, CHUNK * 2);
    alcCaptureStart(cap_dev);

    ALuint source;
    alGenSources(1, &source);

    // Buffers for queuing
    ALuint buffers[3];
    alGenBuffers(3, buffers);

    short input_data[CHUNK];
    short output_data[CHUNK];

    printf("* Resonance active. Press Enter to stop...\n");

    while (1) {
        ALint samples_available;
        alcGetIntegerv(cap_dev, ALC_CAPTURE_SAMPLES, 1, &samples_available);

        if (samples_available >= CHUNK) {
            alcCaptureSamples(cap_dev, input_data, CHUNK);
            
            process_audio(input_data, output_data, CHUNK);

            // OpenAL Queuing logic
            ALint processed;
            alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

            ALuint buffer_id;
            if (processed > 0) {
                alSourceUnqueueBuffers(source, 1, &buffer_id);
            } else {
                // Initial fill if we have empty buffers left
                static int initial_fill = 0;
                if (initial_fill < 3) buffer_id = buffers[initial_fill++];
                else continue; // Wait for a buffer to become processed
            }

            alBufferData(buffer_id, AL_FORMAT_MONO16, output_data, CHUNK * sizeof(short), RATE);
            alSourceQueueBuffers(source, 1, &buffer_id);

            // Keep playing if stopped
            ALint state;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) alSourcePlay(source);
        }
    }

    // Cleanup
    alcCaptureStop(cap_dev);
    alcCaptureCloseDevice(cap_dev);
    alDeleteSources(1, &source);
    alDeleteBuffers(3, buffers);
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(play_dev);

    return 0;
}
