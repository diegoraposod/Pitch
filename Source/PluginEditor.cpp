#include "PluginEditor.h"

using namespace pitch;

namespace
{
const juce::Colour kBg0 (0xff121314), kBg1 (0xff1d1e1f), kInk (0xfff4f1ea), kMute (0xff6c6a66),
                   kAmber (0xffe29d62), kAmberDeep (0xffd48c53), kRec (0xffc46a5f);
const char* kKeys[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
const char* kModeNames[7] = { "major", "lydian", "mixolydian", "minor", "dorian", "phrygian", "harmonic minor" };

juce::Font font (float size, bool light = true)
{
    return juce::Font (juce::FontOptions (size, light ? juce::Font::plain : juce::Font::bold)).withExtraKerningFactor (0.18f);
}

void glow (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, float alpha)
{
    if (alpha <= 0.003f || r < 1.0f) return;
    juce::ColourGradient gr (col.withAlpha (alpha), c, col.withAlpha (0.0f), c.translated (r, 0), true);
    gr.addColour (0.45, col.withAlpha (alpha * 0.35f));
    g.setGradientFill (gr);
    g.fillEllipse (c.x - r, c.y - r, r * 2, r * 2);
}
} // namespace

PitchEditor::PitchEditor (PitchAudioProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setSize (780, 500);
    cam = { proc.getCoreX(), proc.getCoreY() };
    startTimerHz (30);
}

// ---------- coordinates ----------
juce::Point<float> PitchEditor::toScreen (float wx, float wy) const { return { wx - cam.x + getWidth() * 0.5f, wy - cam.y + getHeight() * 0.5f }; }
juce::Point<float> PitchEditor::toWorld (juce::Point<float> s) const { return { s.x + cam.x - getWidth() * 0.5f, s.y + cam.y - getHeight() * 0.5f }; }
juce::Point<float> PitchEditor::coreScreen() const { return toScreen (proc.getCoreX(), proc.getCoreY()); }
juce::Rectangle<float> PitchEditor::recButtonBounds() const { return { (float) getWidth() - 52.0f, 16.0f, 34.0f, 34.0f }; }
juce::Rectangle<float> PitchEditor::keyBounds() const { return { 24.0f, 40.0f, 20.0f, 16.0f }; }
juce::Rectangle<float> PitchEditor::modeBounds() const { return { 44.0f, 40.0f, 130.0f, 16.0f }; }
juce::Rectangle<float> PitchEditor::dropZone() const { return { getWidth() * 0.5f - 150.0f, (float) getHeight() - 58.0f, 300.0f, 36.0f }; }

int PitchEditor::stoneAt (juce::Point<float> p) const
{
    for (int i = 0; i < numStones; ++i)
    {
        if (stones[i].isEarth) continue;
        if (p.getDistanceFrom (toScreen (stones[i].x, stones[i].y)) < stones[i].r * 1.1f) return i;
    }
    return -1;
}

std::shared_ptr<Fossil> PitchEditor::fossilAt (juce::Point<float> p) const
{
    for (auto& f : proc.fossilSnapshot())
        if (p.getDistanceFrom (toScreen (f->x.load(), f->y.load())) < kFossilR * 1.3f) return f;
    return nullptr;
}

// Core X / Y are real plug-in parameters, so moving the core can be recorded as automation in the DAW.
void PitchEditor::beginCoreGesture()
{
    if (gestureOpen) return;
    proc.params.getParameter ("coreX")->beginChangeGesture(); proc.params.getParameter ("coreY")->beginChangeGesture();
    gestureOpen = true;
}
void PitchEditor::endCoreGesture()
{
    if (! gestureOpen) return;
    proc.params.getParameter ("coreX")->endChangeGesture(); proc.params.getParameter ("coreY")->endChangeGesture();
    gestureOpen = false;
}
void PitchEditor::setCore (float x, float y)
{
    auto* px = proc.params.getParameter ("coreX"); auto* py = proc.params.getParameter ("coreY");
    px->setValueNotifyingHost (px->convertTo0to1 (clampf (x, -kWorld, kWorld)));
    py->setValueNotifyingHost (py->convertTo0to1 (clampf (y, -kWorld, kWorld)));
}

// ---------- mouse ----------
void PitchEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    moved = false; dragStart = p; glide.reset();
    if (recButtonBounds().expanded (4).contains (p)) { proc.isRecording() ? proc.stopRecording() : proc.startRecording(); drag = Drag::none; return; }
    if (keyBounds().contains (p) || modeBounds().contains (p))
    {
        proc.stopAutoKey();                                                    // a key chosen by hand stays
        auto* prm = proc.params.getParameter (keyBounds().contains (p) ? "key" : "mode");
        const int count = keyBounds().contains (p) ? 12 : 7, step = e.mods.isRightButtonDown() ? count - 1 : 1;
        const int now = juce::roundToInt (prm->convertFrom0to1 (prm->getValue()));
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 ((float) ((now + step) % count))); prm->endChangeGesture();
        drag = Drag::none; return;
    }
    if (e.mods.isPopupMenu())
    {
        if (auto f = fossilAt (p)) proc.removeFossil (f->uid);           // right-click a fossil: let it go
        drag = Drag::none; return;
    }
    if (p.getDistanceFrom (coreScreen()) < 34.0f)
    {
        drag = Drag::core; coreGrabOffset = coreScreen() - p; beginCoreGesture(); return;
    }
    if (auto f = fossilAt (p)) { drag = Drag::fossilLevel; dragFossil = f; dragLevel0 = f->level.load(); return; }
    if ((dragStone = stoneAt (p)) >= 0)
    {
        drag = Drag::stoneLevel; dragLevel0 = proc.stoneLevel (dragStone);
        proc.levelParam (dragStone)->beginChangeGesture(); return;
    }
    drag = Drag::space; beginCoreGesture();
}

