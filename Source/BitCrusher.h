#pragma once
#include <JuceHeader.h>

struct BitCrusher {
    void prepare (double sampleRate, int samplesPerBlock, int channels) {
        sr = sampleRate; downsampleCounter = 0; held.resize((size_t) channels, 0.0f);
    }

    void setParams (float bitDepth, int downsampleFactor, float mix) {
        bits = juce::jlimit(1.0f, 24.0f, bitDepth);
        dsFactor = juce::jmax(1, downsampleFactor);
        wet = juce::jlimit(0.0f, 1.0f, mix);
        step = std::pow(2.0f, bits) - 1.0f;
    }

    void process (juce::AudioBuffer<float>& buf) {
        const int chs = buf.getNumChannels();
        const int N = buf.getNumSamples();
        for (int ch = 0; ch < chs; ++ch) {
            auto* x = buf.getWritePointer(ch);
            for (int n = 0; n < N; ++n) {
                if ((downsampleCounter++ % dsFactor) == 0) {
                    float q = std::round((x[n] * 0.5f + 0.5f) * step) / step; // quantize 0..1
                    held[(size_t) ch] = (q * 2.0f - 1.0f);                    // back to -1..1
                }
                x[n] = juce::jlimit(-1.0f, 1.0f, juce::jmix(x[n], held[(size_t) ch], wet));
            }
        }
    }

    double sr = 44100.0; int dsFactor = 1; int downsampleCounter = 0; float bits = 16.0f; float wet = 0.0f; float step = 65535.0f;
    std::vector<float> held;
};
