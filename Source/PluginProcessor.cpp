#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace pitch;

static const juce::StringArray kKeyNames { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const juce::StringArray kModeNames { "Major", "Lydian", "Mixolydian", "Minor", "Dorian", "Phrygian", "Harmonic minor" };
static const char* kLevelIds[numStones] = { "earth", "binaural", "granular", "sine" };

juce::AudioProcessorValueTreeState::ParameterLayout PitchAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "coreX", 1 }, "Core X", NormalisableRange<float> (-kWorld, kWorld), 0.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "coreY", 1 }, "Core Y", NormalisableRange<float> (-kWorld, kWorld), 0.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", kKeyNames, 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Scale", kModeNames, 3));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "earth", 1 }, "Earth", 0.0f, 1.0f, 0.55f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "binaural", 1 }, "Binaural", 0.0f, 1.0f, 0.55f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "granular", 1 }, "Granular", 0.0f, 1.0f, 0.65f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "sine", 1 }, "Sine", 0.0f, 1.0f, 0.6f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "fossils", 1 }, "Fossils", 0.0f, 1.0f, 0.8f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input", 0.0f, 1.0f, 1.0f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "space", 1 }, "Space", 0.0f, 1.0f, 0.7f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 6.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "earthLive", 1 }, "Earth Live", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "autoKey", 1 }, "Auto Key", true));   // listen and tune to what comes in
    return l;
}

PitchAudioProcessor::PitchAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PITCH", createLayout())
{
    coreX = params.getRawParameterValue ("coreX"); coreY = params.getRawParameterValue ("coreY");
    key = params.getRawParameterValue ("key"); mode = params.getRawParameterValue ("mode");
    for (int i = 0; i < numStones; ++i) lvl[i] = params.getRawParameterValue (kLevelIds[i]);
    fossilLvl = params.getRawParameterValue ("fossils"); inputLvl = params.getRawParameterValue ("input");
    space = params.getRawParameterValue ("space"); output = params.getRawParameterValue ("output");
    earthLive = params.getRawParameterValue ("earthLive");
    autoKey = params.getRawParameterValue ("autoKey");
    formats.registerBasicFormats();
    startTimerHz (10);
}

PitchAudioProcessor::~PitchAudioProcessor() { stopTimer(); }

bool PitchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono() || in.isDisabled();
}

void PitchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateNow = sampleRate;
    engine.prepare (sampleRate, std::max (64, samplesPerBlock));
    lastKey = lastMode = -1;
    recBuffer.setSize (2, (int) (sampleRate * maxRecordSeconds()) + 1, false, true, false);
    const juce::SpinLock::ScopedLockType sl (fossilLock);
    for (auto& f : fossils) f->place.prepare (sampleRate);
}

float PitchAudioProcessor::stoneLevel (int id) const { return lvl[id]->load(); }
juce::RangedAudioParameter* PitchAudioProcessor::levelParam (int id) const { return params.getParameter (kLevelIds[id]); }

// Natural drift of the Schumann resonance (7.75 - 7.95 Hz) from the clock: same on every device, changes through the day.
static float earthDriftCents()
{
    const double h = (double) juce::Time::currentTimeMillis() / 3.6e6;
    const double hz = 7.85 + 0.1 * (0.6 * std::sin (2.0 * juce::MathConstants<double>::pi * h / 5.3)
                                  + 0.4 * std::sin (2.0 * juce::MathConstants<double>::pi * h / 2.1 + 1.3));
    return (float) (1200.0 * std::log2 (hz / 7.83));
}

void PitchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels();
    for (int ch = inCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    if (inCh == 1 && buffer.getNumChannels() > 1) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // mono input -> both sides
    if (inCh == 0) buffer.clear();                                                            // no input: drones only

    // copy the input for the key detector (mono, lock-free ring)
    if (autoKey->load() > 0.5f && inCh > 0)
    {
        const float* a = buffer.getReadPointer (0);
        const float* b = buffer.getReadPointer (buffer.getNumChannels() > 1 ? 1 : 0);
        int w = keyRingW.load (std::memory_order_relaxed);
        const int len = (int) keyRing.size();
        for (int i = 0; i < n; ++i) { keyRing[(size_t) w] = 0.5f * (a[i] + b[i]); if (++w >= len) w = 0; }
        keyRingW.store (w, std::memory_order_release);
        keyFresh.fetch_add (n);
    }

    // record the signal PITCH receives (before it is processed)
    if (recording.load() && ! recOutput.load()) captureInto (buffer, n);

    // scale
    const int k = (int) key->load(), m = (int) mode->load();
    if (k != lastKey || m != lastMode) { engine.setScale (k, m, lastKey < 0); lastKey = k; lastMode = m; }

    // the map: distance from your core decides every level, filter, position and reverb share
    const float cx = coreX->load(), cy = coreY->load();
    float cloud = 0;
    for (int i = 0; i < numStones; ++i)
    {
        const auto& d = stones[i];
        const auto sp = spatialFor (d.x, d.y, d.r, d.isEarth, cx, cy);
        const float level = std::pow (lvl[i]->load(), 1.5f);
        engine.setStone (i, sp, level);
        if (! d.isEarth) cloud = std::max (cloud, sp.intensity * std::min (1.0f, lvl[i]->load() * 1.4f));
    }

    std::vector<std::shared_ptr<Fossil>>* fossilList = nullptr;
    const juce::SpinLock::ScopedTryLockType tl (fossilLock);
    if (tl.isLocked())
    {
        fossilList = &fossils;
        const float fl = fossilLvl->load();
        for (auto& f : fossils)
        {
            const auto sp = spatialFor (f->x.load(), f->y.load(), kFossilR, false, cx, cy);
            f->place.setTargets (sp, std::pow (f->level.load() * fl, 1.5f) * 1.2f);
            cloud = std::max (cloud, sp.intensity * std::min (1.0f, f->level.load() * 1.4f));
        }
    }

    engine.setCloud (cloud);                                         // near the stones the input rings their resonators
    engine.setInputDry (0.0f);                                         // only a trace of the raw input: PITCH always processes it
    engine.setInputCloud (inputLvl->load());                          // everything that comes in goes through the tuned cloud
    engine.setCloudSpace (0.4f + 0.4f * cloud);                      // back home at Earth the cloud is drier; near the stones it blooms
    engine.setEarthCents (0.15f * earthDriftCents());                 // a few cents of breathing, never out of tune
    const float kp = kpFetcher.kp.load();
    engine.setStorm (earthLive->load() > 0.5f && kp >= 0.0f ? kp / 9.0f : 0.0f);
    engine.setReverb (space->load());
    engine.setOutput (juce::Decibels::decibelsToGain (output->load()));

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (buffer.getNumChannels() > 1 ? 1 : 0);
    engine.process (L, R, L, R, n, fossilList);

    // a tap on the core bounces what PITCH is playing into a new fossil
    if (recording.load() && recOutput.load()) captureInto (buffer, n);
}

void PitchAudioProcessor::captureInto (const juce::AudioBuffer<float>& buffer, int n)
{
    const int pos = recPos.load();
    const int room = std::min (recBuffer.getNumSamples(), recLimit.load()) - pos, take = std::max (0, std::min (room, n));
    if (take > 0)
    {
        for (int ch = 0; ch < 2; ++ch) recBuffer.copyFrom (ch, pos, buffer, std::min (ch, buffer.getNumChannels() - 1), 0, take);
        recPos.store (pos + take);
    }
    if (take < n) recording.store (false);                                                   // limit reached: stop
}

// ---------- fossils ----------
bool PitchAudioProcessor::isAudioFile (const juce::String& path)
{
    return path.endsWithIgnoreCase (".wav") || path.endsWithIgnoreCase (".aif") || path.endsWithIgnoreCase (".aiff")
        || path.endsWithIgnoreCase (".flac") || path.endsWithIgnoreCase (".ogg") || path.endsWithIgnoreCase (".mp3")
        || path.endsWithIgnoreCase (".m4a") || path.endsWithIgnoreCase (".caf");
}

std::shared_ptr<Fossil> PitchAudioProcessor::makeFossil (juce::AudioBuffer<float>&& audio, double rate, const juce::String& name,
                                                          const juce::String& path, float x, float y)
{
    auto f = std::make_shared<Fossil>();
    f->audio = std::move (audio); f->sourceRate = rate; f->name = name; f->path = path;
    f->x.store (clampf (x, -kWorld, kWorld)); f->y.store (clampf (y, -kWorld, kWorld));
    f->uid = nextUid++;
    f->rng.s ^= (uint32_t) (f->uid * 2654435761u);
    f->place.prepare (sampleRateNow);
    {
        const juce::SpinLock::ScopedLockType sl (fossilLock);
        if (fossils.size() >= 12) fossils.erase (fossils.begin());   // keep it light: at most 12 fossils
        fossils.push_back (f);
    }
    return f;
}

bool PitchAudioProcessor::loadFossil (const juce::File& file, float x, float y)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr) return false;
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 60.0));   // up to 60 s
    if (len < 256) return false;
    juce::AudioBuffer<float> audio (2, len);
    reader->read (&audio, 0, len, 0, true, true);
    if (reader->numChannels == 1) audio.copyFrom (1, 0, audio, 0, 0, len);
    const float peak = audio.getMagnitude (0, len);
    if (peak > 0.0001f) audio.applyGain (0.5f / peak);               // gentle normalisation so every fossil sits at a similar level
    makeFossil (std::move (audio), reader->sampleRate, file.getFileNameWithoutExtension(), file.getFullPathName(), x, y);
    return true;
}

void PitchAudioProcessor::removeFossil (int uid)
{
    std::shared_ptr<Fossil> keepAlive;                               // freed here, on the message thread, never on the audio thread
    const juce::SpinLock::ScopedLockType sl (fossilLock);
    for (auto it = fossils.begin(); it != fossils.end(); ++it)
        if ((*it)->uid == uid) { keepAlive = *it; fossils.erase (it); break; }
}

std::vector<std::shared_ptr<Fossil>> PitchAudioProcessor::fossilSnapshot()
{
    const juce::SpinLock::ScopedLockType sl (fossilLock);
    return fossils;
}

