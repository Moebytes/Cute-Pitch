#pragma once
#include <JuceHeader.h>

using Complex = std::complex<float>;

struct Channel {
    HeapBlock<float> input;
    HeapBlock<float> output;
    HeapBlock<Complex> spectrum;
    HeapBlock<Complex> previousSpectrum;
    HeapBlock<Complex> shifted;
};

class PitchShifter {
public:
    PitchShifter() = default;
    virtual ~PitchShifter() = default;

    auto prepareToPlay(double sampleRate, int numChannels) -> void {
        int order = std::clamp(static_cast<int>(std::round(std::log2(sampleRate * 0.09))), 9, 16);
        this->channels.resize(static_cast<size_t>(numChannels));
        this->fft = std::make_unique<dsp::FFT>(order);
        this->fftSize = this->fft->getSize();
        this->synthesisSize = this->fftSize / 8;
        this->lookahead = 5 * this->fftSize / 16;
        this->hop = this->synthesisSize / 2;
        this->time.allocate(this->fftSize, false);
        this->analysisWindow.allocate(this->fftSize, false);
        this->synthesisWindow.allocate(this->synthesisSize, false);

        int center = this->fftSize - this->lookahead;
        for (int i = 0; i < this->fftSize; i++) {
            this->analysisWindow[i] = i < center ? hann(i, 2 * center) : hann(this->fftSize - i, 2 * this->lookahead);
        }
        int start = center - this->synthesisSize / 2;
        for (int i = 0; i < this->synthesisSize; i++) {
            this->synthesisWindow[i] = hann(i, this->synthesisSize) / this->analysisWindow[start + i];
        }

        int bins = this->fftSize / 2 + 1;
        for (auto* data : {&this->power, &this->envelope, &this->gain, &this->phase, &this->nextPhase}) {
            data->allocate(bins, false);
        }
        for (auto& channel : this->channels) {
            channel.input.allocate(this->fftSize, false);
            channel.output.allocate(this->fftSize, false);
            channel.spectrum.allocate(this->fftSize, false);
            channel.previousSpectrum.allocate(this->fftSize, false);
            channel.shifted.allocate(this->fftSize, false);
        }
        this->envelopeDecay = static_cast<float>(std::exp(-sampleRate / (this->fftSize * 65.0)));
        this->reset();
    }

    auto reset() -> void {
        this->position = 0;
        if (!this->fft) return;
        this->phase.clear(this->fftSize / 2 + 1);
        for (auto& channel : this->channels) {
            channel.input.clear(this->fftSize);
            channel.output.clear(this->fftSize);
            channel.spectrum.clear(this->fftSize);
        }
    }

    auto latencySamples() const -> int { 
        return this->lookahead + this->synthesisSize / 2; 
    }

    auto process(AudioBuffer<float>& buffer, int start, int count,
        float pitch, float formant, bool preserveFormant) -> void {
        while (count > 0) {
            int length = (std::min)(count, this->hop - this->position % this->hop);
            for (size_t c = 0; c < this->channels.size(); c++) {
                auto& channel = this->channels[c];
                auto* samples = buffer.getWritePointer(static_cast<int>(c), start);
                std::copy_n(samples, length, channel.input + this->position);
                std::copy_n(channel.output + this->position, length, samples);
                std::fill_n(channel.output + this->position, length, 0.0f);
            }
            start += length;
            count -= length;
            this->position = (this->position + length) % this->fftSize;
            if (this->position % this->hop == 0) this->renderFrame(pitch, formant, preserveFormant);
        }
    }


    static auto hann(int index, int length) -> float {
        return 0.5f - 0.5f * std::cos(twoPi * static_cast<float>(index) / static_cast<float>(length));
    }

    auto renderFrame(float pitch, float formant, bool preserveFormant) -> void {
        this->analyse();
        float ratio = std::exp2(pitch / 12.0f);
        float compensation = (preserveFormant ? pitch : 0.0f) - formant;
        if (std::abs(compensation) > 0.0001f) {
            this->shapeEnvelope(std::exp2(compensation / 12.0f));
        } else {
            std::fill_n(this->gain.get(), this->fftSize / 2 + 1, 1.0f);
        }
        if (std::abs(pitch) > 0.0001f) {
            this->shiftPeaks(ratio);
        } else {
            this->phase.clear(this->fftSize / 2 + 1);
            for (auto& channel : this->channels) {
                for (int k = 0; k <= this->fftSize / 2; k++) {
                    channel.shifted[k] = channel.spectrum[k] * this->gain[k];
                }
            }
        }
        this->synthesise();
    }

    auto analyse() -> void {
        this->power.clear(this->fftSize / 2 + 1);
        for (auto& channel : this->channels) {
            channel.previousSpectrum.swapWith(channel.spectrum);
            for (int i = 0; i < this->fftSize; i++) {
                int index = (this->position + i) & (this->fftSize - 1);
                int centerd = (i + this->lookahead) & (this->fftSize - 1);
                this->time[centerd] = channel.input[index] * this->analysisWindow[i];
            }
            this->fft->perform(this->time, channel.spectrum, false);
            for (int k = 0; k <= this->fftSize / 2; k++) this->power[k] += std::norm(channel.spectrum[k]);
        }
    }

