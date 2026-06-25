#include "Parameters.h"
#include "Functions.hpp"

template<typename T>
static auto castParameter(const AudioProcessorValueTreeState& tree, 
    const ParameterID* id, T*& dest) -> void {
    dest = dynamic_cast<T*>(tree.getParameter(id->getParamID()));
    jassert(dest != nullptr);
}

template <typename T>
static auto resetParameter(const AudioProcessorValueTreeState& tree, 
    const AudioParameterFloat* param, T*& dest) -> void {
    auto* paramObj = tree.getParameter(param->getParameterID());
    if (paramObj) *dest = paramObj->getDefaultValue();
}

template <typename T>
static auto resetParameter(const AudioProcessorValueTreeState& tree, 
    const AudioParameterBool* param, T*& dest) -> void {
    auto* paramObj = tree.getParameter(param->getParameterID());
    if (paramObj) *dest = paramObj->getDefaultValue();
}

template <typename T>
static auto resetParameter(const AudioProcessorValueTreeState& tree, 
    const AudioParameterChoice* param, T*& dest) -> void {
    auto* paramObj = tree.getParameter(param->getParameterID());
    if (paramObj) *dest = static_cast<T>(paramObj->getDefaultValue());
}

ParameterIDs Parameters::paramIDs = ParameterIDs::loadFromJSON();

Parameters::Parameters(AudioProcessorValueTreeState& tree) : tree(tree) {
    using FloatPair = std::pair<AudioParameterFloat*&, const ParameterID*>;
    using BoolPair = std::pair<AudioParameterBool*&, const ParameterID*>;
    using ChoicePair = std::pair<AudioParameterChoice*&, const ParameterID*>;

    auto floatParameters = std::vector<FloatPair>{
        {pitchParam, &paramIDs.pitch},
        {formantParam, &paramIDs.formant},
        {pitchLFORateParam, &paramIDs.pitchLFORate},
        {pitchLFOAmountParam, &paramIDs.pitchLFOAmount},
    };

    auto boolParameters = std::vector<BoolPair>{
        {pitchLFOInvertParam, &paramIDs.pitchLFOInvert}
    };

    auto choiceParameters = std::vector<ChoicePair>{
        {pitchLFOTypeParam, &paramIDs.pitchLFOType}
    };

    for (const auto& [param, paramID] : floatParameters) {
        castParameter(tree, paramID, param);
    }

    for (auto& [param, paramID] : boolParameters) {
        castParameter(tree, paramID, param);
    }

    for (auto& [param, paramID] : choiceParameters) {
        castParameter(tree, paramID, param);
    }
}

auto Parameters::createParameterLayout() -> AudioProcessorValueTreeState::ParameterLayout {
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterFloat>(
        paramIDs.pitch, "Pitch", NormalisableRange<float>{-24.0f, 24.0f, 0.0f}, 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(Functions::displaySemitones)
        .withValueFromStringFunction(Functions::parseSemitones)
    ));

    layout.add(std::make_unique<AudioParameterFloat>(
        paramIDs.formant, "Formant", NormalisableRange<float>{-24.0f, 24.0f, 0.0f}, 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(Functions::displaySemitones)
        .withValueFromStringFunction(Functions::parseSemitones)
    ));

    layout.add(std::make_unique<AudioParameterChoice>(
        paramIDs.pitchLFOType, "Pitch LFO Type", StringArray{"square", "saw", "triangle", "sine"}, 0
    ));

    layout.add(std::make_unique<AudioParameterFloat>(
        paramIDs.pitchLFORate, "Pitch LFO Rate", NormalisableRange<float>{0.03125f, 4.0f, 0.0001f}, 0.25f,
        AudioParameterFloatAttributes().withStringFromValueFunction(Functions::displayLFORate)
        .withValueFromStringFunction(Functions::parseLFORate)
    ));

    layout.add(std::make_unique<AudioParameterFloat>(
        paramIDs.pitchLFOAmount, "Pitch LFO Amount", NormalisableRange<float>{0.0f, 1.0f, 0.01f}, 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction(Functions::displayPercent)
        .withValueFromStringFunction(Functions::parsePercent)
    ));

    layout.add(std::make_unique<AudioParameterBool>(
        paramIDs.pitchLFOInvert, "Pitch LFO Invert", false
    ));

    return layout;
}

