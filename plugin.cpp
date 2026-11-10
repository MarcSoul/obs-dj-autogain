#include <obs-module.h>
#include <obs-frontend-api.h>

#include "autogain.hpp"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <atomic>
#include <deque>
#include <mutex>
#include <chrono>

OBS_DECLARE_MODULE()

namespace {

struct Filter {
    Filter(uint32_t rate, obs_source_t *source)
        : dsp(rate), source(source), latency(1000.0 * dsp.latency_frames() / rate) {}

    dj::AutoGain dsp;
    obs_source_t *source;
    const double latency;
    int id = 0;
    QString label;
    obs_hotkey_id hotkey = OBS_INVALID_HOTKEY_ID;
    std::mutex settings_mutex, reading_mutex;
    dj::Settings pending;
    dj::Reading reading;
    bool changed = false;
    std::atomic<bool> freeze{false}, reset{false};
    std::atomic<bool> unsupported{false};
    std::chrono::steady_clock::time_point last_audio{};
};

std::mutex registry_mutex;
std::vector<Filter *> registry;
int next_id = 1;
constexpr auto dock_id = "marcsoul-dj-autogain";
constexpr auto meter_dock_id = "marcsoul-dj-autogain-meter";

struct Control {
    const char *key;
    const char *label;
    float dj::Settings::*member;
    float min, max, step;
    const char *help;
};

constexpr Control basic[] = {
    {"target_lufs", "Objetivo Short-Term (LUFS-S)", &dj::Settings::target, -30, -8, 0.5f, "Centro de la zona de trabajo durante la música activa."},
    {"band", "Tolerancia a cada lado (LU)", &dj::Settings::band, 0, 6, 0.5f, "Dentro de esta zona, el nivelador procura mantener la ganancia."},
    {"boost", "Subida máxima (dB)", &dj::Settings::max_boost, 0, 6, 0.5f, "Con 0 dB se recupera la atenuación anterior, sin amplificar la señal."},
    {"cut", "Atenuación máxima (dB)", &dj::Settings::max_cut, 0, 24, 0.5f, "Límite del nivelador. El limitador de picos tiene prioridad."},
    {"true_peak", "Techo True Peak (dBTP)", &dj::Settings::ceiling, -8, -0.5f, 0.5f, "Límite estimado entre muestras. Se deja un margen interno de 0,2 dB."},
};

constexpr Control advanced[] = {
    {"floor_lufs", "Nivel mínimo de recuperación (LUFS-M)", &dj::Settings::floor, -45, -20, 0.5f, "Por debajo de este nivel no se recupera ni se añade ganancia."},
    {"gate_lufs", "Umbral de pausa por silencio (LUFS-M)", &dj::Settings::gate, -70, -35, 1, "Por debajo de este nivel se pausa el ajuste automático."},
    {"boost_wait", "Espera antes de amplificar (s)", &dj::Settings::boost_wait, 1, 30, 0.5f, "Tiempo de música baja sostenida antes de superar 0 dB de ganancia."},
    {"attack", "Atenuación normal (s)", &dj::Settings::attack, 0.5f, 10, 0.1f, "Constante de tiempo para corregir subidas de nivel medio."},
    {"overlap_lu", "Exceso para detectar un solape (LU)", &dj::Settings::overload, 2, 12, 0.5f, "Exceso del nivel momentáneo sobre el objetivo para activar la vía rápida."},
    {"overlap_hold", "Persistencia del exceso (s)", &dj::Settings::overlap_hold, 0.2f, 2, 0.1f, "Tiempo por encima del umbral antes de intervenir con rapidez."},
    {"fast_attack", "Atenuación rápida (s)", &dj::Settings::fast_attack, 0.1f, 2, 0.05f, "Constante de tiempo al corregir un solape."},
    {"recover_unity", "Recuperación hacia 0 dB (s)", &dj::Settings::recovery, 1, 30, 0.5f, "Constante para devolver la ganancia que se había quitado."},
    {"recovery", "Subida por encima de 0 dB (s)", &dj::Settings::boost_time, 3, 30, 0.5f, "Constante para amplificar un tema bajo después de la espera."},
    {"limiter_release", "Liberación del limitador (s)", &dj::Settings::release, 0.03f, 1, 0.01f, "Tiempo de recuperación tras un pico. La anticipación es fija."},
};

template<class Callback> void each_control(Callback callback)
{
    for (const auto &control : basic)
        callback(control);
    for (const auto &control : advanced)
        callback(control);
}

void defaults(obs_data_t *data)
{
    const dj::Settings initial;
    each_control([&](const Control &c) { obs_data_set_default_double(data, c.key, initial.*(c.member)); });
}

void add_control(obs_properties_t *props, const Control &c)
{
    auto *p = obs_properties_add_float_slider(props, c.key, c.label, c.min, c.max, c.step);
    obs_property_set_long_description(p, c.help);
}

obs_properties_t *properties(void *)
{
    auto *props = obs_properties_create();
    obs_properties_add_text(props, "guide",
        "Aplica este filtro al audio de tu mesa. Abre Paneles → DJ AutoGain para ver la sonoridad, la ganancia y las guías. "
        "Modo Solo medir conserva la latencia y permite comparar el procesamiento.", OBS_TEXT_INFO);
    obs_properties_add_bool(props, "meter_only", "Solo medir");
    for (const auto &c : basic)
        add_control(props, c);
    auto *extra = obs_properties_create();
    for (const auto &c : advanced)
        add_control(extra, c);
    obs_properties_add_group(props, "advanced", "Ajustes avanzados", OBS_GROUP_NORMAL, extra);
    return props;
}

void update(void *context, obs_data_t *data)
{
    auto &filter = *static_cast<Filter *>(context);
    dj::Settings settings;
    each_control([&](const Control &c) {
        const auto value = static_cast<float>(obs_data_get_double(data, c.key));
        if (std::isfinite(value))
            settings.*(c.member) = std::clamp(value, c.min, c.max);
    });
    settings.meter_only = obs_data_get_bool(data, "meter_only");
    const std::lock_guard<std::mutex> lock(filter.settings_mutex);
    filter.pending = settings;
    filter.changed = true;
}

void freeze_hotkey(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
    auto &value = static_cast<Filter *>(data)->freeze;
    if (pressed) {
        bool old = value.load();
        while (!value.compare_exchange_weak(old, !old)) {}
    }
}

void *create(obs_data_t *settings, obs_source_t *source)
{
    obs_audio_info info{};
    if (!obs_get_audio_info(&info) || info.samples_per_sec < 8000)
        return nullptr;

    auto *filter = new Filter(info.samples_per_sec, source);
    update(filter, settings);
    filter->hotkey = obs_hotkey_register_source(source, "dj_autogain_freeze", "DJ AutoGain · Congelar ganancia", freeze_hotkey, filter);
    const std::lock_guard<std::mutex> lock(registry_mutex);
    filter->id = next_id++;
    filter->label = QString::fromUtf8(obs_source_get_name(source));
    registry.push_back(filter);
    return filter;
}

void attach(void *context, obs_source_t *source)
{
    const std::lock_guard<std::mutex> lock(registry_mutex);
    auto &filter = *static_cast<Filter *>(context);
    filter.label = QStringLiteral("%1 · %2").arg(QString::fromUtf8(obs_source_get_name(source)),
        QString::fromUtf8(obs_source_get_name(filter.source)));
}

void destroy(void *context)
{
    auto *filter = static_cast<Filter *>(context);
    obs_hotkey_unregister(filter->hotkey);
    {
        const std::lock_guard<std::mutex> lock(registry_mutex);
        registry.erase(std::remove(registry.begin(), registry.end(), filter), registry.end());
    }
    delete filter;
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
    if (filter.reset.exchange(false))
        filter.dsp.reset();

    float *planes[MAX_AV_PLANES];
    size_t channels = 0;
    for (auto *plane : audio->data)
        if (plane)
            planes[channels++] = reinterpret_cast<float *>(plane);
    filter.unsupported.store(channels > 2);
    filter.dsp.process(planes, channels, audio->frames, filter.freeze.load());
    const std::unique_lock<std::mutex> lock(filter.reading_mutex, std::try_to_lock);
    if (lock) {
        filter.reading = filter.dsp.reading();
        filter.last_audio = std::chrono::steady_clock::now();
    }
    return audio;
}

struct Guide { const char *title; const char *detail; float integrated_reference = dj::no_signal; };

constexpr Guide guides[] = {
    {"DJ Live · YouTube / Facebook", "Orientativo · −16 LUFS-S, zona −18 a −14 LUFS-S. Ajuste musical del plugin para directos. Techo recomendado del perfil: −2 dBTP."},
    {"Spotify", "Referencia oficial de reproducción · −14 LUFS integrados por defecto. Seleccionarla no cambia la ganancia.", -14},
    {"Apple Music", "Orientativo · −16 LUFS integrados. Apple documenta Sound Check, sin publicar ese objetivo como requisito.", -16},
    {"AES TD1008 · Radio online", "Recomendación técnica · −16 LUFS-I para música normalizada por pista; −18 LUFS-I para voz o contenido variado. −1 dBTP máximo a la entrada de códecs con pérdida."},
    {"EBU R128 · Radio", "Recomendación técnica · −23 LUFS integrados en producción. La transmisión FM tiene su propia cadena de procesado y medición.", -23},
};

QString level(float value)
{
    return value == dj::no_signal ? QStringLiteral("—") : QString::number(value, 'f', 1);
}

QString status(const dj::Reading &r)
{
    switch (r.state) {
    case dj::State::Warming: return QStringLiteral("Midiendo la ventana inicial de 3 s");
    case dj::State::InRange: return QStringLiteral("Ganancia estable");
    case dj::State::Holding: return QStringLiteral("Señal baja · ajuste en pausa");
    case dj::State::Waiting: return QStringLiteral("Tema bajo · esperando %1 s antes de amplificar").arg(std::ceil(r.wait));
    case dj::State::Boost: return QStringLiteral("Recuperando o subiendo ganancia");
    case dj::State::Cut: return QStringLiteral("Corrigiendo nivel alto");
    case dj::State::Overlap: return QStringLiteral("Exceso sostenido · vía rápida");
    case dj::State::Frozen: return QStringLiteral("Ganancia congelada · limitador activo");
    case dj::State::NoHeadroom: return QStringLiteral("Margen de pico insuficiente");
    case dj::State::MeterOnly: return QStringLiteral("Solo medir · audio sin corrección");
    }
    return {};
}

class History : public QWidget {
public:
    History() { setMinimumHeight(96); setAccessibleName(QStringLiteral("Historial de sonoridad")); }

