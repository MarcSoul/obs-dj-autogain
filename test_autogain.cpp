#include "autogain.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {

void require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

float render(dj::AutoGain &dsp, uint32_t frames, float amplitude, uint32_t rate)
{
    std::array<float, 513> left{};
    std::array<float, 513> right{};
    float *planes[] = {left.data(), right.data()};
    float final_gain = 0.0f;
    for (uint32_t offset = 0; offset < frames;) {
        const auto count = std::min<uint32_t>(frames - offset, left.size());
        for (uint32_t i = 0; i < count; ++i) {
            left[i] = amplitude * std::cos((offset + i) * (1000.0 * 6.283185307179586 / rate));
            right[i] = left[i] * 0.5f;
        }
        const float last_input = left[count - 1];
        dsp.process(planes, 2, count);
        for (uint32_t i = 0; i < count; ++i)
            require(std::abs(right[i] - left[i] * 0.5f) < 1e-6f, "Stereo balance moved");
        if (std::abs(last_input) > 1e-6f)
            final_gain = left[count - 1] / last_input;
        offset += count;
    }
    return final_gain;
}

void check(uint32_t rate)
{
    dj::AutoGain dsp(rate);
    const float reference = render(dsp, rate * 2, 0.22f, rate);
    require(std::abs(reference - 1.0f) < 1e-5f, "Normal material was boosted");

    // One short, loud passage must not engage the fast path.
    const float short_burst = render(dsp, rate / 10, 0.55f, rate);
    require(short_burst > 0.99f, "Brief burst triggered fast attenuation");
    render(dsp, rate, 0.22f, rate);

    const float overlap = render(dsp, rate * 7 / 10, 0.55f, rate);
    require(overlap > 0.42f && overlap < 0.68f, "Sustained overlap was not corrected quickly");
    render(dsp, rate * 5, 0.55f, rate);
    render(dsp, rate * 3, 0.0f, rate);
    const float after_silence = render(dsp, rate / 10, 0.22f, rate);
    require(after_silence < 0.55f, "Gain rose during silence");
    const float early_recovery = render(dsp, rate * 2, 0.22f, rate);
    const float late_recovery = render(dsp, rate * 36, 0.22f, rate);
    require(early_recovery < 0.72f && late_recovery > 0.9f, "Recovery is outside its expected range");

    dj::AutoGain peak_test(rate);
    float left[] = {0.5f, 1.25f, 0.5f, std::numeric_limits<float>::quiet_NaN()};
    float right[] = {0.25f, 0.625f, 0.25f, 0.0f};
    float *planes[] = {left, right};
    peak_test.process(planes, 2, 4);
    require(left[0] == 0.5f, "Limiter changed a sample before the peak");
    require(std::abs(left[1] - 0.794328f) < 1e-5f, "Limiter missed its ceiling");
    require(std::abs(right[1] - left[1] * 0.5f) < 1e-6f, "Limiter moved stereo balance");
    require(left[2] < 0.5f && left[3] == 0.0f, "Limiter release or sample sanitation failed");

    dj::AutoGain boost_test(rate);
    dj::Settings boost_settings;
    boost_settings.max_boost_db = 3.0f;
    boost_test.configure(boost_settings);
    const float bed = render(boost_test, rate * 30, 0.035f, rate);
    require(std::abs(bed - 1.0f) < 1e-5f, "Deep bed was boosted");

    const float intro = render(boost_test, rate * 8, 0.09f, rate);
    require(std::abs(intro - 1.0f) < 1e-5f, "Short intro was boosted");
    render(boost_test, rate, 0.035f, rate);
    const float next_intro = render(boost_test, rate * 8, 0.09f, rate);
    require(std::abs(next_intro - 1.0f) < 1e-5f, "A bed failed to reset boost qualification");

    const float boost = render(boost_test, rate * 55, 0.09f, rate);
    require(boost > 1.39f && boost < 1.413f, "Optional boost did not respect its cap");

    std::cout << rate << " Hz | overlap gain " << 20 * std::log10(overlap)
              << " dB | recovery after 2 s " << 20 * std::log10(early_recovery)
              << " dB\n";
}

} // namespace

int main()
{
    check(44100);
    check(48000);
}
