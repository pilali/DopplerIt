/*
 * DopplerIt - JUCE version
 * Copyright (C) 2026 pilali
 * SPDX-License-Identifier: MIT
 *
 * Same DSP engine as the LV2 plugin (src/delay_engine.hpp), same
 * parameters, ranges and defaults (dopplerit.lv2/dopplerit.ttl).
 */

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

namespace {

// exact logarithmic mapping, like lv2 pprops:logarithmic in mod-ui
NormalisableRange<float> logRange(float lo, float hi, float interval)
{
    return NormalisableRange<float>(lo, hi,
        [](float start, float end, float x) { return start * std::pow(end / start, x); },
        [](float start, float end, float v) { return std::log(v / start) / std::log(end / start); },
        [interval](float start, float end, float v) {
            return jlimit(start, end, interval * std::round(v / interval));
        });
}

AudioParameterFloatAttributes withText(std::function<String(float, int)> fn)
{
    return AudioParameterFloatAttributes().withStringFromValueFunction(std::move(fn));
}

String percent(float v, int) { return String(roundToInt(v)) + "%"; }

} // namespace

AudioProcessorValueTreeState::ParameterLayout DopplerItProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    const int v = 1;

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ParamID::heads, v }, "Heads",
        StringArray { "1 head", "2 heads", "3 heads", "4 heads" }, 1));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::time, v }, "Time",
        logRange(dopplerit::kMinTime, dopplerit::kMaxTime, 0.001f), 0.25f,
        withText([](float x, int) { return String(x, 2) + " s"; })));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::speed, v }, "Speed",
        logRange(dopplerit::kMinSpeedKmh, dopplerit::kMaxSpeedKmh, 0.1f), 100.f,
        withText([](float x, int) { return String(roundToInt(x)) + " km/h"; })));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::distance, v }, "Distance",
        logRange(dopplerit::kMinDistance, dopplerit::kMaxDistance, 0.01f), 5.f,
        withText([](float x, int) { return String(x, 2) + " m"; })));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::period, v }, "Period",
        logRange(dopplerit::kMinPeriod, dopplerit::kMaxPeriod, 0.01f), 2.5f,
        withText([](float x, int) { return String(x, 2) + " s"; })));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::stagger, v }, "Stagger",
        NormalisableRange<float>(0.f, 100.f, 0.1f), 0.f, withText(percent)));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::feedback, v }, "Feedback",
        NormalisableRange<float>(0.f, 100.f, 0.1f), 35.f, withText(percent)));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::tone, v }, "Tone",
        NormalisableRange<float>(0.f, 100.f, 0.1f), 60.f, withText(percent)));

    layout.add(std::make_unique<AudioParameterFloat>(ParameterID { ParamID::mix, v }, "Dry/Wet",
        NormalisableRange<float>(0.f, 100.f, 0.1f), 50.f, withText(percent)));

    layout.add(std::make_unique<AudioParameterChoice>(ParameterID { ParamID::output, v }, "Output",
        StringArray { "Mono", "Stereo" }, 1));

    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ParamID::loop, v }, "Loop", true));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ParamID::trigger, v }, "Pass", false));
    layout.add(std::make_unique<AudioParameterBool>(ParameterID { ParamID::bypass, v }, "Bypass", false));

    return layout;
}

DopplerItProcessor::DopplerItProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", AudioChannelSet::mono(), true)
                         .withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "DopplerIt", createLayout())
{
    pHeads    = apvts.getRawParameterValue(ParamID::heads);
    pTime     = apvts.getRawParameterValue(ParamID::time);
    pSpeed    = apvts.getRawParameterValue(ParamID::speed);
    pDistance = apvts.getRawParameterValue(ParamID::distance);
    pPeriod   = apvts.getRawParameterValue(ParamID::period);
    pStagger  = apvts.getRawParameterValue(ParamID::stagger);
    pFeedback = apvts.getRawParameterValue(ParamID::feedback);
    pTone     = apvts.getRawParameterValue(ParamID::tone);
    pMix      = apvts.getRawParameterValue(ParamID::mix);
    pOutput   = apvts.getRawParameterValue(ParamID::output);
    pLoop     = apvts.getRawParameterValue(ParamID::loop);
    pTrigger  = apvts.getRawParameterValue(ParamID::trigger);
    pBypass   = apvts.getRawParameterValue(ParamID::bypass);
    bypassParam  = dynamic_cast<AudioParameterBool*>(apvts.getParameter(ParamID::bypass));
}

dopplerit::DelayParams DopplerItProcessor::currentParams() const
{
    dopplerit::DelayParams p;
    p.heads    = (int)pHeads->load() + 1;
    p.time     = pTime->load();
    p.speedKmh = pSpeed->load();
    p.distance = pDistance->load();
    p.period   = pPeriod->load();
    p.stagger  = pStagger->load() * 0.01f;
    p.feedback = pFeedback->load() * 0.01f;
    p.tone     = pTone->load() * 0.01f;
    p.mix      = pMix->load() * 0.01f;
    p.loop     = pLoop->load() > 0.5f;
    // a mono output bus always gets the mono effect
    p.stereo   = pOutput->load() > 0.5f && getTotalNumOutputChannels() > 1;
    return p;
}

void DopplerItProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine.release();
    engine.init(sampleRate, 2);
    engine.reset(currentParams());
    scratch.setSize(2, jmax(1, samplesPerBlock), false, true, false);
    lastTrigger = pTrigger->load() > 0.5f;
}

void DopplerItProcessor::releaseResources()
{
    engine.release();
}

bool DopplerItProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    const bool inOk  = in == AudioChannelSet::mono() || in == AudioChannelSet::stereo();
    const bool outOk = out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
    return inOk && outOk;
}

void DopplerItProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    if (n > scratch.getNumSamples())
        scratch.setSize(2, n, false, true, true); // host exceeded the announced block size

    // mono input: sum (and scale) the input channels
    float* in = scratch.getWritePointer(0);
    if (numIn <= 0) {
        FloatVectorOperations::clear(in, n);
    } else {
        FloatVectorOperations::copy(in, buffer.getReadPointer(0), n);
        for (int c = 1; c < numIn; ++c)
            FloatVectorOperations::add(in, buffer.getReadPointer(c), n);
        if (numIn > 1)
            FloatVectorOperations::multiply(in, 1.f / (float)numIn, n);
    }

    const bool trig = pTrigger->load() > 0.5f;
    if (trig && !lastTrigger)
        engine.trigger();
    lastTrigger = trig;

    float* outs[2] = { buffer.getWritePointer(0),
                       numOut > 1 ? buffer.getWritePointer(1) : scratch.getWritePointer(1) };

    engine.process(in, outs, (uint32_t)n, currentParams(), pBypass->load() < 0.5f);

    for (int c = 2; c < numOut; ++c)
        buffer.clear(c, 0, n);
}

void DopplerItProcessor::getStateInformation(MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DopplerItProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(ValueTree::fromXml(*xml));
}

AudioProcessorEditor* DopplerItProcessor::createEditor()
{
    return new DopplerItEditor(*this);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DopplerItProcessor();
}
