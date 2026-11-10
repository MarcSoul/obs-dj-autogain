#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace dj {

inline constexpr float no_signal = -120.0f;

struct Biquad {
    double b0{}, b1{}, b2{}, a1{}, a2{}, z1{}, z2{};

    double process(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

class Loudness {
public:
    explicit Loudness(uint32_t rate) : block_frames(rate / 10)
    {
        constexpr double pi = 3.14159265358979323846;
        const double k = std::tan(pi * 1681.974450955533 / rate);
        constexpr double q = 0.7071752369554196;
        const double vh = std::pow(10.0, 3.999843853973347 / 20);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a = 1 + k / q + k * k;
        shelf.fill({(vh + vb * k / q + k * k) / a, 2 * (k * k - vh) / a,
                    (vh - vb * k / q + k * k) / a, 2 * (k * k - 1) / a,
                    (1 - k / q + k * k) / a});
        const double h = std::tan(pi * 38.13547087602444 / rate);
        const double r = h / 0.5003270373238773;
        const double d = 1 + r + h * h;
        highpass.fill({1, -2, 1, 2 * (h * h - 1) / d, (1 - r + h * h) / d});
    }

    bool process(const std::array<float, 2> &samples, size_t channels)
    {
        for (size_t ch = 0; ch < channels; ++ch) {
            const auto y = highpass[ch].process(shelf[ch].process(samples[ch]));
            power += y * y;
        }
        if (++frames < block_frames)
            return false;

        history[position] = power / block_frames;
        position = (position + 1) % history.size();
        count = std::min(count + 1, history.size());
        power = 0;
        frames = 0;
        if (integrated_blocks < 4)
            ++integrated_blocks;
        if (count >= 4) {
            const double energy = mean(4);
            momentary = level(energy);
            if (integrated_blocks == 4 && momentary >= -70) {
                const auto index = std::clamp(static_cast<int>((momentary + 70) * 100), 0, 10999);
                bins[index].energy += energy;
                ++bins[index].count;
                total_energy += energy;
                ++total_count;
            }
        }
        if (ready())
            short_term = level(mean(30));
        return true;
    }

    bool ready() const { return count == history.size(); }

    float integrated() const
    {
        if (!total_count)
            return no_signal;
        const float threshold = std::max(-70.0f, level(total_energy / total_count) - 10.0f);
        const auto first = std::clamp(static_cast<int>((threshold + 70) * 100 + 0.5f), 0, 10999);
        double energy = 0;
        uint64_t accepted = 0;
        for (size_t i = first; i < bins.size(); ++i) {
            energy += bins[i].energy;
            accepted += bins[i].count;
        }
        return accepted ? level(energy / accepted) : no_signal;
    }

    void reset_integrated()
    {
        bins.fill({});
        integrated_blocks = 0;
        total_energy = 0;
        total_count = 0;
    }

    float momentary = no_signal;
    float short_term = no_signal;

private:
    static float level(double energy)
    {
        return energy > 1e-12 ? static_cast<float>(-0.691 + 10 * std::log10(energy)) : no_signal;
    }

    double mean(size_t blocks) const
    {
        double sum = 0;
        for (size_t i = 1; i <= blocks; ++i)
            sum += history[(position + history.size() - i) % history.size()];
        return sum / blocks;
    }

    struct Bin { double energy{}; uint64_t count{}; };
    std::array<Bin, 11000> bins{};
    double total_energy = 0;
    uint64_t total_count = 0;
    const uint32_t block_frames;
    uint32_t frames = 0;
    unsigned integrated_blocks = 0;
    double power = 0;
    size_t count = 0, position = 0;
    std::array<double, 30> history{};
    std::array<Biquad, 2> shelf{}, highpass{};
};

class TruePeak {
public:
    static constexpr size_t taps = 24;
    static constexpr size_t delay = taps / 2 - 1;

    TruePeak()
    {
        constexpr double pi = 3.14159265358979323846;
        for (size_t phase = 0; phase < coefficients.size(); ++phase) {
            double sum = 0;
            for (size_t tap = 0; tap < taps; ++tap) {
                const double x = static_cast<double>(tap) - delay - (phase + 1) / 4.0;
                const double window = 0.42 + 0.5 * std::cos(pi * x / 12)
                                           + 0.08 * std::cos(2 * pi * x / 12);
                sum += coefficients[phase][tap] = std::sin(pi * x) / (pi * x) * window;
            }
            for (auto &coefficient : coefficients[phase])
                coefficient /= sum;
        }
    }

    float process(const std::array<float, 2> &samples, size_t channels)
    {
        const auto oldest = (position + 1) % taps;
        double peak = 0;
        for (size_t ch = 0; ch < channels; ++ch) {
            auto &channel = history[ch];
            channel[position] = channel[position + taps] = samples[ch];
            const float *window = channel.data() + oldest;
            peak = std::max(peak, static_cast<double>(std::abs(window[delay])));
            for (const auto &phase : coefficients) {
                double value = 0;
                for (size_t tap = 0; tap < taps; ++tap)
                    value += window[tap] * phase[tap];
                peak = std::max(peak, std::abs(value));
            }
        }
        position = oldest;
        return static_cast<float>(peak);
    }

private:
    std::array<std::array<double, taps>, 3> coefficients{};
    std::array<std::array<float, 2 * taps>, 2> history{};
    size_t position = 0;
};

} // namespace dj
