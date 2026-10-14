/*
 * DopplerIt - JUCE version
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Coordinates and colours mirror dopplerit.lv2/modgui/stylesheet-dopplerit.css
 */

#include "PluginEditor.h"

using namespace juce;

namespace dpui {

namespace {

const Colour kTitleText  { 0xffc9d1dc };
const Colour kValueText  { 0xff7fb6ff };
const Colour kWell       { 0xff0f1217 };
const Colour kOutline    { 0xff0b0d11 };
const Colour kBlue       { 0xff40a0ff };
const Colour kRed        { 0xffff5448 };

// "Cooper Hewitt": mod-ui forces it on every element, modgui included
// (Book = normal, Semibold = bold). SIL OFL, see Assets/fonts/OFL.txt
Typeface::Ptr cooperHewitt(bool bold)
{
    static Typeface::Ptr book = Typeface::createSystemTypefaceFor(
        BinaryData::CooperHewittBook_ttf, BinaryData::CooperHewittBook_ttfSize);
    static Typeface::Ptr semibold = Typeface::createSystemTypefaceFor(
        BinaryData::CooperHewittSemibold_ttf, BinaryData::CooperHewittSemibold_ttfSize);
    return bold ? semibold : book;
}

// Cooper Hewitt: ascent + descent = 1.20 em = CSS "normal" line height
constexpr float kLineHeight = 1.2f;

// CSS px font size -> JUCE font, with letter-spacing in px
Font cssFont(float px, bool bold, float letterSpacingPx = 0.f)
{
    Font f(FontOptions(cooperHewitt(bold)).withPointHeight(px)); // point height = em size = CSS font-size
    if (letterSpacingPx > 0.f)
        f.setExtraKerningFactor(letterSpacingPx / f.getHeight());
    return f;
}

float textWidth(const Font& f, const String& s)
{
    return GlyphArrangement::getStringWidth(f, s);
}

// vertical CSS gradient "180deg, top -> bottom" over r
ColourGradient vgrad(Colour top, Colour bottom, Rectangle<float> r)
{
    return ColourGradient(top, r.getX(), r.getY(), bottom, r.getX(), r.getBottom(), false);
}

// horizontal CSS gradient "90deg, left -> right" over r
ColourGradient hgrad(Colour left, Colour right, Rectangle<float> r)
{
    return ColourGradient(left, r.getX(), r.getY(), right, r.getRight(), r.getY(), false);
}

// Inset "well" used by the selectors and the switch
void paintWell(Graphics& g, Rectangle<float> r, float radius)
{
    g.setColour(kWell);
    g.fillRoundedRectangle(r, radius);
    // inset shadow (box-shadow: inset 0 1px 3px rgba(0,0,0,.8))
    g.setColour(Colours::black.withAlpha(0.6f));
    g.drawRoundedRectangle(r.reduced(0.5f).translated(0.f, 0.5f), radius, 1.5f);
    // bottom highlight (0 1px 0 rgba(255,255,255,.06))
    g.setColour(Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine((int)r.getBottom(), r.getX() + radius, r.getRight() - radius);
}

Image loadImage(const void* data, int size)
{
    return ImageCache::getFromMemory(data, size);
}

} // namespace

//==============================================================================
SegmentedChoice::SegmentedChoice(RangedAudioParameter& p, std::vector<Option> opts, float fs, float tr)
    : options(std::move(opts)), fontSize(fs), tracking(tr),
      attachment(p, [this](float v) { current = roundToInt(v); repaint(); })
{
    attachment.sendInitialUpdate();
    setMouseCursor(MouseCursor::PointingHandCursor);
}

int SegmentedChoice::optionAt(Point<float> p) const
{
    const float w = (float)getWidth() / (float)options.size();
    return jlimit(0, (int)options.size() - 1, (int)(p.x / w));
}

void SegmentedChoice::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float radius = bounds.getHeight() * 0.5f;
    paintWell(g, bounds, radius);

    Path clip;
    clip.addRoundedRectangle(bounds, radius);
    g.saveState();
    g.reduceClipRegion(clip);

    const float w = bounds.getWidth() / (float)options.size();
    g.setFont(cssFont(fontSize, true, tracking));

