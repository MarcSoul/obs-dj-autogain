#pragma once

#include "autogain.hpp"

namespace dj {

struct Preset {
    int id;
    const char *title;
    const char *detail;
    float integrated_reference;
    Settings settings;
};

inline Settings preset_settings(float target, float ceiling, bool maximum = false)
{
    Settings settings;
    settings.target = target;
    settings.ceiling = ceiling;
    if (maximum) {
        settings.band = 1;
        settings.max_boost = 12;
        settings.peak_drive = 6;
    }
    return settings;
}

inline const Preset presets[] = {
    {4, "YouTube / Facebook · Directo orientativo",
     "Perfil del plugin: −16 LUFS-S y −2 dBTP. Orientativo para directo, sin requisito oficial de LUFS verificado.", no_signal, preset_settings(-16, -2)},
    {5, "Spotify · Referencia de reproducción",
     "Referencia: −14 LUFS integrados y pico inferior a −1 dBTP. El filtro usa −14 LUFS-S como adaptación para directo; comprueba el integrado del programa.", -14, preset_settings(-14, -1)},
    {6, "Apple Music · Referencia orientativa",
     "Referencia aproximada: −16 LUFS integrados. Apple no publica ese valor como requisito estéreo. Adaptación del filtro: −16 LUFS-S y −1 dBTP.", -16, preset_settings(-16, -1)},
    {7, "Radio online · Música (AES)",
     "Referencia AES para música normalizada por pista: −16 LUFS integrados y máximo −1 dBTP antes del códec. Adaptación del filtro: −16 LUFS-S.", -16, preset_settings(-16, -1)},
    {8, "Radio · Producción EBU R128",
     "Referencia de producción e intercambio: −23 LUFS integrados y máximo −1 dBTP. Control del filtro: −23 LUFS-S. Comprueba el integrado; este perfil prepara la señal para la cadena de emisión.", -23, preset_settings(-23, -1)},
    {9, "Radio online · Contenido variado (AES)",
     "Referencia AES para voz, música y contenido variado: −18 LUFS integrados y máximo −1 dBTP. Adaptación del filtro: −18 LUFS-S.", -18, preset_settings(-18, -1)},
    {10, "Grabación · Volumen máximo",
     "Perfil intenso para escucha local: objetivo −9 LUFS-S, tolerancia ±1 LU y techo −1 dBTP. Permite hasta 12 dB de subida y 6 dB de entrada adicional al limitador. Reduce la dinámica y puede alterar la pegada. Ajuste creativo, sin valor integrado garantizado. Para CD, prepara el archivo final en PCM estéreo 44,1 kHz/16 bits, con dithering al reducir la profundidad.", no_signal, preset_settings(-9, -1, true)},
};

inline const Preset *find_preset(int id)
{
    for (const auto &preset : presets)
        if (preset.id == id)
            return &preset;
    return nullptr;
}

} // namespace dj
