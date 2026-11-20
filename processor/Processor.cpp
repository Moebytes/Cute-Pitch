#include "Processor.h"
#include "Editor.h"
#include "Functions.hpp"

Processor::Processor() : AudioProcessor(
    BusesProperties()
        .withInput("Input", AudioChannelSet::stereo(), true)
        .withOutput("Output", AudioChannelSet::stereo(), true)
    ), parameters(tree), presetManager(tree) {
}

Processor::~Processor() {}

auto Processor::isBusesLayoutSupported(const BusesLayout& layouts) const -> bool {
    auto mono = AudioChannelSet::mono();
    auto stereo = AudioChannelSet::stereo();
    auto mainIn = layouts.getMainInputChannelSet();
    auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn == mono && mainOut == mono) return true;
    if (mainIn == mono && mainOut == stereo) return true;
    if (mainIn == stereo && mainOut == stereo) return true;
    return false;
}

auto Processor::getNumPrograms() -> int {
    return static_cast<int>(this->presetManager.factoryPresets.size());
}

auto Processor::getCurrentProgram() -> int {
    return this->presetManager.presetIndex;
}

auto Processor::setCurrentProgram(int index) -> void {
    this->presetManager.presetFolder = "factory";
    this->presetManager.setPreset(index);
}

auto Processor::getProgramName(int index) -> const String {
    int safeIndex = jlimit(0, this->getNumPrograms() - 1, index);
    auto presetName = this->presetManager.factoryPresetNames[static_cast<size_t>(safeIndex)];
    return Functions::replaceChar(presetName, '/', '-');
}

auto Processor::changeProgramName([[maybe_unused]] int index, [[maybe_unused]] const String& newName) -> void {}

auto Processor::createEditor() -> AudioProcessorEditor* {
    return new Editor(*this);
}

auto Processor::getStateInformation(MemoryBlock& destData) -> void {
    auto jsonStr = this->presetManager.savePreset();
    destData.replaceAll(jsonStr.toUTF8(), jsonStr.getNumBytesAsUTF8());
}

auto Processor::setStateInformation(const void* data, int sizeInBytes) -> void {
    auto jsonStr = String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    this->presetManager.loadPreset(jsonStr);
}

auto JUCE_CALLTYPE createPluginFilter() -> AudioProcessor* {
    return new Processor();
}

auto Processor::prepareToPlay(double sampleRate, int) -> void {
    this->parameters.prepareToPlay(sampleRate);
    this->parameters.reset();

    this->pitchShifter.prepareToPlay(sampleRate, getTotalNumInputChannels());
    this->setLatencySamples(this->pitchShifter.latencySamples());
}

auto Processor::reset() -> void {
    this->pitchShifter.reset();
}

auto Processor::releaseResources() -> void {}

auto Processor::getHostInfo() noexcept -> std::tuple<double, double, TimeSignature> {
    double bpm = 150.0;
    double ppq = -1.0;
    TimeSignature timeSignature{4, 4};

    if (auto* playhead = this->getPlayHead()) {
        auto info = playhead->getPosition().orFallback(AudioPlayHead::PositionInfo{});
        bpm = info.getBpm().orFallback(150.0);
        ppq = info.getPpqPosition().orFallback(-1.0);
        timeSignature = info.getTimeSignature().orFallback(TimeSignature{4, 4});
    }

    return {bpm, ppq, timeSignature};
}

auto Processor::processBlock(AudioBuffer<float>& buffer, [[maybe_unused]] MidiBuffer& midiMessages) -> void {
    ScopedNoDenormals noDenormals;

    auto [bpm, ppq, timeSignature] = this->getHostInfo();
    this->parameters.setHostInfo(bpm, ppq, timeSignature);
    this->parameters.blockUpdate();

    constexpr int chunkSize = 64;
    for (int sample = 0; sample < buffer.getNumSamples(); sample += chunkSize) {
        int count = juce::jmin(chunkSize, buffer.getNumSamples() - sample);
        this->parameters.update(count);
        this->pitchShifter.process(buffer, sample, count, this->parameters.pitch,
            this->parameters.formant, this->parameters.preserveFormant);
    }

    if (getTotalNumInputChannels() == 1 && getTotalNumOutputChannels() == 2) {
        buffer.copyFrom(1, 0, buffer, 0, 0, buffer.getNumSamples());
    }

    #if JUCE_DEBUG
        Functions::checkAudioSafety(buffer);
    #endif
}