    for (size_t i = 0; i < options.size(); ++i)
    {
        const auto& o = options[i];
        const auto r = Rectangle<float>((float)i * w, 0.f, w, bounds.getHeight());
        const bool sel = o.index == current;

        if (sel)
        {
            auto grad = o.selected;
            grad.point1 = { r.getX() + grad.point1.x * r.getWidth(), r.getY() + grad.point1.y * r.getHeight() };
            grad.point2 = { r.getX() + grad.point2.x * r.getWidth(), r.getY() + grad.point2.y * r.getHeight() };
            g.setGradientFill(grad);
            g.fillRect(r);
        }

        g.setColour(sel ? Colours::white : ((int)i == hover ? Colour(0xffb8c1ce) : Colour(0xff6f7887)));
        g.drawText(o.label.toUpperCase(), r, Justification::centred, false);
    }
    g.restoreState();
}

void SegmentedChoice::mouseDown(const MouseEvent& e)
{
    attachment.setValueAsCompleteGesture((float)options[(size_t)optionAt(e.position)].index);
}

void SegmentedChoice::mouseMove(const MouseEvent& e)
{
    const int h = optionAt(e.position);
    if (h != hover) { hover = h; repaint(); }
}

void SegmentedChoice::mouseExit(const MouseEvent&)
{
    hover = -1;
    repaint();
}

//==============================================================================
void FilmKnob::Knob::paint(Graphics& g)
{
    const int frames = strip.getWidth() / strip.getHeight();
    const double pos = valueToProportionOfLength(getValue());
    const int frame = jlimit(0, frames - 1, roundToInt(pos * (frames - 1)));
    const int fs = strip.getHeight();
    g.setImageResamplingQuality(Graphics::highResamplingQuality);
    g.drawImage(strip, 0, 0, getWidth(), getHeight(), frame * fs, 0, fs, fs);
}

FilmKnob::FilmKnob(AudioProcessorValueTreeState& state, const String& id, const String& t, const Image& strip)
    : param(*state.getParameter(id)), title(t), knob(strip),
      attachment(state, id, knob)
{
    knob.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    knob.setMouseDragSensitivity(200);
    knob.setDoubleClickReturnValue(true, param.convertFrom0to1(param.getDefaultValue()));
    knob.setMouseCursor(MouseCursor::PointingHandCursor);
    knob.setPopupDisplayEnabled(false, false, nullptr);
    knob.onValueChange = [this] { repaint(); };
    knob.setTooltip(t);
    addAndMakeVisible(knob);
}

void FilmKnob::resized()
{
    // CSS: 64 x 64 knob centred at the top of a 100 x 100 group
    knob.setBounds((getWidth() - 64) / 2, 0, 64, 64);
}

void FilmKnob::paint(Graphics& g)
{
    g.setColour(kTitleText);
    g.setFont(cssFont(11.f, true, 1.f));
    const float titleH = 11.f * kLineHeight;
    g.drawText(title.toUpperCase(), Rectangle<float>(0.f, 66.f, (float)getWidth(), titleH), Justification::centred, false);

    g.setColour(kValueText);
    g.setFont(cssFont(11.f, false));
    g.drawText(param.getCurrentValueAsText(), Rectangle<float>(0.f, 66.f + titleH, (float)getWidth(), 15.f),
               Justification::centred, false);
}

//==============================================================================
Footswitch::Footswitch(RangedAudioParameter& p, const Image& s)
    : sheet(s),
      attachment(p, [this](float v) {
          bypassed = v > 0.5f;
          repaint();
          if (onChange) onChange(bypassed);
      })
{
    setMouseCursor(MouseCursor::PointingHandCursor);
}

void Footswitch::paint(Graphics& g)
{
    // sheet: top frame = pressed (bypassed), bottom = released
    const int fw = sheet.getWidth();
    g.setImageResamplingQuality(Graphics::highResamplingQuality);
    g.drawImage(sheet, 0, 0, getWidth(), getHeight(), 0, bypassed ? 0 : fw, fw, fw);
}

void Footswitch::mouseDown(const MouseEvent&)
{
    attachment.setValueAsCompleteGesture(bypassed ? 0.f : 1.f);
}

//==============================================================================
void BeaconLed::paint(Graphics& g)
{
    // 20 x 16 dome inside a component with room for the glow
    const auto dome = Rectangle<float>(20.f, 20.f, 20.f, 16.f);
    Path p;
    p.addRoundedRectangle(dome.getX(), dome.getY(), dome.getWidth(), dome.getHeight(), 10.f, 10.f,
                          true, true, false, false);

    if (active)
    {
        // CSS keyframes: blue glow on the left, then red glow on the right
        const Colour glow = phase ? kRed.withAlpha(0.85f) : kBlue.withAlpha(0.85f);
        Path big(p);
        big.applyTransform(AffineTransform::scale(1.3f, 1.35f, dome.getCentreX(), dome.getCentreY())
                               .translated(phase ? 6.f : -6.f, -2.f));
        DropShadow(glow, 12, {}).drawForPath(g, big);

        ColourGradient grad = hgrad(Colour(0xff7fc0ff), Colour(0xffff8a7f), dome);
        grad.addColour(0.49, Colour(0xff2f7fe0));
        grad.addColour(0.51, Colour(0xffe0443a));
        g.setGradientFill(grad);
    }
    else
    {
        ColourGradient grad = hgrad(Colour(0xff1c2a3a), Colour(0xff3a1512), dome);
        grad.addColour(0.5, Colour(0xff1c2a3a));
        grad.addColour(0.5001, Colour(0xff3a1512));
        g.setGradientFill(grad);
    }
    g.fillPath(p);
    g.setColour(kOutline);
    g.strokePath(p, PathStrokeType(1.f));
}

//==============================================================================
Pedal::Pedal(DopplerItProcessor& proc)
    : knobStrip(loadImage(BinaryData::knobdopplerit_png, BinaryData::knobdopplerit_pngSize)),
      footswitchSheet(loadImage(BinaryData::footswitchdopplerit_png, BinaryData::footswitchdopplerit_pngSize)),
      header(Drawable::createFromImageData(BinaryData::header_svg, BinaryData::header_svgSize)),
      output(*proc.apvts.getParameter(ParamID::output),
             { { "Mono", 0, vgrad(Colour(0xff5b6474), Colour(0xff3a414e), { 0, 0, 1, 1 }) },
               { "Stereo", 1, vgrad(Colour(0xff5b6474), Colour(0xff3a414e), { 0, 0, 1, 1 }) } },
             9.f, 1.5f),
      mode(*proc.apvts.getParameter(ParamID::mode),
           { { "Approach", 0, vgrad(Colour(0xff3f8fe0), Colour(0xff2a6bb0), { 0, 0, 1, 1 }) },
             { "Pass-by", 2, hgrad(Colour(0xff3f8fe0), Colour(0xffe0544a), { 0, 0, 1, 1 }) },
             { "Recede", 1, vgrad(Colour(0xffe0574c), Colour(0xffa83a32), { 0, 0, 1, 1 }) } },
           11.f, 1.f),
      time(proc.apvts, ParamID::time, "Time", knobStrip),
      speed(proc.apvts, ParamID::speed, "Speed", knobStrip),
      depth(proc.apvts, ParamID::depth, "Depth", knobStrip),
      shape(proc.apvts, ParamID::shape, "Shape", knobStrip),
      period(proc.apvts, ParamID::period, "Period", knobStrip),
      feedback(proc.apvts, ParamID::feedback, "Feedback", knobStrip),
      tone(proc.apvts, ParamID::tone, "Tone", knobStrip),
      mix(proc.apvts, ParamID::mix, "Dry/Wet", knobStrip),
      footswitch(*proc.apvts.getParameter(ParamID::bypass), footswitchSheet)
{
    // same coordinates as the modgui stylesheet
    output.setBounds(20, 74, 120, 18);
    mode.setBounds(20, 118, 280, 28);

    time.setBounds(10, 154, 100, 100);
    speed.setBounds(110, 154, 100, 100);
    depth.setBounds(210, 154, 100, 100);
    shape.setBounds(10, 258, 100, 100);
    period.setBounds(110, 258, 100, 100);
    feedback.setBounds(210, 258, 100, 100);
    tone.setBounds(60, 362, 100, 100);
    mix.setBounds(160, 362, 100, 100);

    led.setBounds(150 - 20, 468 - 20, 60, 56);
    led.setInterceptsMouseClicks(false, false);
    footswitch.setBounds(125, 494, 70, 70);
    footswitch.onChange = [this](bool bypassed) { led.setActive(!bypassed); };
    led.setActive(!footswitch.isBypassed());

    for (auto* c : std::initializer_list<Component*> { &output, &mode, &time, &speed, &depth, &shape, &period,
                                                       &feedback, &tone, &mix, &led, &footswitch })
        addAndMakeVisible(c);
}

void Pedal::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float radius = 14.f;

