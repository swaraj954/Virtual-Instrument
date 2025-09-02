#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================

DualOscSynthAudioProcessor::DualOscSynthAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                      #if ! JucePlugin_IsMidiEffect
                       #if ! JucePlugin_IsSynth
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       #endif
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                      #endif
                       ),
       parameters (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    // Attach parameter pointers
    osc1WaveParam   = parameters.getRawParameterValue("OSC1WAVE");
    osc2WaveParam   = parameters.getRawParameterValue("OSC2WAVE");
    osc1PitchParam  = parameters.getRawParameterValue("OSC1PITCH");
    osc2PitchParam  = parameters.getRawParameterValue("OSC2PITCH");
    unisonParam     = parameters.getRawParameterValue("UNISON");

    ampADSRParams.attack  = parameters.getRawParameterValue("AMPATTACK");
    ampADSRParams.decay   = parameters.getRawParameterValue("AMPDECAY");
    ampADSRParams.sustain = parameters.getRawParameterValue("AMPSUSTAIN");
    ampADSRParams.release = parameters.getRawParameterValue("AMPRELEASE");

    filterADSRParams.attack  = parameters.getRawParameterValue("FILATTACK");
    filterADSRParams.decay   = parameters.getRawParameterValue("FILDECAY");
    filterADSRParams.sustain = parameters.getRawParameterValue("FILSUSTAIN");
    filterADSRParams.release = parameters.getRawParameterValue("FILRELEASE");

    lfoRateParam     = parameters.getRawParameterValue("LFORATE");
    lfoDepthParam    = parameters.getRawParameterValue("LFODEPTH");
    lfoDestParam     = parameters.getRawParameterValue("LFODEST");

    bendParam        = parameters.getRawParameterValue("BEND");
    crushParam       = parameters.getRawParameterValue("CRUSH");
    chorusMixParam   = parameters.getRawParameterValue("CHORUSMIX");
    reverbMixParam   = parameters.getRawParameterValue("REVERBMIX");

    // Prepare DSP objects
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = getSampleRate();
    spec.maximumBlockSize = getBlockSize();
    spec.numChannels = getTotalNumOutputChannels();

    chorus.prepare(spec);
    reverb.prepare(spec);

    // Default reverb settings
    juce::dsp::Reverb::Parameters reverbParams;
    reverbParams.roomSize = 0.5f;
    reverbParams.wetLevel = 0.3f;
    reverb.setParameters(reverbParams);
}

DualOscSynthAudioProcessor::~DualOscSynthAudioProcessor() {}

//==============================================================================

void DualOscSynthAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    chorus.reset();
    reverb.reset();
}

void DualOscSynthAudioProcessor::releaseResources() {}

void DualOscSynthAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

   
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* channelData = buffer.getWritePointer(channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            float time = (float) sample / (float) getSampleRate();

            // Compute base phase
            float osc1Phase = fmod(time * 440.0f * pow(2.0f, *osc1PitchParam / 12.0f), 1.0f);
            float osc2Phase = fmod(time * 220.0f * pow(2.0f, *osc2PitchParam / 12.0f), 1.0f);

            float sampleValue = 0.0f;
            sampleValue += renderOscillator(*osc1WaveParam, osc1Phase);
            sampleValue += renderOscillator(*osc2WaveParam, osc2Phase);

            // Unison detune
            if (*unisonParam > 0.5f)
            {
                float detunePhase = fmod(time * 441.0f, 1.0f);
                sampleValue += 0.5f * renderOscillator(*osc1WaveParam, detunePhase);
            }

            // Apply wave bending
            sampleValue = std::tanh(sampleValue * (1.0f + *bendParam * 5.0f));

            // Bitcrusher
            float step = pow(2.0f, 8.0f - (*crushParam * 7.0f)); 
            sampleValue = floor(sampleValue * step) / step;

            channelData[sample] = sampleValue;
        }
    }

   
    juce::dsp::AudioBlock<float> block(buffer);
    chorus.process(juce::dsp::ProcessContextReplacing<float>(block));

    
    reverb.process(juce::dsp::ProcessContextReplacing<float>(block));
}

//==============================================================================

float DualOscSynthAudioProcessor::renderOscillator(float waveType, float phase)
{
    if (waveType < 0.25f)    
        return std::sin(2.0f * juce::MathConstants<float>::pi * phase);
    else if (waveType < 0.5f)  // Saw
        return 2.0f * phase - 1.0f;
    else if (waveType < 0.75f) 
        return (phase < 0.5f) ? 1.0f : -1.0f;
    else                       
        return 2.0f * fabs(2.0f * phase - 1.0f) - 1.0f;
}

//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout DualOscSynthAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

   
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OSC1WAVE", "Osc1 Wave", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OSC2WAVE", "Osc2 Wave", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OSC1PITCH", "Osc1 Pitch", -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("OSC2PITCH", "Osc2 Pitch", -24.0f, 24.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("UNISON", "Unison", false));

   
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AMPATTACK", "Amp Attack", 0.01f, 5.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AMPDECAY", "Amp Decay", 0.01f, 5.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AMPSUSTAIN", "Amp Sustain", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("AMPRELEASE", "Amp Release", 0.01f, 5.0f, 0.3f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILATTACK", "Filter Attack", 0.01f, 5.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILDECAY", "Filter Decay", 0.01f, 5.0f, 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILSUSTAIN", "Filter Sustain", 0.0f, 1.0f, 0.8f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("FILRELEASE", "Filter Release", 0.01f, 5.0f, 0.3f));

   
    params.push_back(std::make_unique<juce::AudioParameterFloat>("LFORATE", "LFO Rate", 0.1f, 20.0f, 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("LFODEPTH", "LFO Depth", 0.0f, 1.0f, 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("LFODEST", "LFO Destination",
                                                                  juce::StringArray { "Pitch", "Filter", "Mix", "Volume" }, 0));

    
    params.push_back(std::make_unique<juce::AudioParameterFloat>("BEND", "Wave Bend", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CRUSH", "Bit Crush", 0.0f, 1.0f, 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("CHORUSMIX", "Chorus Mix", 0.0f, 1.0f, 0.3f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("REVERBMIX", "Reverb Mix", 0.0f, 1.0f, 0.3f));

    return { params.begin(), params.end() };
}



bool DualOscSynthAudioProcessor::hasEditor() const { return true; }
juce::AudioProcessorEditor* DualOscSynthAudioProcessor::createEditor() { return new DualOscSynthAudioProcessorEditor (*this); }



const juce::String DualOscSynthAudioProcessor::getName() const { return JucePlugin_Name; }

bool DualOscSynthAudioProcessor::acceptsMidi() const { return true; }
bool DualOscSynthAudioProcessor::producesMidi() const { return false; }
bool DualOscSynthAudioProcessor::isMidiEffect() const { return false; }
double DualOscSynthAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int DualOscSynthAudioProcessor::getNumPrograms() { return 1; }
int DualOscSynthAudioProcessor::getCurrentProgram() { return 0; }
void DualOscSynthAudioProcessor::setCurrentProgram (int) {}
const juce::String DualOscSynthAudioProcessor::getProgramName (int) { return {}; }
void DualOscSynthAudioProcessor::changeProgramName (int, const juce::String&) {}

void DualOscSynthAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream(destData, true).writeString(parameters.state.toXmlString());
}

void DualOscSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = juce::parseXML(juce::String::createStringFromData(data, sizeInBytes));
    if (xml != nullptr)
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}