void PitchEditor::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (! moved && p.getDistanceFrom (dragStart) > 4.0f) moved = true;
    if (! moved) return;
    switch (drag)
    {
        case Drag::core: { const auto w = toWorld (p + coreGrabOffset); setCore (w.x, w.y); break; }
        case Drag::space:
        {
            const auto d = p - dragStart; dragStart = p;
            setCore (proc.getCoreX() + d.x, proc.getCoreY() + d.y); break;
        }
        case Drag::stoneLevel:
        {
            auto* prm = proc.levelParam (dragStone);
            prm->setValueNotifyingHost (clampf (dragLevel0 - (p.y - e.mouseDownPosition.y) / 160.0f, 0.0f, 1.0f));
            showLevelStone = dragStone; showLevelUntil = juce::Time::getMillisecondCounterHiRes() + 1500; break;
        }
        case Drag::fossilLevel:
            if (dragFossil) dragFossil->level.store (clampf (dragLevel0 - (p.y - e.mouseDownPosition.y) / 160.0f, 0.0f, 1.0f));
            break;
        case Drag::none: break;
    }
}

void PitchEditor::mouseUp (const juce::MouseEvent& e)
{
    if (drag == Drag::stoneLevel)
    {
        proc.levelParam (dragStone)->endChangeGesture();
        if (! moved)                                                     // a tap on a stone: drift toward it
        {
            const auto& s = stones[dragStone];
            const float dx = proc.getCoreX() - s.x, dy = proc.getCoreY() - s.y, d = std::max (1.0f, std::sqrt (dx * dx + dy * dy));
            glide = juce::Point<float> (s.x + dx / d * (s.r * 1.6f + 40.0f), s.y + dy / d * (s.r * 1.6f + 40.0f));
            beginCoreGesture();
        }
    }
    else if (drag == Drag::space && ! moved)
    {
        glide = toWorld (e.position);                                    // a tap on the water: drift there
        endCoreGesture(); beginCoreGesture();
    }
    else if (drag == Drag::core && ! moved)                             // a tap on the core: bounce 6 s of what is playing into a fossil
    {
        endCoreGesture();
        if (proc.isRecording()) proc.stopRecording(); else proc.startRecording (true, 6.0);
    }
    else if (drag == Drag::core || drag == Drag::space) endCoreGesture();
    dragFossil.reset();
    drag = Drag::none;
}

void PitchEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (stoneAt (e.position) < 0 && ! fossilAt (e.position)) { glide = juce::Point<float> (0, 0); beginCoreGesture(); }   // home to Earth
}

void PitchEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int s = stoneAt (e.position);
    if (s >= 0)
    {
        auto* prm = proc.levelParam (s);
        prm->beginChangeGesture(); prm->setValueNotifyingHost (clampf (prm->getValue() + w.deltaY * 0.25f, 0.0f, 1.0f)); prm->endChangeGesture();
        showLevelStone = s; showLevelUntil = juce::Time::getMillisecondCounterHiRes() + 1500;
    }
    else if (w.deltaY < 0) { glide = juce::Point<float> (0, 0); beginCoreGesture(); }   // scroll down: return to Earth
}

void PitchEditor::mouseMove (const juce::MouseEvent& e)
{
    hoverStone = stoneAt (e.position);
    const bool clickable = hoverStone >= 0 || e.position.getDistanceFrom (coreScreen()) < 34.0f
                        || recButtonBounds().contains (e.position) || keyBounds().contains (e.position) || modeBounds().contains (e.position);
    setMouseCursor (clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::DraggingHandCursor);
}

// ---------- drop audio ----------
bool PitchEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (PitchAudioProcessor::isAudioFile (f)) return true;
    return false;
}
void PitchEditor::fileDragEnter (const juce::StringArray&, int x, int y) { dragHover = true; dragPos = { (float) x, (float) y }; }
void PitchEditor::fileDragMove (const juce::StringArray&, int x, int y)  { dragPos = { (float) x, (float) y }; }
void PitchEditor::fileDragExit (const juce::StringArray&)                { dragHover = false; }
void PitchEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    dragHover = false;
    auto w = toWorld ({ (float) x, (float) y });
    if (dropZone().contains ((float) x, (float) y)) w = { proc.getCoreX() + 150.0f, proc.getCoreY() };   // dropped on the zone: born beside you
    int i = 0;
    for (auto& path : files)
        if (PitchAudioProcessor::isAudioFile (path) && proc.loadFossil (juce::File (path), w.x + i * 60.0f, w.y + i * 30.0f)) ++i;
}

// ---------- animation ----------
void PitchEditor::timerCallback()
{
    const float dt = 1.0f / 30.0f;
    if (glide)
    {
        const float k = std::min (1.0f, dt * 1.6f);
        const float nx = proc.getCoreX() + (glide->x - proc.getCoreX()) * k, ny = proc.getCoreY() + (glide->y - proc.getCoreY()) * k;
        setCore (nx, ny);
        if (std::abs (glide->x - nx) + std::abs (glide->y - ny) < 1.0f) { glide.reset(); endCoreGesture(); }
    }
    const juce::Point<float> core (proc.getCoreX(), proc.getCoreY());
    cam += (core - cam) * std::min (1.0f, dt * (1.6f + core.getDistanceFrom (cam) / 150.0f));

    const float in = std::min (1.0f, std::sqrt (proc.inputMeter()) * 1.6f);
    lvl += (in - lvl) * (in > lvl ? 0.4f : 0.08f);
    ringCool -= dt;
    if (lvl > 0.25f && ringCool <= 0) { rings.push_back ({ 24.0f, 0.22f * std::min (1.0f, lvl * 1.6f) }); ringCool = 1.0f; }
    for (auto& r : rings) { r.r += dt * 30.0f; r.a *= std::pow (0.55f, dt); }
    rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.a < 0.004f; }), rings.end());
    repaint();
}