    // body
    g.setColour(kOutline);
    g.fillRoundedRectangle(bounds, radius);
    const auto body = bounds.reduced(1.f);
    ColourGradient bg = vgrad(Colour(0xff2a303c), Colour(0xff14171d), body);
    bg.addColour(0.55, Colour(0xff1b1f27));
    g.setGradientFill(bg);
    g.fillRoundedRectangle(body, radius - 1.f);
    g.setGradientFill(ColourGradient(kBlue.withAlpha(0.10f), body.getCentreX(), 0.f,
                                     kBlue.withAlpha(0.f), body.getCentreX(), 200.f, true));
    g.fillRoundedRectangle(body, radius - 1.f);
    g.setColour(Colours::white.withAlpha(0.12f));
    g.drawHorizontalLine(1, radius, bounds.getWidth() - radius);

    // header
    const auto head = Rectangle<float>(0.f, 0.f, bounds.getWidth(), 104.f);
    Path headClip;
    headClip.addRoundedRectangle(head.getX(), head.getY(), head.getWidth(), head.getHeight(), radius, radius,
                                 true, true, false, false);
    g.saveState();
    g.reduceClipRegion(headClip);
    g.setGradientFill(vgrad(Colour(0xff11141a), Colour(0xff181c24), head));
    g.fillRect(head);
    if (header != nullptr)
        header->draw(g, 1.f); // SVG viewBox = header coordinates (320 x 104)
    g.restoreState();
    g.setColour(kOutline);
    g.drawHorizontalLine(104, 0.f, bounds.getWidth());
    g.setColour(Colours::white.withAlpha(0.06f));
    g.drawHorizontalLine(105, 0.f, bounds.getWidth());