auto Parameters::getDefaultParameter(const Array<var>& args,
    WebBrowserComponent::NativeFunctionCompletion completion) -> void {

    auto paramID = args[0].toString();
    auto* param = this->tree.getParameter(paramID);
    float defaultValue = param->convertFrom0to1(param->getDefaultValue());

    completion(defaultValue);
}

auto Parameters::prepareToPlay(double sampleRate, int blockSize) noexcept -> void {
    this->sampleRate = sampleRate;
    this->blockSize = blockSize;

    double duration = 0.001;

    auto smoothers = std::vector{
        &pitchSmoother,
        &formantSmoother,
        &pitchLFOAmountSmoother
    };

    for (const auto& smoother : smoothers) {
        smoother->reset(this->sampleRate, duration);
    }

    this->pitchLFO.prepareToPlay(this->sampleRate);
}

auto Parameters::reset() noexcept -> void {
    auto paramFloats = std::vector{
        std::pair{pitchParam, &pitch},
        std::pair{formantParam, &formant}
    };

    for (auto& [param, value] : paramFloats) {
        resetParameter(tree, param, value);
    }
    
    auto smoothers = std::vector{
        std::pair{pitchParam, &pitchSmoother},
        std::pair{formantParam, &formantSmoother},
        std::pair{pitchLFOAmountParam, &pitchLFOAmountSmoother}
    };

    for (const auto& [param, smoother] : smoothers) {
        smoother->setCurrentAndTargetValue(param->get());
    }

    this->pitchLFO.reset();
}

auto Parameters::setHostInfo(double bpm, double ppq, const AudioPlayHead::TimeSignature& timeSignature) noexcept -> void {
    this->bpm = bpm;
    this->ppq = ppq;
    this->timeSignature = timeSignature;

    if (ppq > 0.0) {
        this->ppq = ppq;
        this->internalPPQ = this->ppq;
    } else {
        double ppqPerSample = (this->bpm / 60.0) / this->sampleRate;
        this->internalPPQ += ppqPerSample * this->blockSize; 
        this->ppq = this->internalPPQ;
    }

    this->pitchLFO.syncToHost(this->bpm, this->ppq, this->timeSignature);
}

auto Parameters::blockUpdate() noexcept -> void {
    auto smoothers = std::vector{
        std::pair{pitchParam, &pitchSmoother},
        std::pair{formantParam, &formantSmoother},
        std::pair{pitchLFOAmountParam, &pitchLFOAmountSmoother}
    };

    for (const auto& [param, smoother] : smoothers) {
        smoother->setTargetValue(param->get());
    }

    this->pitchLFO.setType(this->pitchLFOTypeParam->getCurrentChoiceName());
    this->pitchLFO.setSyncedRate(this->pitchLFORateParam->get());
    this->pitchLFO.setPhaseInvert(this->pitchLFOInvertParam->get());
}

auto Parameters::update() noexcept -> void {
    this->pitch = pitchSmoother.getNextValue();
    this->formant = formantSmoother.getNextValue();

    float pitchLFOValue = this->pitchLFO.getSample();
    this->pitchLFOAmount = this->pitchLFOAmountSmoother.getNextValue();

    this->pitch *= jmap(pitchLFOValue, -1.0f, 1.0f, 1.0f - this->pitchLFOAmount, 1.0f);
    this->formant *= jmap(pitchLFOValue, -1.0f, 1.0f, 1.0f - this->pitchLFOAmount, 1.0f);
}