#pragma once
#include <JuceHeader.h>
#include "LFO.h"


struct MorphOsc {
    void prepare (double sampleRate) { sr = sampleRate; }
    void setFreq (float f) { freq = f; }
    void setShape (float s01) { shape = juce::jlimit(0.0f, 1.0f, s01); }
    void reset() { phase = 0.0f; }
    inline float process() {
        phase += freq / (float) sr; if (phase >= 1.0f) phase -= 1.0f;
        float s = std::sin (juce::MathConstants<float>::twoPi * phase);
        float saw = 2.0f * phase - 1.0f;
        float square = (phase < 0.5f ? 1.0f : -1.0f);
        float mix1 = juce::jmap(shape, 0.0f, 0.5f, 0.0f, 1.0f);    // 0..0.5: sine->saw
        mix1 = juce::jlimit(0.0f, 1.0f, mix1);
        float a = juce::jmap(mix1, 0.0f, 1.0f, s, saw);
        float mix2 = juce::jmap(shape, 0.5f, 1.0f, 0.0f, 1.0f);     // 0.5..1: saw->square
        mix2 = juce::jlimit(0.0f, 1.0f, mix2);
        return juce::jmap(mix2, 0.0f, 1.0f, a, square);
    }
    double sr { 44100.0 }; float phase { 0.0f }, freq { 100.0f }, shape { 0.0f };
};

struct SynthVoice : public juce::SynthesiserVoice {
    SynthVoice() {
        filter.state->type = juce::dsp::StateVariableTPTFilterType::lowpass;
    }

    void prepare (const juce::dsp::ProcessSpec& spec) {
        sr = spec.sampleRate;
        filter.prepare (spec);
        chorus.prepare (spec);
        reverb.prepare (spec);
        reset();
        osc1.prepare(sr); osc2.prepare(sr);
    }

    void reset() {
        envAmp.reset (sr); envFilter.reset (sr);
        lfo.reset();
        osc1.reset(); osc2.reset();
        filter.reset();
        chorus.reset();
        reverb.reset();
        lastNote = -1;
    }

    // Parameter slots (wired by processor each block)
    float osc1Shape = 0.0f, osc2Shape = 0.0f;
    float osc1Semi = 0.0f,  osc2Semi = 0.0f;
    int   unison = 1;        // 1 or 3
    float detuneCents = 8.0f;
    float foldDrive = 0.0f;  // 0..1

  
    juce::ADSR envAmp, envFilter;
    juce::ADSR::Parameters ampParams, filtParams;

   
    juce::dsp::StateVariableTPTFilter<float> filter;
    float cutoffHz = 1200.0f, resonance = 0.7f, filterEnvAmt = 0.0f; // +Hz

   
    SimpleLFO lfo; float lfoRate = 5.0f, lfoDepth = 0.0f; int lfoDest = 0; // 0=off

   
    juce::dsp::Chorus<float> chorus; juce::dsp::Reverb reverb; juce::dsp::Reverb::Parameters rv;

    bool canPlaySound (juce::SynthesiserSound* s) override { return dynamic_cast<SynthSound*> (s) != nullptr; }

    void startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override {
        baseHz = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        envAmp.noteOn(); envFilter.noteOn();
        lastNote = midiNoteNumber;
    }

    void stopNote (float, bool allowTailOff) override {
        envAmp.noteOff(); envFilter.noteOff();
        if (! allowTailOff || ! envAmp.isActive()) clearCurrentNote();
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    void renderNextBlock (juce::AudioBuffer<float>& output, int start, int num) override {
        if (lastNote < 0) return;

        // Update time-varying params
        envAmp.setParameters (ampParams);
        envFilter.setParameters (filtParams);
        lfo.set (lfoRate, lfoDepth);
        filter.setCutoffFrequency (cutoffHz);
        filter.setResonance (resonance);

        temp.setSize (output.getNumChannels(), num, false, false, true);
        temp.clear();

        const float lfoToPitch = (lfoDest == 1) ? 1.0f : 0.0f;   // semitones
        const float lfoToAmp   = (lfoDest == 2) ? 1.0f : 0.0f;   // 0..1
        const float lfoToCut   = (lfoDest == 3) ? 1.0f : 0.0f;   // Hz (scaled)
        const float lfoToFold  = (lfoDest == 4) ? 1.0f : 0.0f;   // 0..1
        const float lfoToCrush = (lfoDest == 5) ? 1.0f : 0.0f;   // mix 0..1 (sent back to processor)

        for (int n = 0; n < num; ++n) {
            float l = lfo.process(); // -depth..+depth

            float hz1 = baseHz * juce::MidiMessage::getMidiNoteInHertz(0) ; // dummy to keep IDE happy
            (void) hz1; // unused
            float semiMod = l * 12.0f * lfoToPitch; // up to ±12 st when depth=1
            float f1 = hzFromSemi (baseHz, osc1Semi + semiMod);
            float f2 = hzFromSemi (baseHz, osc2Semi + semiMod);
            osc1.setFreq (f1);
            osc2.setFreq (f2);
            osc1.setShape (osc1Shape);
            osc2.setShape (osc2Shape);

            // Simple unison (1 or 3 voices) per osc
            float s1 = osc1.process();
            float s2 = osc2.process();
            if (unison == 3) {
                float det = centsToRatio (detuneCents);
                // detuned copies
                MorphOsc u1a = osc1; MorphOsc u1b = osc1; u1a.setFreq (f1 * det); u1b.setFreq (f1 / det);
                MorphOsc u2a = osc2; MorphOsc u2b = osc2; u2a.setFreq (f2 * det); u2b.setFreq (f2 / det);
                s1 = 0.3333f * (s1 + u1a.process() + u1b.process());
                s2 = 0.3333f * (s2 + u2a.process() + u2b.process());
            }

            float mixed = 0.5f * (s1 + s2);

           
            float foldAmt = juce::jlimit (0.0f, 1.0f, foldDrive + l * lfoToFold);
            mixed = wavefold (mixed, foldAmt);

            
            float filtEnv = envFilter.getNextSample();
            float cutoffMod = filterEnvAmt * filtEnv + (l * 2000.0f * lfoToCut);
            filter.setCutoffFrequency (juce::jlimit (20.0f, 20000.0f, cutoffHz + cutoffMod));

            float y = filter.processSample (0, mixed); // mono voice, will duplicate to channels

           
            float amp = envAmp.getNextSample();
            if (lfoToAmp > 0.0f) amp = juce::jlimit (0.0f, 1.0f, amp * (0.5f + 0.5f * (l + 1.0f)));

            for (int ch = 0; ch < temp.getNumChannels(); ++ch)
                temp.addSample (ch, n, y * amp);

            // stash optional bitcrush mod mix to first sample of temp chan