    void add(const dj::Reading &r)
    {
        readings.push_back(r);
        if (readings.size() > 300)
            readings.pop_front();
        update();
    }

    void clear() { readings.clear(); update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().base());
        if (readings.empty())
            return;

        auto y = [this](float lu) { return height() * (-6 - std::clamp(lu, -40.0f, -6.0f)) / 34; };
        const auto &latest = readings.back();
        const auto top = y(latest.target + latest.band);
        const auto bottom = y(latest.target - latest.band);
        painter.fillRect(QRectF(0, top, width(), bottom - top), QColor(45, 160, 140, 45));

        painter.setRenderHint(QPainter::Antialiasing);
        for (int trace = 0; trace < 3; ++trace) {
            painter.setPen(QPen(trace == 0 ? QColor(135, 145, 160) : trace == 1 ? QColor(50, 170, 145) : QColor(230, 160, 70), 1.5));
            QPointF previous;
            bool valid = false;
            for (size_t i = 0; i < readings.size(); ++i) {
                const auto &r = readings[i];
                const float value = trace == 0 ? r.in_s : trace == 1 ? r.out_s : -20 + r.gain;
                const QPointF point(width() * (300.0 - readings.size() + i) / 299, y(value));
                if (valid && value != dj::no_signal)
                    painter.drawLine(previous, point);
                valid = value != dj::no_signal;
                previous = point;
            }
        }
    }

private:
    std::deque<dj::Reading> readings;
};

