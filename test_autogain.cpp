#include "autogain.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

constexpr double pi = 3.14159265358979323846;

dj::Reading render(dj::AutoGain &dsp, uint32_t rate, double seconds, float amplitude,
                   bool freeze = false, double frequency = 997, double phase = 0)
{
    std::array<float, 503> left{}, right{};
    float *planes[] = {left.data(), right.data()};
    const auto length = static_cast<uint32_t>(rate * seconds);
    for (uint32_t offset = 0; offset < length;) {
        const auto count = std::min<uint32_t>(length - offset, left.size());
        for (uint32_t i = 0; i < count; ++i) {
            left[i] = amplitude * std::cos((offset + i) * (frequency * 2 * pi / rate) + phase);
            right[i] = left[i] * 0.5f;
        }
        dsp.process(planes, 2, count, freeze);
        for (uint32_t i = 0; i < count; ++i)
            require(std::abs(right[i] - left[i] * 0.5f) < 1e-6f, "Stereo balance moved");
        offset += count;
    }
    return dsp.reading();
}

void check(uint32_t rate)
{
    {
        dj::AutoGain dsp(rate);
        dj::Settings settings;
        settings.meter_only = true;
        dsp.configure(settings);
        require(!render(dsp, rate, 2, 0.2f).ready, "Incomplete Short-Term window");
        auto r = render(dsp, rate, 3, 0.2f);
        // BS.1770 Annex 1: a full-scale 997 Hz sine in one channel is -3.01 LKFS.
        const float expected = -3.01f + 20 * std::log10(0.2f) + 10 * std::log10(1.25f);
        require(std::abs(r.in_s - expected) < 0.12f && std::abs(r.out_i - expected) < 0.12f,
                "BS.1770 997 Hz reference mismatch");
        require(std::abs(r.in_m - r.out_s) < 0.05f, "Measure-only altered steady-state audio");
        const float integrated = r.out_i;
        r = render(dsp, rate, 12, 0.0005f);
        require(std::abs(r.out_i - integrated) < 0.3f, "Integrated gate included very quiet material");
        dsp.reset();
        require(dsp.reading().out_i == dj::no_signal, "Reset did not clear integrated loudness");
    }
    {
        dj::AutoGain dsp(rate);
        auto r = render(dsp, rate, 5, 0.20f);
        require(std::abs(r.gain) < 0.05f, "Gain moved in tolerance band");
        r = render(dsp, rate, 0.8, 0.58f);
        require(r.gain < -3, "Overlap did not trigger fast cut");
        const float held = r.gain;
        r = render(dsp, rate, 3, 0.2f, true);
        require(std::abs(r.gain - held) < 0.05f, "Freeze moved the gain");
    }
    {
        dj::AutoGain dsp(rate);
        auto r = render(dsp, rate, 15, 0.02f);
        require(std::abs(r.gain) < 0.05f, "Bed was boosted");
        r = render(dsp, rate, 8, 0.08f);
        require(std::abs(r.gain) < 0.05f, "Short intro was boosted");
        r = render(dsp, rate, 38, 0.08f);
        require(r.gain > 2.5f && r.gain <= 3.001f, "Sustained quiet music did not respect boost cap");
    }
    {
        dj::AutoGain dsp(rate);
        dj::Settings settings;
        settings.meter_only = true;
        dsp.configure(settings);
        auto r = render(dsp, rate, 1, 0.96f, false, rate / 4.0, pi / 4);
        require(r.peak > -0.7f, "Intersample peak was missed");
        dsp.configure(dj::Settings{});
        dsp.reset();
        render(dsp, rate, 0.2, 0);
        dsp.reset();
        r = render(dsp, rate, 1, 0.96f, true, rate / 4.0, pi / 4);
        require(r.max_peak <= -1.95f && r.limiting > 1, "True Peak ceiling failed");
    }
    {
        dj::AutoGain dsp(rate);
        dj::Settings settings;
        settings.meter_only = true;
        dsp.configure(settings);
        std::vector<float> impulse(dsp.latency_frames() + 5);
        impulse[0] = 0.5;
        float *planes[] = {impulse.data()};
        dsp.process(planes, 1, static_cast<uint32_t>(impulse.size()));
        require(impulse[dsp.latency_frames()] == 0.5f && impulse[0] == 0, "Latency mismatch");
    }
    std::cout << rate << " Hz passed\n";
}

} // namespace

int main()
{
    check(44100);
    check(48000);
}
