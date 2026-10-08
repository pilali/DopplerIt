/*
 * DopplerIt - JUCE version
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <JuceHeader.h>

#include "delay_engine.hpp"

namespace ParamID {
    static constexpr const char* mode     = "mode";
    static constexpr const char* heads    = "heads";
    static constexpr const char* time     = "time";
    static constexpr const char* speed    = "speed";
    static constexpr const char* distance = "distance";
    static constexpr const char* period   = "period";
    static constexpr const char* stagger  = "stagger";
    static constexpr const char* feedback = "feedback";
    static constexpr const char* tone     = "tone";
    static constexpr const char* mix      = "mix";
    static constexpr const char* output   = "output";
    static constexpr const char* loop     = "loop";
    static constexpr const char* trigger  = "trigger";
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
    double getTailLengthSeconds() const override { return 12.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    dopplerit::DelayParams currentParams() const;

    dopplerit::DelayEngine engine;
    juce::AudioBuffer<float> scratch; // [0] mono input, [1] spare output
    bool lastTrigger = false;

    std::atomic<float>* pMode = nullptr;
    std::atomic<float>* pHeads = nullptr;
    std::atomic<float>* pTime = nullptr;
    std::atomic<float>* pSpeed = nullptr;
    std::atomic<float>* pDistance = nullptr;
    std::atomic<float>* pPeriod = nullptr;
    std::atomic<float>* pStagger = nullptr;
    std::atomic<float>* pFeedback = nullptr;
    std::atomic<float>* pTone = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pLoop = nullptr;
    std::atomic<float>* pTrigger = nullptr;
    std::atomic<float>* pBypass = nullptr;
    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DopplerItProcessor)
};
