# DJ AutoGain para OBS Studio

Filtro para nivelar la salida mono o estéreo de una mesa o controladora DJ.
El nivelador mueve una ganancia común a ambos canales y el limitador se
ocupa de los picos. Se aplica a una fuente, antes de su mezcla con el resto
del audio de OBS.

## Instalación y primera prueba

Descarga un paquete de [GitHub Releases](https://github.com/MarcSoul/obs-dj-autogain/releases)
y cierra OBS antes de instalar.

- **Windows x64**. Ejecuta el instalador.
- **macOS (Apple Silicon e Intel)**. Extrae el ZIP y copia
  `obs-dj-autogain.plugin` a `~/Library/Application Support/obs-studio/plugins/`.
  El paquete no está notarizado.

En OBS añade **DJ AutoGain** en los filtros de la fuente de la mesa y abre
**Paneles → DJ AutoGain**. Selecciona esa fuente en el panel. Empieza con el
perfil **DJ Live**, o usa **Solo medir** para observar tu mezcla antes de
aplicar corrección. El perfil trabaja con un objetivo de **−16 LUFS-S**,
una zona de **−18 a −14 LUFS-S** y un techo de **−2 dBTP**. Son valores
orientativos para pasajes activos del set.

La versión 0.3 cambia de RMS a LUFS. Las instancias existentes conservan
los límites de ganancia en dB y el tiempo de subida, pero adoptan los
nuevos valores iniciales para los controles que cambian de métrica.
**Aplicar preset DJ Live** vuelve a los valores de fábrica del filtro.

## Qué mide el panel

| Indicador | Lectura |
| --- | --- |
| LUFS-M | Sonoridad momentánea sobre 400 ms. Sirve para detectar exceso sostenido durante un solape. |
| LUFS-S | Sonoridad sobre los últimos 3 s. Es la referencia principal del nivelador. |
| LUFS-I | Sonoridad integrada con puerta desde el último reinicio, estimada mediante un histograma de 0,01 LU. |
| True Peak | Pico estimado entre muestras mediante interpolación 4×. El máximo permanece hasta reiniciar estadísticas. |
| Ganancia | Ajuste aplicado por el nivelador. Congelarlo deja el limitador activo. |
| Limitador | Reducción actual y porcentaje de muestras con más de 0,1 dB de reducción. |

El historial muestra unos 30 s con el panel visible. Gris es la entrada,
verde la salida y ámbar el ajuste de ganancia, cuyo cero se dibuja a la
altura de −20 en la gráfica. Las guías de plataforma son informativas.
Spotify y Apple Music se comparan con LUFS integrados, no con el objetivo
Short-Term. Los valores de DJ Live para YouTube y Facebook son una
orientación del plugin. El perfil EBU Radio se refiere a producción,
no al procesamiento del transmisor FM.

## Ajustes y comportamiento

- **Zona de trabajo**. Si el nivel se sale de la zona, el filtro corrige
  con suavidad. La vía rápida se activa tras un exceso momentáneo sostenido.
- **Camas y temas bajos**. Por debajo del nivel mínimo se mantiene la
  ganancia. Por encima se puede recuperar atenuación anterior. Para
  amplificar por encima de 0 dB se exige unos 10 s de nivel bajo elegible.
  La subida está limitada a +3 dB de fábrica y se frena si falta margen
  de pico. Una cama larga puede cumplir el mismo criterio que un tema bajo.
- **Congelar**. Conserva la ganancia del nivelador. También puedes asignar
  un atajo de OBS a **DJ AutoGain · Congelar ganancia**.
- **True Peak**. El limitador utiliza anticipación fija de unos 5 ms y
  reserva internamente 0,2 dB. La latencia exacta se muestra en el panel
  y se conserva en Solo medir. El techo se refiere a la salida PCM del
  filtro. Comprueba la sincronía con vídeo y el nivel final de programa
  si hay más fuentes, ganancia posterior o codificación con pérdida.

## Compilación y pruebas

Para el módulo necesitas un compilador C++17, CMake, los paquetes de
desarrollo de libobs y obs-frontend-api, y Qt 6 Widgets. El workflow de
GitHub usa OBS Plugin Template para crear los paquetes de distribución.

Para probar el motor de forma independiente:

```sh
cmake -S . -B build-dsp -DBUILD_OBS_PLUGIN=OFF
cmake --build build-dsp --config Release
ctest --test-dir build-dsp -C Release --output-on-failure
```

Las pruebas sintéticas verifican la referencia de 997 Hz de BS.1770, la
puerta del integrado, el comportamiento de la ganancia, la detección de
picos entre muestras, el enlace estéreo y el retardo. La escucha de un
set real y la validación de conformidad con toda la batería EBU quedan
pendientes.

Referencias: [ITU-R BS.1770-5](https://www.itu.int/rec/R-REC-BS.1770-5-202311-I/en),
[EBU Tech 3341](https://tech.ebu.ch/publications/tech3341),
[AES TD1008](https://aes.org/community/technical-council/technical-document-aestd1008/).
