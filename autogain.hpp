#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace dj {

struct Settings {
    float target_db = -18.0f;
    float max_boost_db = 3.0f;
    float boost_floor_db = -30.0f;
    float max_cut_db = 18.0f;
    float gate_db = -42.0f;
    float overload_db = 3.0f;
    float recovery_seconds = 12.0f;
    float ceiling_db = -2.0f;
};

class AutoGain {
public:
    explicit AutoGain(uint32_t sample_rate)
        : window_frames(std::max(1u, sample_rate / 10)),
          slow_memory(std::exp(-static_cast<double>(window_frames) / (3.0 * sample_rate))),
          normal_attack(memory(sample_rate, 2.0f)),
          fast_attack(memory(sample_rate, 0.25f)),
          limiter_release(memory(sample_rate, 0.08f)),
          rate(sample_rate)
    {
        configure(Settings{});
    }

    void configure(const Settings &value)
    {
        settings = value;
        gate_power = std::pow(10.0, settings.gate_db / 10.0);
        ceiling = amplitude(settings.ceiling_db);
        recovery = memory(rate, settings.recovery_seconds);
        set_goal(last_goal_db);
    }

    void process(float *const *planes, std::size_t channels, uint32_t frames)
    {
        if (channels == 0)
            return;

        for (uint32_t frame = 0; frame < frames; ++frame) {
            float peak = 0.0f;
            double power = 0.0;
            for (std::size_t ch = 0; ch < channels; ++ch) {
                float &sample = planes[ch][frame];
                if (!std::isfinite(sample))
                    sample = 0.0f;
                peak = std::max(peak, std::abs(sample));
                power += static_cast<double>(sample) * sample;
            }

            window_power += power / channels;
            if (++window_count == window_frames)
                update_detector();

            const bool hold_recovery = below_boost_floor && settings.max_boost_db > 0.0f;
            if (active && (target_gain <= gain || !hold_recovery)) {
                const double smoothing = target_gain < gain
                    ? (overload_windows >= overload_hold ? fast_attack : normal_attack)
                    : recovery;
                gain += (target_gain - gain) * smoothing;
            }

            const double requested_peak = peak * gain;
            const double safe_gain = requested_peak > ceiling ? ceiling / requested_peak : 1.0;
            limiter_gain += (1.0 - limiter_gain) * limiter_release;
            limiter_gain = std::min(safe_gain, limiter_gain);

            const double output_gain = gain * limiter_gain;
            for (std::size_t ch = 0; ch < channels; ++ch)
                planes[ch][frame] = static_cast<float>(planes[ch][frame] * output_gain);
        }
    }

private:
    static double amplitude(float db) { return std::pow(10.0, db / 20.0); }

    static double memory(uint32_t sample_rate, float seconds)
    {
        return -std::expm1(-1.0 / (sample_rate * seconds));
    }

    void set_goal(float db)
    {
        last_goal_db = db;
        target_gain = amplitude(std::clamp(db, -settings.max_cut_db, settings.max_boost_db));
    }

    void update_detector()
    {
        const double block_power = window_power / window_frames;
        window_power = 0.0;
        window_count = 0;

        active = block_power >= gate_power;
        if (!active) {
            overload_windows = 0;
            quiet_windows = 0;
            return;
        }

        if (slow_power == 0.0)
            slow_power = block_power;
        else
            slow_power = slow_power * slow_memory + block_power * (1.0 - slow_memory);

        const auto block_db = static_cast<float>(10.0 * std::log10(block_power));
        const auto slow_db = static_cast<float>(10.0 * std::log10(slow_power));
        const auto gain_db = static_cast<float>(20.0 * std::log10(gain));
        if (block_db + gain_db >= settings.target_db + settings.overload_db)
            overload_windows = std::min(overload_windows + 1, overload_hold);
        else
            overload_windows = 0;

        float goal_db = settings.target_db - slow_db;
        if (overload_windows >= overload_hold)
            goal_db = std::min(goal_db, settings.target_db - block_db);
        below_boost_floor = block_db < settings.boost_floor_db;
        if (!below_boost_floor && goal_db > 0.0f)
            quiet_windows = std::min(quiet_windows + 1, boost_hold);
        else
            quiet_windows = 0;

        // Let attenuation return to unity above the floor, but require
        // sustained low programme before adding any new positive gain.
        if (quiet_windows < boost_hold)
            goal_db = std::min(goal_db, std::max(0.0f, gain_db));
        set_goal(goal_db);
    }

    static constexpr unsigned overload_hold = 3;
    static constexpr unsigned boost_hold = 100;
    const uint32_t window_frames;
    const double slow_memory;
    const double normal_attack;
    const double fast_attack;
    const double limiter_release;
    const uint32_t rate;

    Settings settings;
    double gate_power = 0.0;
    double ceiling = 1.0;
    double recovery = 0.0;
    double slow_power = 0.0;
    double window_power = 0.0;
    uint32_t window_count = 0;
    unsigned overload_windows = 0;
    unsigned quiet_windows = 0;
    bool active = false;
    bool below_boost_floor = false;
    float last_goal_db = 0.0f;
    double target_gain = 1.0;
    double gain = 1.0;
    double limiter_gain = 1.0;
};

} // namespace dj
