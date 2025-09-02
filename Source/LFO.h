#pragma once
#include <JuceHeader.h>

struct SimpleLFO {
    void prepare (double sampleRate) { sr = sampleRate; }
    void set (float rateHz, float depth01) { rate = rateHz; depth = depth01; }
    void reset() { phase = 0.0f; }
    float process() {
        auto out = std::sin (juce::MathConstants<float>::twoPi * phase);
        phase += rate / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
        return out * depth;
    }
    double sr { 44100.0 }; float phase { 0.0f }, rate { 1.0f }, depth { 0.0f };
};
