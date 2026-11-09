#include <obs-module.h>

#include "autogain.hpp"

#include <mutex>
#include <new>

OBS_DECLARE_MODULE()

namespace {

struct Filter {
    explicit Filter(uint32_t sample_rate) : dsp(sample_rate) {}

    dj::AutoGain dsp;
    std::mutex settings_mutex;
    dj::Settings pending;
    bool changed = false;
};

struct Control {
    const char *key;
    const char *label;
    float dj::Settings::*member;
    float min;
    float max;
    float step;
};

constexpr Control controls[] = {
    {"target", "Objetivo RMS (dBFS)", &dj::Settings::target_db, -30, -8, 0.5f},
    {"boost", "Subida máxima (dB)", &dj::Settings::max_boost_db, 0, 6, 0.5f},
    {"boost_floor", "Nivel mínimo para recuperar o subir ganancia (dBFS RMS)",
     &dj::Settings::boost_floor_db, -40, -20, 0.5f},
    {"cut", "Atenuación máxima (dB)", &dj::Settings::max_cut_db, 0, 24, 0.5f},
    {"gate", "Pausa de ajuste bajo (dBFS RMS)", &dj::Settings::gate_db, -65, -32, 1},
    {"overload", "Margen para reacción rápida (dB)", &dj::Settings::overload_db, 2, 10, 0.5f},
    {"recovery", "Tiempo de recuperación (s)", &dj::Settings::recovery_seconds, 3, 30, 0.5f},
    {"ceiling", "Techo del limitador (dBFS de pico)", &dj::Settings::ceiling_db, -8, -0.5f, 0.5f},
};

const char *name(void *)
{
    return "DJ AutoGain";
}

void defaults(obs_data_t *data)
{
    const dj::Settings initial;
    for (const auto &control : controls)
        obs_data_set_default_double(data, control.key, initial.*(control.member));
}

obs_properties_t *properties(void *)
{
    auto *props = obs_properties_create();
    obs_properties_add_text(props, "guide",
        "Sube temas bajos tras 10 s de señal elegible. Con subida máxima a 0 dB solo atenúa. "
        "El limitador protege frente a picos de muestra.", OBS_TEXT_INFO);
    for (const auto &control : controls)
        obs_properties_add_float_slider(props, control.key, control.label,
                                       control.min, control.max, control.step);
    return props;
}

void update(void *context, obs_data_t *data)
{
    auto &filter = *static_cast<Filter *>(context);
    dj::Settings settings;
    for (const auto &control : controls) {
        const auto value = static_cast<float>(obs_data_get_double(data, control.key));
        if (std::isfinite(value))
            settings.*(control.member) = std::clamp(value, control.min, control.max);
    }

    const std::lock_guard<std::mutex> lock(filter.settings_mutex);
    filter.pending = settings;
    filter.changed = true;
}

void *create(obs_data_t *settings, obs_source_t *)
{
    obs_audio_info info{};
    if (!obs_get_audio_info(&info) || info.samples_per_sec < 10)
        return nullptr;

    auto *filter = new (std::nothrow) Filter(info.samples_per_sec);
    if (filter)
        update(filter, settings);
    return filter;
}

void destroy(void *context)
{
    delete static_cast<Filter *>(context);
}

obs_audio_data *process(void *context, obs_audio_data *audio)
{
    auto &filter = *static_cast<Filter *>(context);
    {
        const std::unique_lock<std::mutex> lock(filter.settings_mutex, std::try_to_lock);
        if (lock && filter.changed) {
            filter.dsp.configure(filter.pending);
            filter.changed = false;
        }
    }

    float *planes[MAX_AV_PLANES];
    std::size_t channels = 0;
    for (auto *plane : audio->data) {
        if (plane)
            planes[channels++] = reinterpret_cast<float *>(plane);
    }
    filter.dsp.process(planes, channels, audio->frames);
    return audio;
}

} // namespace

bool obs_module_load(void)
{
    obs_source_info info{};
    info.id = "marcsoul_dj_autogain";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = name;
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.create = create;
    info.destroy = destroy;
    info.update = update;
    info.filter_audio = process;
    obs_register_source(&info);
    return true;
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Slow gain riding and peak protection for DJ sets";
}
