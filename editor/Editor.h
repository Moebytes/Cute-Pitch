#pragma once
#pragma clang diagnostic ignored "-Wshadow-field"
#include <JuceHeader.h>
#include "Processor.h"
#include "EventEmitter.hpp"

class Editor : public AudioProcessorEditor, public EventEmitter::Listener {
public:
    Editor(Processor& p);
    ~Editor() override;
    
    auto resized() -> void override;

    auto getResource(const String& url) -> std::optional<WebBrowserComponent::Resource>;
    auto webviewOptions() -> WebBrowserComponent::Options;
    auto getWebviewFileBytes(const String& resourceStr) -> std::vector<std::byte>;

    auto handleEvent(const String& name, const var& payload) -> void override;
    auto handleThemeChange(const String& theme) -> void;
        
private:
    Processor& processor;
    ComponentBoundsConstrainer constrainer;

    WebSliderRelay pitchRelay {Parameters::paramIDs.pitch.getParamID()};
    WebSliderParameterAttachment pitchAttachment {*this->processor.parameters.pitchParam, pitchRelay, nullptr};

    WebSliderRelay formantRelay {Parameters::paramIDs.formant.getParamID()};
    WebSliderParameterAttachment formantAttachment {*this->processor.parameters.formantParam, formantRelay, nullptr};

    WebComboBoxRelay pitchLFOTypeRelay {Parameters::paramIDs.pitchLFOType.getParamID()};
    WebComboBoxParameterAttachment pitchLFOTypeAttachment {*this->processor.parameters.pitchLFOTypeParam, pitchLFOTypeRelay, nullptr};
    WebSliderRelay pitchLFORateRelay {Parameters::paramIDs.pitchLFORate.getParamID()};
    WebSliderParameterAttachment pitchLFORateAttachment {*this->processor.parameters.pitchLFORateParam, pitchLFORateRelay, nullptr};
    WebSliderRelay pitchLFOAmountRelay {Parameters::paramIDs.pitchLFOAmount.getParamID()};
    WebSliderParameterAttachment pitchLFOAmountAttachment {*this->processor.parameters.pitchLFOAmountParam, pitchLFOAmountRelay, nullptr};
    WebToggleButtonRelay pitchLFOInvertRelay {Parameters::paramIDs.pitchLFOInvert.getParamID()};
    WebToggleButtonParameterAttachment pitchLFOInvertAttachment {*this->processor.parameters.pitchLFOInvertParam, pitchLFOInvertRelay, nullptr};

    WebBrowserComponent webview;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};