#pragma once

#include "loudness.hpp"

#include <vector>

namespace dj {

struct Settings {
    float target = -16;
    float band = 2;
    float max_boost = 3;
    float max_cut = 18;
    float floor = -30;
    float gate = -50;
    float boost_wait = 10;
    float overload = 4;
    float overlap_hold = 0.3f;
    float attack = 2;
    float fast_attack = 0.25f;
    float recovery = 5;
    float boost_time = 12;
    float ceiling = -2;
    float release = 0.1f;
    bool meter_only = false;
};

enum class State { Warming, InRange, Holding, Waiting, Boost, Cut, Overlap, Frozen, NoHeadroom, MeterOnly };

struct Reading {
    float in_m = no_signal, in_s = no_signal, in_i = no_signal;
    float out_m = no_signal, out_s = no_signal, out_i = no_signal;
    float peak = no_signal, max_peak = no_signal, gain = 0, limiting = 0, wait = 0, target = -16, band = 2;
    float limited_percent = 0;
    State state = State::Warming;
    bool ready = false;
};

class AutoGain {
public:
    AutoGain(uint32_t rate, unsigned lookahead_ms = 5)
        : rate(rate), delay_frames(rate * lookahead_ms / 1000 + TruePeak::taps),
          input(rate), output(rate), delay(delay_frames + 1), peaks(delay_frames + 2)
    {
        configure(Settings{});
    }

    void configure(const Settings &value)
    {
        settings = value;
        ceiling = amplitude(settings.ceiling - 0.2f);
        release = smoothing(settings.release);
        smooth_cut = smoothing(settings.attack);
        smooth_fast = smoothing(settings.fast_attack);
        smooth_up = smoothing(settings.recovery);
        smooth_boost = smoothing(settings.boost_time);
    }

    uint32_t latency_frames() const { return delay_frames; }
    Reading reading() const { return meters; }

    void reset()
    {
        input.reset_integrated();
        output.reset_integrated();
        meters.in_i = meters.out_i = meters.max_peak = no_signal;
        limited_frames = emitted_frames = 0;
    }

    void process(float *const *planes, size_t channels, uint32_t frames, bool freeze = false)
    {
        if (channels == 0 || channels > 2)
            return;

        for (uint32_t i = 0; i < frames; ++i, ++frame) {
            std::array<float, 2> sample{};
            for (size_t ch = 0; ch < channels; ++ch)
                sample[ch] = std::isfinite(planes[ch][i]) ? planes[ch][i] : 0;
            const bool tick = input.process(sample, channels);
            if (tick)
                control(freeze);
            if (!freeze || settings.meter_only) {
                const float coefficient = goal < gain ? (fast ? smooth_fast : smooth_cut)
                    : (gain < 1 ? smooth_up : smooth_boost);
                gain += (goal - gain) * coefficient;
            }

            const float detect_gain = settings.meter_only ? 1 : gain;
            for (auto &value : sample)
                value *= detect_gain;
            delay[frame % delay.size()] = sample;
            const float peak = peak_in.process(sample, channels);
            in_peak = std::max(in_peak, peak);

            // Monotonic queue keeps the largest oversampled peak across the
            // lookahead window plus FIR support. No allocation on audio thread.
            const uint64_t play = frame >= delay_frames ? frame - delay_frames : 0;
            while (front != back && peaks[front % peaks.size()].index < play)
                ++front;
            while (front != back && peaks[(back - 1) % peaks.size()].peak <= peak)
                --back;
            peaks[back++ % peaks.size()] = {frame, peak};

            sample = {};
            if (frame >= delay_frames) {
                const float maximum = peaks[front % peaks.size()].peak;
                limiter += (1 - limiter) * release;
                if (maximum > ceiling)
                    limiter = std::min(limiter, ceiling / maximum);
                sample = delay[play % delay.size()];
                if (!settings.meter_only) {
                    for (auto &value : sample)
                        value *= limiter;
                    limited_frames += limiter < 0.98855f;
                }
                ++emitted_frames;
            }
            for (size_t ch = 0; ch < channels; ++ch)
                planes[ch][i] = sample[ch];
            out_peak = std::max(out_peak, peak_out.process(sample, channels));
            if (frame >= delay_frames)
                output.process(sample, channels);
            if (tick)
                publish();
        }
    }

private:
    static float amplitude(float db) { return std::pow(10.0f, db / 20); }
    static float db(float value) { return value > 1e-6f ? 20 * std::log10(value) : no_signal; }

