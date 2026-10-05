/*
 * DopplerIt - JUCE version
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * The editor reproduces the MOD modgui (dopplerit.lv2/modgui) as closely as
 * possible: same 320 x 476 layout, colours, knob film strip, footswitch and
 * beacon header. The window can be resized, the content is scaled.
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

// "Loop" slide switch
class SlideSwitch : public juce::Component
{
public:
    explicit SlideSwitch(juce::RangedAudioParameter&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    bool on = false;
    juce::ParameterAttachment attachment;
};

// Momentary "Pass" button (trigger parameter)
class PassButton : public juce::Component
{
public:
    explicit PassButton(juce::RangedAudioParameter&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& param;
    bool down = false;
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

// The 320 x 476 pedal
class Pedal : public juce::Component
{
public:
    explicit Pedal(DopplerItProcessor&);
    void paint(juce::Graphics&) override;

private:
    juce::Image knobStrip, footswitchSheet;
    std::unique_ptr<juce::Drawable> header;

    SegmentedChoice output, mode;
    FilmKnob speed, period, distance, attenuation, width, mix;
    SlideSwitch loop;
    PassButton pass;
    BeaconLed led;
    Footswitch footswitch;
};

} // namespace dpui

class DopplerItEditor : public juce::AudioProcessorEditor
{
public:
    explicit DopplerItEditor(DopplerItProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

    static constexpr int kWidth = 320;
    static constexpr int kHeight = 476;

private:
    dpui::Pedal pedal;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DopplerItEditor)
};