// Called with registry_mutex held. The reading mutex protects last_audio.
bool receiving_audio(const Filter &filter)
{
    return std::chrono::steady_clock::now() - filter.last_audio < std::chrono::seconds(1);
}

Filter *selected_filter(const QComboBox &sources)
{
    const int id = sources.currentData().toInt();
    const auto found = std::find_if(registry.begin(), registry.end(),
        [id](const Filter *f) { return f->id == id; });
    return found == registry.end() ? nullptr : *found;
}

void refresh_sources(QComboBox &sources)
{
    bool rebuild = sources.count() != static_cast<int>(registry.size());
    for (int i = 0; !rebuild && i < sources.count(); ++i)
        rebuild = sources.itemData(i).toInt() != registry[i]->id || sources.itemText(i) != registry[i]->label;
    if (!rebuild)
        return;

    const int id = sources.currentData().toInt();
    sources.clear();
    for (auto *f : registry)
        sources.addItem(f->label, f->id);
    const int previous = sources.findData(id);
    if (previous >= 0)
        sources.setCurrentIndex(previous);
}

class CompactMeter : public QWidget {
public:
    CompactMeter()
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        layout->setSpacing(4);
        sources.setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        sources.setMinimumContentsLength(8);
        sources.setAccessibleName(QStringLiteral("Fuente del medidor LUFS"));
        sources.hide();
        layout->addWidget(&sources);

