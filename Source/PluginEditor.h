#pragma once
#include "PluginProcessor.h"

// The Onsen: a dark, quiet map. Drag your core toward the amber stones; drop audio to make a fossil; record what PITCH receives.
class PitchEditor : public juce::AudioProcessorEditor,
                    public juce::FileDragAndDropTarget,
                    private juce::Timer
{
public:
    explicit PitchEditor (PitchAudioProcessor&);
    ~PitchEditor() override = default;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    juce::Point<float> toScreen (float wx, float wy) const;
    juce::Point<float> toWorld (juce::Point<float> s) const;
    juce::Point<float> coreScreen() const;
    int stoneAt (juce::Point<float> p) const;
    std::shared_ptr<pitch::Fossil> fossilAt (juce::Point<float> p) const;
    void setCore (float x, float y);
    void beginCoreGesture();
    void endCoreGesture();
    juce::Rectangle<float> recButtonBounds() const;
    juce::Rectangle<float> keyBounds() const;
    juce::Rectangle<float> modeBounds() const;
    juce::Rectangle<float> dropZone() const;

    void drawStone (juce::Graphics&, juce::Point<float> c, float r, float heat, float life, float breath, bool isFossil);
    void drawCore (juce::Graphics&);

    PitchAudioProcessor& proc;
    juce::Point<float> cam;                         // world point at the centre of the view
    enum class Drag { none, core, space, stoneLevel, fossilLevel } drag = Drag::none;
    juce::Point<float> dragStart, coreGrabOffset;
    int dragStone = -1; float dragLevel0 = 0; std::shared_ptr<pitch::Fossil> dragFossil;
    bool moved = false, gestureOpen = false;
    std::optional<juce::Point<float>> glide;
    int hoverStone = -1; int showLevelStone = -1; double showLevelUntil = 0;

    bool dragHover = false; juce::Point<float> dragPos;
    struct Ring { float r, a; };
    std::vector<Ring> rings; float ringCool = 0, lvl = 0;
    double t0 = juce::Time::getMillisecondCounterHiRes();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PitchEditor)
};