// ---------- drawing ----------
void PitchEditor::drawStone (juce::Graphics& g, juce::Point<float> c, float r, float heat, float life, float breath, bool isFossil)
{
    glow (g, c, r * (2.2f + 1.6f * heat + 0.15f * breath), kAmber, (0.10f + 0.26f * heat) * life);       // diffusion halo
    const auto hl = c.translated (-r * 0.18f, -r * 0.22f);
    juce::ColourGradient body (juce::Colour (0xfff0ba84).withAlpha (0.95f * life), hl,
                               kAmberDeep.withAlpha (0.0f), hl.translated (r * 1.25f, 0), true);
    body.addColour (0.42, kAmber.withAlpha (0.8f * life));
    body.addColour (0.72, kAmberDeep.withAlpha (0.34f * life));
    g.setGradientFill (body);
    g.fillEllipse (c.x - r * 1.6f, c.y - r * 1.6f, r * 3.2f, r * 3.2f);     // larger than the gradient: no hard rim anywhere
    glow (g, hl, r * 0.8f, juce::Colour (0xfffacd96), 0.3f * heat);                                      // warmth from inside
    if (isFossil) { g.setColour (kAmber.withAlpha (0.5f)); g.drawEllipse (c.x - r - 6, c.y - r - 6, r * 2 + 12, r * 2 + 12, 1.0f); }
}