        auto *card = new QFrame(this);
        card->setObjectName(QStringLiteral("djAutoGainMeterCard"));
        card->setStyleSheet(QStringLiteral(
            "QFrame#djAutoGainMeterCard { background: palette(base); "
            "border: 1px solid palette(mid); border-radius: 8px; }"));
        auto *reading_layout = new QVBoxLayout(card);
        reading_layout->setContentsMargins(10, 6, 10, 6);
        reading_layout->setSpacing(0);
        auto *caption = new QLabel(QStringLiteral("SALIDA · LUFS-S"), card);
        caption->setAlignment(Qt::AlignCenter);
        value.setAlignment(Qt::AlignCenter);
        value.setAccessibleName(QStringLiteral("Sonoridad Short-Term de salida"));
        QFont font = value.font();
        font.setPointSize(32);
        font.setBold(true);
        value.setFont(font);
        reading_layout->addWidget(&value);
        reading_layout->addWidget(caption);
        layout->addWidget(card);
        layout->addStretch();

        connect(&timer, &QTimer::timeout, this, [this] { refresh(); });
        timer.start(100);
    }

private:
    void refresh()
    {
        if (!isVisible())
            return;

        float loudness = dj::no_signal;
        QString description = QStringLiteral("Añade el filtro DJ AutoGain a una fuente de audio.");
        {
            const std::lock_guard<std::mutex> lock(registry_mutex);
            refresh_sources(sources);
            if (auto *f = selected_filter(sources)) {
                const std::lock_guard<std::mutex> reading_lock(f->reading_mutex);
                if (f->unsupported.load())
                    description = QStringLiteral("Se requiere una fuente mono o estéreo.");
                else if (!receiving_audio(*f))
                    description = QStringLiteral("Sin audio reciente.");
                else if (!f->reading.ready)
                    description = QStringLiteral("Esperando la primera ventana de 3 s.");
                else if (f->reading.out_s == dj::no_signal)
                    description = QStringLiteral("Sin nivel medible.");
                else {
                    loudness = f->reading.out_s;
                    description = QStringLiteral("%1 · Sonoridad de salida de los últimos 3 s.").arg(f->label);
                }
            }
        }
        sources.setVisible(sources.count() > 1);
        value.setText(level(loudness));
        setToolTip(description);
    }

    QComboBox sources;
    QLabel value{QStringLiteral("—")};
    QTimer timer;
};

