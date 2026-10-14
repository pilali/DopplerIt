/*
 * DopplerIt - JUCE version
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <JuceHeader.h>

#include "age_engine.hpp"
#include "presets.hpp"

namespace ParamID {
    static constexpr const char* mode     = "mode";
    static constexpr const char* time     = "time";
    static constexpr const char* speed    = "speed";
    static constexpr const char* depth    = "depth";
    static constexpr const char* shape    = "shape";
    static constexpr const char* period   = "period";
    static constexpr const char* feedback = "feedback";
    static constexpr const char* tone     = "tone";
    static constexpr const char* mix      = "mix";
    static constexpr const char* output   = "output";
    static constexpr const char* bypass   = "bypass";
}

class DopplerItProcessor : public juce::AudioProcessor
{
public:
    DopplerItProcessor();
    ~DopplerItProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 20.0; }

    // factory presets (src/presets.hpp, shared with the LV2 version)
    int getNumPrograms() override { return dopplerit::kNumPresets; }
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dopplerit::AgeParams currentParams() const;

    dopplerit::AgeDelayEngine engine;
    juce::AudioBuffer<float> scratch; // [0] mono input, [1] spare output
    std::atomic<int> currentProgram { 0 };

    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pTime = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pDepth = nullptr;
    std::atomic<float>* pShape = nullptr;
    std::atomic<float>* pPeriod = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pBypass = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DopplerItProcessor)
};
