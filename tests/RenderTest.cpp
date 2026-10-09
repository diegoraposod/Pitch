// Offline test for PITCH: runs the real processor without a DAW.
//  1. renders a 40 s journey (Earth -> Granular -> Binaural -> Sine) with a soft "room" input, checks for NaN / clipping
//  2. drops an audio file (makes a fossil), records the input, checks both become fossils
//  3. saves and restores the plug-in state
//  4. snapshots the editor to a PNG
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

static void setParam (PitchAudioProcessor& p, const char* id, float v)
{
    auto* prm = p.params.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? argv[1] : ".");
    const double sr = 48000.0; const int block = 256;

    PitchAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, block);
    proc.prepareToPlay (sr, block);

    juce::AudioBuffer<float> io (2, block), all (2, (int) (sr * 40));
    juce::MidiBuffer midi;
    juce::Random rnd (7);
    double phase = 0;
    struct Way { float t, x, y; } path[] = { { 0, 0, 0 }, { 8, 0, 0 }, { 16, 560, 160 }, { 24, -440, -230 }, { 32, -110, 520 }, { 40, -110, 520 } };
    float worst = 0; bool nan = false; long nearCeiling = 0;

    for (int pos = 0; pos < all.getNumSamples(); pos += block)
    {
        const float t = (float) (pos / sr);
        int w = 0; while (w < 4 && path[w + 1].t <= t) ++w;
        const float u = juce::jlimit (0.0f, 1.0f, (t - path[w].t) / (path[w + 1].t - path[w].t));
        setParam (proc, "coreX", path[w].x + (path[w + 1].x - path[w].x) * u);
        setParam (proc, "coreY", path[w].y + (path[w + 1].y - path[w].y) * u);
        for (int i = 0; i < block; ++i)                                  // a quiet "room": a hummed note plus air
        {
            phase += 220.0 / sr;
            const float s = 0.06f * (float) std::sin (juce::MathConstants<double>::twoPi * phase) * (0.5f + 0.5f * (float) std::sin (pos * 0.00005))
                          + 0.01f * (rnd.nextFloat() * 2 - 1);
            io.setSample (0, i, s); io.setSample (1, i, s);
        }
        if (pos >= (int) (sr * 2) && pos < (int) (sr * 2) + block) proc.startRecording();
        if (pos >= (int) (sr * 6) && pos < (int) (sr * 6) + block) proc.stopRecording();
        proc.processBlock (io, midi);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
            {
                const float v = io.getSample (ch, i);
                if (! std::isfinite (v)) nan = true;
                worst = std::max (worst, std::abs (v)); if (std::abs (v) > 0.85f) ++nearCeiling;
            }
        const int take = std::min (block, all.getNumSamples() - pos);
        for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, pos, io, ch, 0, take);
        if (pos >= (int) (sr * 7) && pos < (int) (sr * 7) + block) juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // lets the recording become a fossil
    }

    juce::WavAudioFormat wav;
    const auto renderFile = outDir.getChildFile ("pitch_journey.wav");
    renderFile.deleteFile();
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (renderFile.createOutputStream().release(), sr, 2, 24, {}, 0)))
        w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());

    std::printf ("render: peak %.3f (%.1f dBFS)  nan: %s  samples above -1.4 dBFS: %ld of %d\n", worst, juce::Decibels::gainToDecibels (worst), nan ? "YES" : "no", nearCeiling, all.getNumSamples() * 2);
    std::printf ("rms per 8 s segment (Earth / ->Granular / ->Binaural / ->Sine / Sine):");
    for (int s = 0; s < 5; ++s) std::printf (" %.4f", all.getRMSLevel (0, (int) (s * 8 * sr), (int) (8 * sr)));
    std::printf ("\nfossils after recording: %d\n", (int) proc.fossilSnapshot().size());

    // drop a file
    const bool dropped = proc.loadFossil (renderFile, 300, 300);
    std::printf ("dropped file -> fossil: %s, fossils now %d\n", dropped ? "yes" : "NO", (int) proc.fossilSnapshot().size());
    for (int b = 0; b < 200; ++b) { io.clear(); proc.processBlock (io, midi); }
    std::printf ("after fossils: out meter %.3f\n", proc.outputMeter());

    // state
    juce::MemoryBlock state; proc.getStateInformation (state);
    PitchAudioProcessor copy; copy.setPlayConfigDetails (2, 2, sr, block); copy.prepareToPlay (sr, block);
    copy.setStateInformation (state.getData(), (int) state.getSize());
    std::printf ("state restore: core (%.0f, %.0f), fossils %d\n", copy.getCoreX(), copy.getCoreY(), (int) copy.fossilSnapshot().size());

    // editor snapshot
    setParam (proc, "coreX", 330); setParam (proc, "coreY", 60);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    for (int i = 0; i < 40; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
    juce::Image img (juce::Image::ARGB, ed->getWidth() * 2, ed->getHeight() * 2, true);
    { juce::Graphics g (img); g.addTransform (juce::AffineTransform::scale (2.0f)); ed->paintEntireComponent (g, true); }
    juce::PNGImageFormat png;
    const auto snap = outDir.getChildFile ("pitch_editor.png"); snap.deleteFile();
    if (auto os = snap.createOutputStream()) png.writeImageToStream (img, *os);
    std::printf ("editor snapshot: %s\n", snap.getFullPathName().toRawUTF8());
    ed.reset();
    return nan ? 1 : 0;
}