class Dock : public QWidget {
public:
    Dock()
    {
        auto *layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral("FUENTE MEDIDA")));
        layout->addWidget(&sources);
        auto *font = new QLabel(QStringLiteral("SALIDA · SHORT-TERM (3 s)"));
        layout->addWidget(font);
        QFont big = primary.font();
        big.setPointSize(29);
        big.setBold(true);
        primary.setFont(big);
        layout->addWidget(&primary);
        for (auto *label : {&target, &state, &detail, &peaks, &gain})
            layout->addWidget(label);
        state.setWordWrap(true);
        detail.setWordWrap(true);
        peaks.setWordWrap(true);
        gain.setWordWrap(true);
        layout->addWidget(&history);
        auto *legend = new QLabel(QStringLiteral("Gris: entrada · Verde: salida · Ámbar: ajuste de ganancia"));
        legend->setWordWrap(true);
        layout->addWidget(legend);
        auto *buttons = new QHBoxLayout;
        buttons->addWidget(&freeze);
        buttons->addWidget(&reset);
        layout->addLayout(buttons);
        layout->addWidget(new QLabel(QStringLiteral("GUÍA DE SONORIDAD")));
        for (const auto &guide : guides)
            reference.addItem(QString::fromUtf8(guide.title));
        layout->addWidget(&reference);
        guidance.setWordWrap(true);
        layout->addWidget(&guidance);
        layout->addWidget(&guide_delta);
        layout->addWidget(&preset);
        layout->addStretch();

        connect(&freeze, &QPushButton::clicked, this, [this] {
            const std::lock_guard<std::mutex> lock(registry_mutex);
            if (auto *f = selected_filter(sources))
                freeze_hotkey(f, 0, nullptr, true);
        });
        connect(&reset, &QPushButton::clicked, this, [this] {
            const std::lock_guard<std::mutex> lock(registry_mutex);
            if (auto *f = selected_filter(sources))
                f->reset.store(true);
            history.clear();
        });
        connect(&reference, &QComboBox::currentIndexChanged, this, [this](int index) {
            guidance.setText(QString::fromUtf8(guides[index].detail));
        });
        connect(&preset, &QPushButton::clicked, this, [this] {
            obs_source_t *source = nullptr;
            {
                const std::lock_guard<std::mutex> lock(registry_mutex);
                if (auto *f = selected_filter(sources))
                    source = obs_source_get_ref(f->source);
            }
            if (!source)
                return;
            auto *data = obs_source_get_settings(source);
            each_control([&](const Control &c) { obs_data_unset_user_value(data, c.key); });
            obs_data_unset_user_value(data, "meter_only");
            obs_source_update(source, data);
            obs_data_release(data);
            obs_source_release(source);
        });
        guidance.setText(QString::fromUtf8(guides[0].detail));
        connect(&timer, &QTimer::timeout, this, [this] { refresh(); });
        timer.start(100);
        refresh();
    }