    auto shapeEnvelope(float sourceScale) -> void {
        int last = this->fftSize / 2;
        float smooth = std::sqrt(this->power[0]);
        for (int k = 0; k <= last; k++) {
            smooth = std::lerp(std::sqrt(this->power[k]), smooth, this->envelopeDecay);
            this->envelope[k] = smooth;
        }
        for (int k = last; k >= 0; k--) {
            smooth = std::lerp(this->envelope[k], smooth, this->envelopeDecay);
            this->envelope[k] = smooth;
        }

        int left = 0;
        for (int right = 1; right <= last; right++) {
            float height = std::sqrt(this->power[right]);
            if (right < last && (this->power[right] <= this->power[right - 1]
                || this->power[right] < this->power[right + 1]
                || height < 0.5f * this->envelope[right])) continue;
            float slope = (height - this->envelope[left]) / static_cast<float>(right - left);
            for (int k = left + 1; k < right; k++) {
                this->envelope[k] = this->envelope[left] + static_cast<float>(k - left) * slope;
            }
            this->envelope[right] = height;
            left = right;
        }

        float inputPower = 0.0f, shapedPower = 0.0f;
        for (int k = 0; k <= last; k++) {
            float source = (std::min)(static_cast<float>(last), static_cast<float>(k) * sourceScale);
            int bin = static_cast<int>(source);
            float target = std::lerp(this->envelope[bin], this->envelope[(std::min)(bin + 1, last)],
                                     source - static_cast<float>(bin));
            constexpr float floor = 1.0e-8f;
            this->gain[k] = std::clamp((target + floor) / (this->envelope[k] + floor), 1.0f / 16.0f, 16.0f);
            inputPower += this->power[k];
            shapedPower += this->power[k] * this->gain[k] * this->gain[k];
        }

        float level = std::clamp(std::sqrt((inputPower + 1.0e-12f) / (shapedPower + 1.0e-12f)), 0.5f, 2.0f);
        for (int k = 0; k <= last; k++) this->gain[k] *= level;
    }

    auto shiftPeaks(float ratio) -> void {
        int last = this->fftSize / 2;
        for (auto& channel : this->channels) channel.shifted.clear(last + 1);

        int begin = 0;
        int peak = -1;
        for (int k = 0; k <= last; k++) {
            if (k > 0 && this->power[k] <= this->power[k - 1]) continue;
            if (k < last && this->power[k] < this->power[k + 1]) continue;
            if (peak >= 0) {
                auto* valley = std::min_element(this->power + peak + 1, this->power + k);
                int end = static_cast<int>(valley - this->power.get());
                this->shiftRegion(peak, begin, end, ratio);
                begin = end;
            }
            peak = k;
        }
        this->shiftRegion(peak, begin, last + 1, ratio);
        this->phase.swapWith(this->nextPhase);
    }

    auto shiftRegion(int peak, int begin, int end, float ratio) -> void {
        int last = this->fftSize / 2;
        Complex advance = {};
        float previousPower = 0.0f;
        for (auto& channel : this->channels) {
            auto previous = channel.previousSpectrum[peak];
            advance += channel.spectrum[peak] * std::conj(previous);
            previousPower += std::norm(previous);
        }
        float step = twoPi * static_cast<float>(this->hop) / static_cast<float>(this->fftSize);
        float center = static_cast<float>(peak);
        bool attack = this->power[peak] > 8.0f * previousPower;
        if (!attack && peak > 0 && peak < last) {
            float residual = std::remainder(std::arg(advance) - center * step, twoPi);
            center += std::clamp(residual / step, -1.0f, 1.0f);
        }
        float shift = center * (ratio - 1.0f);
        float angle = attack ? 0.0f : std::remainder(this->phase[peak] + shift * step, twoPi);
        auto rotation = std::polar(1.0f, angle);
        std::fill(this->nextPhase + begin, this->nextPhase + end, angle);

        float headroom = static_cast<float>(last) - center * ratio;
        float bandGain = ratio > 1.0f ? std::clamp(headroom / (4.0f * ratio), 0.0f, 1.0f) : 1.0f;
        if (bandGain <= 0.0f) return;

        int offset = static_cast<int>(std::floor(shift));
        float fraction = shift - static_cast<float>(offset);

        auto addBand = [&](int displacement, float weight) {
            if (weight <= 0.0f) return;
            auto scale = rotation * (weight * bandGain);
            int first = (std::max)(begin, -displacement);
            int limit = (std::min)(end, last + 1 - displacement);
            for (auto& channel : this->channels) {
                for (int k = first; k < limit; k++) {
                    channel.shifted[k + displacement] += channel.spectrum[k] * this->gain[k] * scale;
                }
            }
        };
        
        addBand(offset, 1.0f - fraction);
        addBand(offset + 1, fraction);
    }

    auto synthesise() -> void {
        for (auto& channel : this->channels) {
            int last = this->fftSize / 2;
            channel.shifted[0].imag(0.0f);
            channel.shifted[last].imag(0.0f);
            for (int k = 1; k < last; k++) channel.shifted[this->fftSize - k] = std::conj(channel.shifted[k]);
            this->fft->perform(channel.shifted, this->time, true);
            for (int i = 0; i < this->synthesisSize; i++) {
                int index = (this->position + i) & (this->fftSize - 1);
                int centerd = (i - this->synthesisSize / 2) & (this->fftSize - 1);
                channel.output[index] += this->time[centerd].real() * this->synthesisWindow[i];
            }
        }
    }

private:
    static constexpr float twoPi = MathConstants<float>::twoPi;
    std::unique_ptr<dsp::FFT> fft;
    std::vector<Channel> channels;
    HeapBlock<Complex> time;
    HeapBlock<float> analysisWindow;
    HeapBlock<float> synthesisWindow;
    HeapBlock<float> power; 
    HeapBlock<float> envelope;
    HeapBlock<float> gain;
    HeapBlock<float> phase;
    HeapBlock<float> nextPhase;
    int fftSize = 0;
    int synthesisSize = 0;
    int lookahead = 0;
    int hop = 0;
    int position = 0;
    float envelopeDecay = 0.0f;
};