// ---------- recording ----------
void PitchAudioProcessor::startRecording (bool bounceOutput, double seconds)
{
    if (recBuffer.getNumSamples() < 2) return;
    recOutput.store (bounceOutput);
    recLimit.store ((int) (std::min (seconds, maxRecordSeconds()) * sampleRateNow));
    recPos.store (0); recording.store (true); recPending = true;
}

void PitchAudioProcessor::stopRecording() { recording.store (false); }

void PitchAudioProcessor::timerCallback()
{
    // the scale shown in the editor
    const int k = (int) key->load(), m = (int) mode->load();
    if (k != shownScale.pc || m != shownScale.mode) shownScale = buildScale (k, m);
    if (recPending && ! recording.load()) { recPending = false; finishRecording(); }

    // auto key: when it is switched back on, listen again from scratch
    const bool ak = autoKey->load() > 0.5f;
    if (ak && ! lastAutoKey) { params.state.setProperty ("keyLocked", false, nullptr); keyDetector.reset(); }
    lastAutoKey = ak;
    if (! isListeningForKey()) return;

    // every ~0.2 s of new input, analyse the latest window
    const int hop = (int) (0.2 * sampleRateNow);
    if (keyFresh.load() < hop) return;
    keyFresh.store (0);
    const int len = (int) keyRing.size(), w = keyRingW.load (std::memory_order_acquire);
    for (int i = 0; i < KeyDetector::size; ++i) keyWindow[(size_t) i] = keyRing[(size_t) ((w - KeyDetector::size + i + len) % len)];
    int pc = 0, md = 3;
    if (keyDetector.analyse (keyWindow.data(), sampleRateNow, 0.2, pc, md))
    {
        const char* ids[2] = { "key", "mode" }; const int vals[2] = { pc, md };
        for (int i = 0; i < 2; ++i)
        {
            auto* p = params.getParameter (ids[i]);
            p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) vals[i])); p->endChangeGesture();
        }
        params.state.setProperty ("keyLocked", true, nullptr);                 // decided: keep this key (saved with the project)
    }
}

void PitchAudioProcessor::stopAutoKey()
{
    auto* p = params.getParameter ("autoKey");
    p->beginChangeGesture(); p->setValueNotifyingHost (0.0f); p->endChangeGesture();
    lastAutoKey = false;
}

void PitchAudioProcessor::finishRecording()
{
    const int n = recPos.load();
    if (n < (int) (0.4 * sampleRateNow)) return;
    juce::AudioBuffer<float> take (2, n);
    for (int ch = 0; ch < 2; ++ch) take.copyFrom (ch, 0, recBuffer, ch, 0, n);

    // keep the take as a WAV in ~/Music/PITCH/Fossils, so it survives with the project
    const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("PITCH").getChildFile ("Fossils");
    dir.createDirectory();
    const auto name = "PITCH " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H.%M.%S");
    const auto file = dir.getChildFile (name + ".wav");
    juce::String path;
    if (auto out = std::unique_ptr<juce::FileOutputStream> (file.createOutputStream()))
    {
        juce::WavAudioFormat wav;
        if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (out.get(), sampleRateNow, 2, 24, {}, 0)))
        {
            out.release();                                            // the writer owns the stream now
            writer->writeFromAudioSampleBuffer (take, 0, n);
            path = file.getFullPathName();
            lastSavedName = file.getFileName();
        }
    }
    const float peak = take.getMagnitude (0, n);
    if (peak > 0.0001f) take.applyGain (0.5f / peak);
    const float a = juce::Random::getSystemRandom().nextFloat() * juce::MathConstants<float>::twoPi;
    makeFossil (std::move (take), sampleRateNow, name, path, coreX->load() + std::cos (a) * 170.0f, coreY->load() + std::sin (a) * 170.0f);
}

// ---------- state ----------
void PitchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    juce::ValueTree list ("FOSSILS");
    for (auto& f : fossilSnapshot())
    {
        if (f->path.isEmpty()) continue;
        juce::ValueTree t ("F");
        t.setProperty ("path", f->path, nullptr); t.setProperty ("x", f->x.load(), nullptr);
        t.setProperty ("y", f->y.load(), nullptr); t.setProperty ("level", f->level.load(), nullptr);
        list.appendChild (t, nullptr);
    }
    state.removeChild (state.getChildWithName ("FOSSILS"), nullptr);
    state.appendChild (list, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PitchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (params.state.getType())) return;
    auto list = state.getChildWithName ("FOSSILS");
    state.removeChild (list, nullptr);
    params.replaceState (state);
    {
        const juce::SpinLock::ScopedLockType sl (fossilLock);
        fossils.clear();
    }
    for (int i = 0; i < list.getNumChildren(); ++i)
    {
        const auto t = list.getChild (i);
        const juce::File file (t["path"].toString());
        if (file.existsAsFile() && loadFossil (file, (float) t["x"], (float) t["y"]))
            fossilSnapshot().back()->level.store ((float) t["level"]);
    }
}

juce::AudioProcessorEditor* PitchAudioProcessor::createEditor() { return new PitchEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PitchAudioProcessor(); }