    float smoothing(float seconds) const
    {
        return -std::expm1(-1.0f / (rate * seconds));
    }

    void control(bool freeze)
    {
        meters.wait = 0;
        if (settings.meter_only) {
            goal = 1;
            meters.state = State::MeterOnly;
            return;
        }
        if (freeze || !input.ready() || input.momentary < settings.gate) {
            goal = gain;
            quiet = over = 0;
            fast = false;
            meters.state = freeze ? State::Frozen : (!input.ready() ? State::Warming : State::Holding);
            return;
        }

        const float gain_db = db(gain);
        const float momentary = input.momentary;
        const float short_term = input.short_term;
        const float excess = momentary + gain_db - settings.target;
        over = excess >= settings.overload ? std::min(over + 0.1f, 60.0f) : 0;
        fast = over + 0.001f >= settings.overlap_hold
            || (fast && excess > settings.overload - 1);

        const float correction = settings.target - short_term;
        float desired = std::clamp(gain_db, correction - settings.band, correction + settings.band);
        if (desired > gain_db && correction >= 0)
            desired = correction;
        if (fast)
            desired = std::min(desired, settings.target + settings.band - momentary);

        const bool eligible = momentary >= settings.floor && correction > settings.band
                              && momentary < settings.target;
        quiet = eligible ? std::min(quiet + 0.1f, 120.0f) : 0;
        meters.state = State::InRange;
        if (desired > gain_db && momentary < settings.floor) {
            desired = gain_db;
            meters.state = State::Holding;
        } else if (desired > std::max(0.0f, gain_db) && quiet + 0.001f < settings.boost_wait) {
            desired = std::max(0.0f, gain_db);
            meters.wait = settings.boost_wait - quiet;
            meters.state = State::Waiting;
        }

        if (desired > gain_db && in_peak > 0) {
            const float allowed = gain_db + db(ceiling / in_peak);
            if (allowed < desired) {
                desired = std::max(gain_db, allowed);
                meters.state = State::NoHeadroom;
            }
        }
        desired = std::clamp(desired, -settings.max_cut, settings.max_boost);
        goal = amplitude(desired);
        if (fast)
            meters.state = State::Overlap;
        else if (desired < gain_db - 0.05f)
            meters.state = State::Cut;
        else if (desired > gain_db + 0.05f && meters.state == State::InRange)
            meters.state = State::Boost;
    }

    void publish()
    {
        meters.ready = output.ready();
        meters.in_m = input.momentary;
        meters.in_s = input.short_term;
        meters.out_m = output.momentary;
        meters.out_s = output.short_term;
        meters.peak = db(out_peak);
        meters.max_peak = std::max(meters.peak, meters.max_peak);
        meters.gain = settings.meter_only ? 0 : db(gain);
        meters.limiting = settings.meter_only ? 0 : -db(limiter);
        meters.target = settings.target;
        meters.band = settings.band;
        meters.limited_percent = emitted_frames ? 100.0f * limited_frames / emitted_frames : 0;
        if (++ticks == 10) {
            ticks = 0;
            meters.in_i = input.integrated();
            meters.out_i = output.integrated();
        }
        in_peak = out_peak = 0;
    }

    struct Peak { uint64_t index; float peak; };
    const uint32_t rate, delay_frames;
    Loudness input, output;
    TruePeak peak_in, peak_out;
    std::vector<std::array<float, 2>> delay;
    std::vector<Peak> peaks;
    uint64_t front = 0, back = 0, frame = 0, limited_frames = 0, emitted_frames = 0;
    unsigned ticks = 0;
    Settings settings;
    Reading meters;
    float ceiling = 1, release = 0, smooth_cut = 0, smooth_fast = 0, smooth_up = 0, smooth_boost = 0;
    float gain = 1, goal = 1, limiter = 1, quiet = 0, over = 0, in_peak = 0, out_peak = 0;
    bool fast = false;
};

} // namespace dj