private:
    void refresh()
    {
        if (!isVisible())
            return;
        dj::Reading r;
        bool held = false, unsupported = false, available = false, receiving = false;
        double latency = 0;
        int id = 0;
        {
            const std::lock_guard<std::mutex> lock(registry_mutex);
            refresh_sources(sources);
            id = sources.currentData().toInt();
            if (auto *f = selected_filter(sources)) {
                const std::lock_guard<std::mutex> reading_lock(f->reading_mutex);
                r = f->reading;
                receiving = receiving_audio(*f);
                available = true;
                latency = f->latency;
                held = f->freeze.load();
                unsupported = f->unsupported.load();
            }
        }
        if (id != history_source) {
            history.clear();
            history_source = id;
        }
        freeze.setEnabled(available && !unsupported);
        reset.setEnabled(available && !unsupported);
        preset.setEnabled(available && !unsupported);
        freeze.setText(held ? QStringLiteral("Reanudar") : QStringLiteral("Congelar"));
        primary.setText(available && receiving && !unsupported ? level(r.out_s) + QStringLiteral(" LUFS-S") : QStringLiteral("—"));
        target.setText(available ? QStringLiteral("Objetivo %1 · zona %2 a %3 LUFS-S")
            .arg(level(r.target), level(r.target - r.band), level(r.target + r.band)) : QString());
        state.setText(!available ? QStringLiteral("Añade el filtro DJ AutoGain a una fuente.")
            : unsupported ? QStringLiteral("Procesamiento disponible en mono o estéreo.")
            : !receiving ? QStringLiteral("Sin bloques de audio recientes") : status(r));
        detail.setText(available ? QStringLiteral("Entrada M / S / I: %1 / %2 / %3 LUFS\nSalida M / S / I: %4 / %5 / %6 LUFS")
            .arg(level(r.in_m), level(r.in_s), level(r.in_i), level(r.out_m), level(r.out_s), level(r.out_i)) : QString());
        peaks.setText(available ? QStringLiteral("Pico %1 · máx. %2 dBTP\nRetardo de audio: %3 ms")
            .arg(level(r.peak), level(r.max_peak), QString::number(latency, 'f', 2)) : QString());
        gain.setText(available ? QStringLiteral("Ganancia %1 dB · limitador −%2 dB\nLimitando: %3 % del tiempo")
            .arg(level(r.gain), level(r.limiting), QString::number(r.limited_percent, 'f', 1)) : QString());
        const float guide = guides[reference.currentIndex()].integrated_reference;
        guide_delta.setText(available && !unsupported && guide != dj::no_signal && r.out_i != dj::no_signal
            ? QStringLiteral("Salida integrada: %1 LU frente a la referencia").arg(level(r.out_i - guide))
            : QString());
        if (available && !unsupported) {
            if (!receiving)
                r.in_s = r.out_s = dj::no_signal;
            history.add(r);
        }
    }

    QComboBox sources, reference;
    QLabel primary, target, state, detail, peaks, gain, guidance, guide_delta;
    QPushButton freeze{QStringLiteral("Congelar")}, reset{QStringLiteral("Reiniciar estadísticas")},
        preset{QStringLiteral("Aplicar preset DJ Live")};
    History history;
    QTimer timer;
    int history_source = 0;
};

} // namespace

bool obs_module_load(void)
{
    obs_source_info info{};
    info.id = "marcsoul_dj_autogain";
    info.type = OBS_SOURCE_TYPE_FILTER;
    info.output_flags = OBS_SOURCE_AUDIO;
    info.get_name = [](void *) { return "DJ AutoGain"; };
    info.get_defaults = defaults;
    info.get_properties = properties;
    info.create = create;
    info.destroy = destroy;
    info.update = update;
    info.filter_add = attach;
    info.filter_audio = process;
    obs_register_source(&info);

    auto *dock = new Dock;
    if (!obs_frontend_add_dock_by_id(dock_id, "DJ AutoGain", dock))
        delete dock;
    auto *meter = new CompactMeter;
    if (!obs_frontend_add_dock_by_id(meter_dock_id, "DJ AutoGain — Medidor LUFS", meter))
        delete meter;
    return true;
}

void obs_module_unload(void)
{
    obs_frontend_remove_dock(meter_dock_id);
    obs_frontend_remove_dock(dock_id);
}

MODULE_EXPORT const char *obs_module_description(void)
{
    return "Short-term loudness levelling and true-peak protection for DJ sets";
}