void PitchEditor::drawCore (juce::Graphics& g)
{
    const auto c = coreScreen();
    const float t = (float) ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    for (auto& r : rings)                                                // drops on calm water
        for (auto [w, f] : { std::pair<float, float> { 5.0f, 0.25f }, { 2.4f, 0.5f }, { 1.0f, 1.0f } })
        {
            g.setColour (kInk.withAlpha (r.a * f));
            g.drawEllipse (c.x - r.r, c.y - r.r * 0.62f, r.r * 2, r.r * 1.24f, w);
        }
    const float cr = 17.0f + lvl * 9.0f;
    glow (g, c, cr * 4.0f, kInk, 0.10f + 0.15f * lvl);
    const auto chl = c.translated (-cr * 0.2f, -cr * 0.25f);
    juce::ColourGradient body (kInk.withAlpha (0.98f), chl, kInk.withAlpha (0.0f), chl.translated (cr * 1.3f, 0), true);
    body.addColour (0.5, kInk.withAlpha (0.75f)); body.addColour (0.82, kInk.withAlpha (0.25f));
    g.setGradientFill (body);
    g.fillEllipse (c.x - cr * 1.7f, c.y - cr * 1.7f, cr * 3.4f, cr * 3.4f);
    for (int i = 0; i < 3; ++i)                                          // threads of vapour
    {
        const float ph = t * 0.35f + i * 2.1f, h = 28.0f + 16.0f * lvl;
        juce::Path p; const float x0 = c.x + (i - 1) * cr * 0.45f;
        p.startNewSubPath (x0, c.y - cr * 0.8f);
        for (int k = 1; k <= 12; ++k) { const float u = k / 12.0f; p.lineTo (x0 + std::sin (ph * 1.7f + u * 5 + i) * 6 * u, c.y - cr * 0.8f - u * h); }
        g.setColour (kInk.withAlpha ((0.06f + 0.12f * lvl) * (0.6f + 0.4f * std::sin (ph))));
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
    if (proc.isRecording())
    {
        const float prog = (float) (proc.recordedSeconds() / std::max (0.5, proc.recordLimitSeconds()));
        juce::Path arc; arc.addCentredArc (c.x, c.y, 60, 60, 0, 0, juce::MathConstants<float>::twoPi * prog, true);
        g.setColour (kRec.withAlpha (0.8f)); g.strokePath (arc, juce::PathStrokeType (1.2f));
    }
}

void PitchEditor::paint (juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    const float t = (float) ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    const double now = juce::Time::getMillisecondCounterHiRes();

    // wet basalt, light filtered through steam
    g.setGradientFill (juce::ColourGradient (kBg1, W * 0.5f, H * 0.62f, kBg0, W * 0.5f + std::max (W, H) * 0.85f, H * 0.62f, true));
    g.fillAll();
    for (int i = 0; i < 6; ++i)
    {
        const float sx = std::fmod (i * 241.0f - cam.x * 0.06f * (1 + i % 3) + 4000.0f, W + 400.0f) - 200.0f;
        const float sy = std::fmod (H + 300.0f - std::fmod (t * (4.0f + i) + i * 173.0f, H + 600.0f) - cam.y * 0.02f + 6000.0f, H + 600.0f) - 300.0f;
        glow (g, { sx + std::sin (t * 0.07f + i) * 40.0f, sy }, 160.0f + i * 25.0f, kInk, 0.035f);
    }
    g.setColour (kInk.withAlpha (0.18f));                                // parallax motes
    for (int i = 0; i < 90; ++i)
    {
        const float z = 0.08f + (i % 7) * 0.05f;
        const float x = std::fmod (i * 977.0f - cam.x * z + 100000.0f, 1600.0f), y = std::fmod (i * 613.0f - cam.y * z - t * 6.0f * z + 100000.0f, 1600.0f);
        if (x < W && y < H) g.fillRect (x, y, 1.0f, 1.0f);
    }

    const float cx = proc.getCoreX(), cy = proc.getCoreY();
    const auto core = coreScreen();

    // Earth: a faint pearl ring that warms when the planet's magnetic field is agitated
    {
        const auto e = toScreen (0, 0);
        const float kp = proc.kpNow(), k = kp >= 0 ? kp / 9.0f : 0.0f, rr = 70.0f + 20.0f * k;
        if (k > 0.3f) glow (g, e, 120.0f, kAmber, 0.12f * k);
        g.setColour ((k > 0.4f ? kAmber : kInk).withAlpha (0.05f + 0.05f * std::sin (t * (0.9f + 2 * k)) + 0.25f * k));
        g.drawEllipse (e.x - rr, e.y - rr * 0.62f, rr * 2, rr * 1.24f, 1.0f);
    }

    // links: your sound reaching the stones
    for (int i = 0; i < numStones; ++i)
    {
        if (stones[i].isEarth) continue;
        const auto sp = spatialFor (stones[i].x, stones[i].y, stones[i].r, false, cx, cy);
        if (sp.near < 0.02f) continue;
        const auto s = toScreen (stones[i].x, stones[i].y);
        juce::Path line; line.startNewSubPath (core); line.lineTo (s);
        juce::Path dashed; const float dashes[2] = { 2.0f, 6.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (kInk.withAlpha (0.08f * sp.near)); g.fillPath (dashed);
    }

    // stones
    for (int i = 0; i < numStones; ++i)
    {
        const auto& d = stones[i];
        if (d.isEarth) continue;
        const auto s = toScreen (d.x, d.y);
        if (s.x < -d.r * 4 || s.x > W + d.r * 4 || s.y < -d.r * 4 || s.y > H + d.r * 4)
        {
            const auto dir = s - juce::Point<float> (W * 0.5f, H * 0.5f);   // edge mark: which way it is
            const float k = std::min ((W * 0.5f - 16) / std::max (1e-3f, std::abs (dir.x)), (H * 0.5f - 16) / std::max (1e-3f, std::abs (dir.y)));
            const auto m = juce::Point<float> (W * 0.5f, H * 0.5f) + dir * k;
            g.setColour (kAmber.withAlpha (0.6f)); g.fillEllipse (m.x - 2.5f, m.y - 2.5f, 5, 5);
            continue;
        }
        const auto sp = spatialFor (d.x, d.y, d.r, false, cx, cy);
        const float life = 0.55f + 0.45f * proc.stoneLevel (i);
        drawStone (g, s, d.r, sp.near, life, std::sin (t * 0.7f + i), false);
        g.setFont (font (9.0f));
        g.setColour (kInk.withAlpha (0.22f + 0.5f * sp.near + (i == hoverStone ? 0.25f : 0.0f)));
        g.drawText (d.label, juce::Rectangle<float> (s.x - 100, s.y + d.r + 10, 200, 14), juce::Justification::centred);
        if (i == hoverStone || (i == showLevelStone && now < showLevelUntil))
        {
            juce::Path arc; arc.addCentredArc (s.x, s.y, d.r + 12, d.r + 12, 0, 0, juce::MathConstants<float>::twoPi * proc.stoneLevel (i), true);
            g.setColour (kInk.withAlpha (0.6f)); g.strokePath (arc, juce::PathStrokeType (1.0f));
        }
    }

    // fossils
    for (auto& f : proc.fossilSnapshot())
    {
        const auto s = toScreen (f->x.load(), f->y.load());
        if (s.x < -80 || s.x > W + 80 || s.y < -80 || s.y > H + 80) continue;
        const auto sp = spatialFor (f->x.load(), f->y.load(), kFossilR, false, cx, cy);
        drawStone (g, s, kFossilR, sp.near, 0.55f + 0.45f * f->level.load(), std::sin (t + f->uid), true);
        g.setFont (font (8.0f)); g.setColour (kInk.withAlpha (0.2f + 0.4f * sp.near));
        g.drawText (f->name.toLowerCase(), juce::Rectangle<float> (s.x - 90, s.y + kFossilR + 12, 180, 12), juce::Justification::centred);
    }

    drawCore (g);

    // top-left: name, key and scale (click to change), Earth Kp
    g.setFont (font (11.0f)); g.setColour (kInk.withAlpha (0.9f));
    g.drawText ("P I T C H", 24, 18, 160, 16, juce::Justification::left);
    const auto& sc = proc.currentScale();
    g.setFont (font (9.0f)); g.setColour (kInk.withAlpha (0.6f));
    g.drawText (kKeys[sc.pc], keyBounds(), juce::Justification::left);
    g.drawText (kModeNames[sc.mode], modeBounds(), juce::Justification::left);
    if (proc.isListeningForKey())
    {
        g.setColour (kInk.withAlpha (0.35f + 0.25f * (float) std::sin (juce::Time::getMillisecondCounterHiRes() * 0.004)));
        g.drawText ("listening for the key...", 24, 72, 200, 12, juce::Justification::left);
    }
    const float kp = proc.kpNow();
    if (kp >= 0) { g.setColour (kMute); g.drawText ("earth kp " + juce::String (kp, 1), 24, 58, 160, 14, juce::Justification::left); }

    // top-right: record what PITCH receives
    const auto rb = recButtonBounds();
    const bool rec = proc.isRecording();
    g.setColour ((rec ? kRec : kInk).withAlpha (rec ? 0.9f : 0.25f)); g.drawEllipse (rb, 1.0f);
    g.setColour (kRec);
    if (rec)
    {
        const float a = 0.65f + 0.35f * std::sin (t * 5.0f);
        g.setColour (kRec.withAlpha (a)); g.fillRoundedRectangle (rb.withSizeKeepingCentre (9, 9), 2.0f);
        const int secs = (int) proc.recordedSeconds();
        g.setFont (font (10.0f)); g.setColour (kRec);
        g.drawText (juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2), rb.translated (-62, 0).withWidth (54), juce::Justification::centredRight);
    }
    else
    {
        g.fillEllipse (rb.withSizeKeepingCentre (11, 11));
        if (proc.lastSavedName.isNotEmpty())
        {
            g.setFont (font (8.0f)); g.setColour (kMute);
            g.drawText ("saved in Music / PITCH / Fossils", juce::Rectangle<float> (W - 260, 54, 240, 12), juce::Justification::right);
        }
    }

    // bottom: the drop zone
    const auto dz = dropZone();
    juce::Path zone; zone.addRoundedRectangle (dz, 18.0f);
    juce::Path dashed; const float dashes[2] = { 3.0f, 5.0f };
    juce::PathStrokeType (1.0f).createDashedStroke (dashed, zone, dashes, 2);
    const bool overZone = dragHover && dz.contains (dragPos);
    g.setColour ((dragHover ? kAmber : kInk).withAlpha (overZone ? 0.8f : dragHover ? 0.45f : 0.14f)); g.fillPath (dashed);
    g.setFont (font (9.0f)); g.setColour ((dragHover ? kAmber : kMute).withAlpha (dragHover ? 0.9f : 0.7f));
    g.drawText (dragHover ? "release to create a fossil" : "drop audio here  -  it becomes a fossil", dz, juce::Justification::centred);
    if (dragHover && ! overZone)
    {
        g.setColour (kAmber.withAlpha (0.5f));
        g.drawEllipse (dragPos.x - 24, dragPos.y - 24, 48, 48, 1.0f);
    }
}
