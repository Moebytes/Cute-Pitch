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

auto Processor::prepareToPlay(double sampleRate, int samplesPerBlock) -> void {
    this->parameters.prepareToPlay(sampleRate, samplesPerBlock);
    this->parameters.reset();

    this->pitchShifter.configure(getTotalNumInputChannels(), 5120, 1024, false);
    this->copyBuffer.setSize(getTotalNumOutputChannels(), samplesPerBlock);

    this->setLatencySamples(this->pitchShifter.inputLatency() + this->pitchShifter.outputLatency());
}

auto Processor::releaseResources() -> void {}

auto Processor::getHostInfo() noexcept -> std::tuple<double, double, TimeSignature> {
    double bpm = 150.0;
    double ppq = 0.0;
    TimeSignature timeSignature{4, 4};

    if (auto* playhead = this->getPlayHead()) {
        auto info = playhead->getPosition().orFallback(AudioPlayHead::PositionInfo{});
        bpm = info.getBpm().orFallback(150.0);
        ppq = info.getPpqPosition().orFallback(0.0);
        timeSignature = info.getTimeSignature().orFallback(TimeSignature{4, 4});
    }

    return {bpm, ppq, timeSignature};
}

auto Processor::processBlock(AudioBuffer<float>& buffer, [[maybe_unused]] MidiBuffer& midiMessages) -> void {
    ScopedNoDenormals noDenormals;

    auto mainInput = this->getBusBuffer(buffer, true, 0);
    auto mainOutput = this->getBusBuffer(buffer, false, 0);

    auto [bpm, ppq, timeSignature] = this->getHostInfo();
    this->parameters.setHostInfo(bpm, ppq, timeSignature);
    this->parameters.blockUpdate();

    int blocks = this->pitchShifter.blockSamples();
    int interval = this->pitchShifter.intervalSamples();

    if (this->parameters.pitchLFOAmount > 0.0f) {
        if (blocks != 2048 && interval != 384) {
            this->pitchShifter.configure(getTotalNumInputChannels(), 2048, 384, false);
            this->setLatencySamples(this->pitchShifter.inputLatency() + this->pitchShifter.outputLatency());
        }
    } else {
        if (blocks != 5120 && interval != 1024) {
            this->pitchShifter.configure(getTotalNumInputChannels(), 5120, 1024, false);
            this->setLatencySamples(this->pitchShifter.inputLatency() + this->pitchShifter.outputLatency());
        }
    }

    this->copyBuffer.makeCopyOf(buffer);
 
    this->inputSamples[0] = this->copyBuffer.getWritePointer(0);
    this->inputSamples[1] = this->copyBuffer.getNumChannels() > 1 ? this->copyBuffer.getWritePointer(1): this->inputSamples[0];

    this->outputSamples[0] = buffer.getWritePointer(0);
    this->outputSamples[1] = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1): this->outputSamples[0];

    double ppqPerSample = (bpm / 60.0) / getSampleRate();

    int chunkSize = 64;

    for (int sample = 0; sample < buffer.getNumSamples(); sample+=chunkSize) {
        if (ppq > 0.0) {
            double samplePPQ = ppq + sample * ppqPerSample;
            this->parameters.pitchLFO.syncPPQ(samplePPQ);
        }

        this->parameters.update();
        
        this->pitchShifter.setTransposeSemitones(this->parameters.pitch);
        this->pitchShifter.setFormantSemitones(this->parameters.formant, this->parameters.preserveFormant);

        int count = (std::min)(chunkSize, buffer.getNumSamples() - sample);

        float* in[2] = {
            this->inputSamples[0] + sample,
            this->inputSamples[1] + sample
        };

        float* out[2] = {
            this->outputSamples[0] + sample,
            this->outputSamples[1] + sample
        };

        this->pitchShifter.process(in, count, out, count);
    }

    #if JUCE_DEBUG
        Functions::checkAudioSafety(buffer);
    #endif
}