    // brand
    g.setColour(Colour(0xff7d8796));
    g.setFont(cssFont(11.f, false, 3.f));
    g.drawText("PILALI", Rectangle<float>(20.f, 14.f, 120.f, 11.f * kLineHeight), Justification::centredLeft, false);

    // title "Doppler" + gradient "It"
    const Font titleFont = cssFont(36.f, true, 0.5f);
    const auto titleBox = Rectangle<float>(18.f, 30.f, 220.f, 40.f);
    const float w1 = textWidth(titleFont, "Doppler");
    const float w2 = textWidth(titleFont, "It");
    g.setFont(titleFont);
    g.setColour(Colours::black.withAlpha(0.45f));
    g.drawText("Doppler", titleBox.translated(0.f, 2.f), Justification::centredLeft, false);
    g.drawText("It", titleBox.translated(w1, 2.f), Justification::centredLeft, false);
    g.setColour(Colour(0xfff2f5fa));
    g.drawText("Doppler", titleBox, Justification::centredLeft, false);
    g.setGradientFill(hgrad(kBlue, kRed, { titleBox.getX() + w1, 0.f, w2, 1.f }));
    g.drawText("It", titleBox.translated(w1, 0.f), Justification::centredLeft, false);
}

//==============================================================================
PresetBar::MenuLook::MenuLook()
{
    setColour(PopupMenu::backgroundColourId, Colour(0xff181c24));
    setColour(PopupMenu::textColourId, kTitleText);
    setColour(PopupMenu::headerTextColourId, Colour(0xff7d8796));
    setColour(PopupMenu::highlightedBackgroundColourId, Colour(0xff2a6bb0));
    setColour(PopupMenu::highlightedTextColourId, Colours::white);
}

Font PresetBar::MenuLook::getPopupMenuFont()
{
    return cssFont(13.f, false);
}

PresetBar::PresetBar(DopplerItProcessor& p) : proc(p)
{
    setMouseCursor(MouseCursor::PointingHandCursor);
    startTimerHz(10);
}

PresetBar::Zone PresetBar::zoneAt(Point<float> p) const
{
    if (p.x < 40.f) return kPrev;
    if (p.x > (float)getWidth() - 40.f) return kNext;
    return kName;
}

