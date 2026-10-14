/*
 * DopplerIt - JUCE version
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * The editor reproduces the MOD modgui (dopplerit.lv2/modgui) as closely as
 * possible: same 320 x 576 layout, colours, knob film strip, footswitch and
 * beacon header. Above it, a preset bar plays the role of the preset menu of
 * mod-ui. The window can be resized, the content is scaled.
 */

#pragma once

#include <JuceHeader.h>

#include "PluginProcessor.h"

namespace dpui {

// Segmented selector bound to a choice parameter (Mode, Output)
class SegmentedChoice : public juce::Component
{
public:
    struct Option {
        juce::String label;
        int index;                      // choice index of the parameter
        juce::ColourGradient selected;  // background when selected (local coords 0..1)
    };

    SegmentedChoice(juce::RangedAudioParameter& param, std::vector<Option> options, float fontSize, float tracking);

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    int optionAt(juce::Point<float>) const;

    std::vector<Option> options;
    float fontSize, tracking;
    int current = 0;
    int hover = -1;
    juce::ParameterAttachment attachment;
};

// Rotary knob drawn from the modgui film strip, with title and value
class FilmKnob : public juce::Component
{
public:
    FilmKnob(juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& title,
             const juce::Image& strip);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class Knob : public juce::Slider
    {
    public:
        explicit Knob(const juce::Image& s) : strip(s) {}
        void paint(juce::Graphics&) override;
    private:
        const juce::Image& strip;
    };

    juce::RangedAudioParameter& param;
    juce::String title;
    Knob knob;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

// Bypass footswitch + mini beacon LED
class Footswitch : public juce::Component
{
public:
    Footswitch(juce::RangedAudioParameter&, const juce::Image& sheet);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    std::function<void(bool)> onChange;
    bool isBypassed() const { return bypassed; }

private:
    const juce::Image& sheet;
    bool bypassed = false;
    juce::ParameterAttachment attachment;
};

class BeaconLed : public juce::Component, private juce::Timer
{
public:
    BeaconLed() { startTimer(450); }
    void setActive(bool a) { active = a; repaint(); }
    void paint(juce::Graphics&) override;

private:
    void timerCallback() override { phase = !phase; if (active) repaint(); }
    bool active = true;
    bool phase = false;
};

// The 320 x 576 pedal
class Pedal : public juce::Component
{
public:
    explicit Pedal(DopplerItProcessor&);
    void paint(juce::Graphics&) override;

private:
    juce::Image knobStrip, footswitchSheet;
    std::unique_ptr<juce::Drawable> header;

    SegmentedChoice output, mode;
    FilmKnob time, speed, depth, shape, period, feedback, tone, mix;
    BeaconLed led;
    Footswitch footswitch;
};

// Factory preset selector: previous / next arrows and a menu of the presets
class PresetBar : public juce::Component, private juce::Timer
{
public:
    explicit PresetBar(DopplerItProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    enum Zone { kNone, kPrev, kName, kNext };
    Zone zoneAt(juce::Point<float>) const;
    void select(int index);
    void timerCallback() override; // follows program changes made by the host

    // pop-up menu in the colours and font of the pedal
    struct MenuLook : juce::LookAndFeel_V4
    {
        MenuLook();
        juce::Font getPopupMenuFont() override;
    };

    DopplerItProcessor& proc;
    MenuLook menuLook;
    int shown = -1;
    Zone hover = kNone;
};

} // namespace dpui

class DopplerItEditor : public juce::AudioProcessorEditor
{
public:
    explicit DopplerItEditor(DopplerItProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 320;
    static constexpr int kBarHeight = 30;
    static constexpr int kPedalHeight = 576;
    static constexpr int kHeight = kBarHeight + kPedalHeight;

private:
    dpui::PresetBar presets;
    dpui::Pedal pedal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DopplerItEditor)
};
