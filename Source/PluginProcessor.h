#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine.h"

// Polls NOAA's planetary K-index (public JSON, updated every minute) in the background.
class KpFetcher : private juce::Thread
{
public:
    KpFetcher() : juce::Thread ("PITCH Kp") { startThread (juce::Thread::Priority::low); }
    ~KpFetcher() override { stopThread (8000); }
    std::atomic<float> kp { -1.0f };              // -1 = no reading yet
private:
    void run() override
    {
        while (! threadShouldExit())
        {
            fetch();
            for (int i = 0; i < 300 && ! threadShouldExit(); ++i) wait (1000);   // every 5 minutes
        }
    }
    void fetch()
    {
        juce::URL url ("https://services.swpc.noaa.gov/json/planetary_k_index_1m.json");
        auto opts = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs (6000);
        if (auto stream = url.createInputStream (opts))
        {
            const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
            if (auto* arr = json.getArray(); arr != nullptr && ! arr->isEmpty())
            {
                const auto& last = arr->getReference (arr->size() - 1);
                const auto v = last.hasProperty ("estimated_kp") ? last["estimated_kp"] : last["kp_index"];
                const float k = (float) (double) v;
                if (k >= 0.0f && k <= 9.0f) kp.store (k);
            }
        }
    }
};

class PitchAudioProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    PitchAudioProcessor();
    ~PitchAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PITCH"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------- used by the editor (message thread) ----------
    juce::AudioProcessorValueTreeState params;
    float getCoreX() const { return coreX->load(); }
    float getCoreY() const { return coreY->load(); }
    float stoneLevel (int id) const;
    juce::RangedAudioParameter* levelParam (int id) const;

    bool loadFossil (const juce::File& file, float x, float y);              // dropped audio file -> new fossil
    void removeFossil (int uid);
    std::vector<std::shared_ptr<pitch::Fossil>> fossilSnapshot();
    static bool isAudioFile (const juce::String& path);

    void startRecording();                                                  // records the signal PITCH receives
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    double recordedSeconds() const { return recPos.load() / std::max (1.0, sampleRateNow); }
    double maxRecordSeconds() const { return 30.0; }
    juce::String lastSavedName;

    float kpNow() const { return kpFetcher.kp.load(); }
    float inputMeter() const { return engine.inputMeter.load(); }
    float outputMeter() const { return engine.outputMeter.load(); }
    const pitch::Scale& currentScale() const { return shownScale; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void finishRecording();
    std::shared_ptr<pitch::Fossil> makeFossil (juce::AudioBuffer<float>&& audio, double rate, const juce::String& name, const juce::String& path, float x, float y);

    pitch::Engine engine;
    pitch::Scale shownScale = pitch::buildScale (2, 3);
    double sampleRateNow = 48000.0;
    int lastKey = -1, lastMode = -1;

    std::atomic<float>* coreX = nullptr; std::atomic<float>* coreY = nullptr;
    std::atomic<float>* key = nullptr; std::atomic<float>* mode = nullptr;
    std::atomic<float>* lvl[pitch::numStones] {};
    std::atomic<float>* fossilLvl = nullptr; std::atomic<float>* inputLvl = nullptr;
    std::atomic<float>* space = nullptr; std::atomic<float>* output = nullptr; std::atomic<float>* earthLive = nullptr;

    std::vector<std::shared_ptr<pitch::Fossil>> fossils;    // read on the audio thread under a try-lock
    juce::SpinLock fossilLock;
    int nextUid = 1;

    juce::AudioBuffer<float> recBuffer;
    std::atomic<bool> recording { false };
    std::atomic<int> recPos { 0 };
    bool recPending = false;

    juce::AudioFormatManager formats;
    KpFetcher kpFetcher;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchAudioProcessor)
};