void PresetBar::paint(Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setGradientFill(vgrad(Colour(0xff181c24), Colour(0xff11141a), bounds));
    g.fillRect(bounds);

    const auto well = Rectangle<float>(10.f, 4.f, bounds.getWidth() - 20.f, 22.f);
    paintWell(g, well, 11.f);

    // arrows
    for (int i = 0; i < 2; ++i)
    {
        const bool next = i == 1;
        const float cx = next ? well.getRight() - 15.f : well.getX() + 15.f, cy = well.getCentreY();
        Path tri;
        tri.addTriangle(next ? cx + 4.f : cx - 4.f, cy, next ? cx - 3.f : cx + 3.f, cy - 5.f,
                        next ? cx - 3.f : cx + 3.f, cy + 5.f);
        g.setColour(hover == (next ? kNext : kPrev) ? Colour(0xffffffff) : Colour(0xff6f7887));
        g.fillPath(tri);
    }

    const int index = proc.getCurrentProgram();
    const auto text = well.reduced(30.f, 0.f);
    g.setColour(Colour(0xff6f7887));
    g.setFont(cssFont(9.f, true, 1.5f));
    g.drawText(String(index + 1).paddedLeft('0', 2) + "/" + String(dopplerit::kNumPresets), text.withTrimmedLeft(4.f),
               Justification::centredLeft, false);
    g.setColour(hover == kName ? Colours::white : kValueText);
    g.setFont(cssFont(11.f, true, 1.f));
    g.drawText(proc.getProgramName(index).toUpperCase(), text, Justification::centred, true);
}

void PresetBar::select(int index)
{
    const int n = dopplerit::kNumPresets;
    proc.setCurrentProgram(((index % n) + n) % n);
    proc.updateHostDisplay(AudioProcessor::ChangeDetails().withProgramChanged(true));
    repaint();
}

void PresetBar::mouseDown(const MouseEvent& e)
{
    const int current = proc.getCurrentProgram();
    switch (zoneAt(e.position))
    {
    case kPrev: select(current - 1); return;
    case kNext: select(current + 1); return;
    case kName: break;
    case kNone: return;
    }

    PopupMenu menu;
    for (int gi = 0; gi < dopplerit::kNumPresetGroups; ++gi)
    {
        const int first = dopplerit::kPresetGroups[gi].first;
        const int end = gi + 1 < dopplerit::kNumPresetGroups ? dopplerit::kPresetGroups[gi + 1].first : dopplerit::kNumPresets;
        menu.addSectionHeader(dopplerit::kPresetGroups[gi].name);
        for (int i = first; i < end; ++i)
            menu.addItem(i + 1, proc.getProgramName(i), true, i == current);
    }
    SafePointer<PresetBar> self(this);
    menu.setLookAndFeel(&menuLook);
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this).withMinimumWidth(getWidth()),
                       [self](int result) { if (self != nullptr && result > 0) self->select(result - 1); });
}

void PresetBar::mouseMove(const MouseEvent& e)
{
    const Zone z = zoneAt(e.position);
    if (z != hover) { hover = z; repaint(); }
}

void PresetBar::mouseExit(const MouseEvent&)
{
    hover = kNone;
    repaint();
}

void PresetBar::timerCallback()
{
    const int index = proc.getCurrentProgram();
    if (index != shown) { shown = index; repaint(); }
}

} // namespace dpui

//==============================================================================
DopplerItEditor::DopplerItEditor(DopplerItProcessor& p)
    : AudioProcessorEditor(p), presets(p), pedal(p)
{
    addAndMakeVisible(presets);
    addAndMakeVisible(pedal);
    presets.setBounds(0, 0, kWidth, kBarHeight);
    pedal.setBounds(0, kBarHeight, kWidth, kPedalHeight);

    setResizable(true, true);
    setResizeLimits(kWidth * 3 / 4, kHeight * 3 / 4, kWidth * 3, kHeight * 3);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio((double)kWidth / (double)kHeight);
    setSize(kWidth, kHeight);
}

void DopplerItEditor::paint(Graphics& g)
{
    g.fillAll(Colour(0xff0b0d11));
}

void DopplerItEditor::resized()
{
    const float scale = (float)getWidth() / (float)kWidth;
    presets.setTransform(AffineTransform::scale(scale));
    pedal.setTransform(AffineTransform::scale(scale));
}
