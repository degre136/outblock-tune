#include "PluginProcessor.h"

OutblockTuneAudioProcessor::OutblockTuneAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParams())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout OutblockTuneAudioProcessor::createParams()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    juce::StringArray keys { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    juce::StringArray scales { "Chromatic","Major","Minor" };

    p.push_back(std::make_unique<juce::AudioParameterChoice>("key", "Key", keys, 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("scale", "Scale", scales, 2));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("retune", "Retune ms", juce::NormalisableRange<float>(1.0f, 150.0f, 0.1f, 0.4f), 15.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("amount", "Amount", 0.0f, 1.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 1.0f, 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("hard", "Hard Tune", true));

    return { p.begin(), p.end() };
}

void OutblockTuneAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    detector.prepare(sampleRate);
    shifter.prepare(sampleRate, getTotalNumOutputChannels());
}

bool OutblockTuneAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
}

float OutblockTuneAudioProcessor::midiToHz(float midi)
{
    return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f);
}

float OutblockTuneAudioProcessor::hzToMidi(float hz)
{
    return 69.0f + 12.0f * std::log2(hz / 440.0f);
}

int OutblockTuneAudioProcessor::quantizeMidi(int midi, int key, int scale)
{
    if (scale == 0) return midi;

    static constexpr int major[] = {0,2,4,5,7,9,11};
    static constexpr int minor[] = {0,2,3,5,7,8,10};
    const int* intervals = scale == 1 ? major : minor;

    int best = midi;
    int bestDist = 999;

    for (int candidate = midi - 6; candidate <= midi + 6; ++candidate)
    {
        int pc = ((candidate - key) % 12 + 12) % 12;
        bool allowed = false;
        for (int i = 0; i < 7; ++i)
            if (pc == intervals[i]) { allowed = true; break; }

        if (allowed)
        {
            const int dist = std::abs(candidate - midi);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = candidate;
            }
        }
    }
    return best;
}

void OutblockTuneAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    juce::AudioBuffer<float> dry;
    dry.makeCopyOf(buffer, true);

    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() > 0)
    {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
            detector.push(r ? 0.5f * (l[i] + r[i]) : l[i]);
    }

    const float hz = detector.getHz();
    detectedHz.store(hz);

    float ratio = 1.0f;
    float target = 0.0f;

    if (hz > 60.0f && hz < 1000.0f)
    {
        const float midi = hzToMidi(hz);
        const int nearest = (int) std::lround(midi);
        const int key = (int) apvts.getRawParameterValue("key")->load();
        const int scale = (int) apvts.getRawParameterValue("scale")->load();
        const int targetMidi = quantizeMidi(nearest, key, scale);
        target = midiToHz((float) targetMidi);
        targetHz.store(target);

        const float amount = apvts.getRawParameterValue("amount")->load();
        const bool hard = apvts.getRawParameterValue("hard")->load() > 0.5f;
        const float desired = target / hz;
        ratio = hard ? desired : std::pow(desired, amount);
    }
    else
    {
        targetHz.store(0.0f);
    }

    const float retuneMs = apvts.getRawParameterValue("retune")->load();
    const float tauSamples = juce::jmax(1.0f, (float) (currentSampleRate * retuneMs * 0.001));
    const float smoothingCoeff = juce::jlimit(0.00001f, 1.0f, 1.0f / tauSamples);

    shifter.process(buffer, ratio, smoothingCoeff);

    const float mix = apvts.getRawParameterValue("mix")->load();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        const auto* d = dry.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
            wet[i] = d[i] * (1.0f - mix) + wet[i] * mix;
    }
}

juce::AudioProcessorEditor* OutblockTuneAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

void OutblockTuneAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void OutblockTuneAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OutblockTuneAudioProcessor();
}
