#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

class PitchDetector
{
public:
    void prepare(double sr)
    {
        sampleRate = sr;
        fifo.assign(2048, 0.0f);
        pos = 0;
        filled = false;
        lastHz = 0.0f;
    }

    void push(float x)
    {
        fifo[(size_t) pos++] = x;
        if (pos >= (int) fifo.size())
        {
            pos = 0;
            filled = true;
            analyse();
        }
    }

    float getHz() const noexcept { return lastHz.load(); }

private:
    void analyse()
    {
        if (!filled) return;

        const int n = (int) fifo.size();
        const int minLag = juce::jmax(1, (int) (sampleRate / 800.0));
        const int maxLag = juce::jmin(n / 2, (int) (sampleRate / 70.0));

        float best = 0.0f;
        int bestLag = 0;

        double energy = 0.0;
        for (float v : fifo) energy += (double) v * v;
        if (energy < 1.0e-5)
        {
            lastHz.store(0.0f);
            return;
        }

        for (int lag = minLag; lag <= maxLag; ++lag)
        {
            double sum = 0.0;
            double e1 = 0.0, e2 = 0.0;
            for (int i = 0; i < n - lag; ++i)
            {
                const float a = fifo[(size_t) i];
                const float b = fifo[(size_t) (i + lag)];
                sum += (double) a * b;
                e1 += (double) a * a;
                e2 += (double) b * b;
            }

            const double denom = std::sqrt(e1 * e2) + 1.0e-12;
            const float corr = (float) (sum / denom);
            if (corr > best)
            {
                best = corr;
                bestLag = lag;
            }
        }

        if (bestLag > 0 && best > 0.55f)
            lastHz.store((float) (sampleRate / (double) bestLag));
        else
            lastHz.store(0.0f);
    }

    double sampleRate = 44100.0;
    std::vector<float> fifo;
    int pos = 0;
    bool filled = false;
    std::atomic<float> lastHz { 0.0f };
};

class GranularPitchShifter
{
public:
    void prepare(double sr, int channels)
    {
        sampleRate = sr;
        const int size = (int) std::ceil(sr * 0.12) + 8;
        delay.setSize(juce::jmax(1, channels), size);
        delay.clear();
        writePos = 0;
        phase = 0.0f;
        smoothedRatio = 1.0f;
    }

    void reset()
    {
        delay.clear();
        writePos = 0;
        phase = 0.0f;
        smoothedRatio = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer, float ratio, float smoothingCoeff)
    {
        ratio = juce::jlimit(0.5f, 2.0f, ratio);
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const int size = delay.getNumSamples();
        const float grain = (float) juce::jmin(size - 4, (int) (sampleRate * 0.045));

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedRatio += smoothingCoeff * (ratio - smoothedRatio);
            const float diff = 1.0f - smoothedRatio;
            phase += std::abs(diff) / juce::jmax(32.0f, grain);
            if (phase >= 1.0f) phase -= 1.0f;

            const float p1 = phase;
            float p2 = phase + 0.5f;
            if (p2 >= 1.0f) p2 -= 1.0f;

            const float d1 = diff >= 0.0f ? (1.0f - p1) * grain : p1 * grain;
            const float d2 = diff >= 0.0f ? (1.0f - p2) * grain : p2 * grain;

            const float w1 = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * p1);
            const float w2 = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * p2);
            const float norm = juce::jmax(0.001f, w1 + w2);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* d = delay.getWritePointer(ch);
                auto* x = buffer.getWritePointer(ch);

                d[writePos] = x[i];

                const float a = readLinear(d, size, (float) writePos - d1);
                const float b = readLinear(d, size, (float) writePos - d2);
                x[i] = (a * w1 + b * w2) / norm;
            }

            if (++writePos >= size) writePos = 0;
        }
    }

private:
    static float readLinear(const float* data, int size, float index)
    {
        while (index < 0.0f) index += (float) size;
        while (index >= (float) size) index -= (float) size;

        const int i0 = (int) index;
        const int i1 = (i0 + 1) % size;
        const float frac = index - (float) i0;
        return data[i0] + frac * (data[i1] - data[i0]);
    }

    double sampleRate = 44100.0;
    juce::AudioBuffer<float> delay;
    int writePos = 0;
    float phase = 0.0f;
    float smoothedRatio = 1.0f;
};

class OutblockTuneAudioProcessor final : public juce::AudioProcessor
{
public:
    OutblockTuneAudioProcessor();
    ~OutblockTuneAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "OUTBLOCK TUNE"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float> detectedHz { 0.0f };
    std::atomic<float> targetHz { 0.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParams();
    static float midiToHz(float midi);
    static float hzToMidi(float hz);
    static int quantizeMidi(int midi, int key, int scale);

    PitchDetector detector;
    GranularPitchShifter shifter;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutblockTuneAudioProcessor)
};
