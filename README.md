# DJ AutoGain para OBS Studio

Prototipo 0.2.0 de un filtro nativo para nivelar la entrada estéreo de una mesa
DJ o controladora. Aplica una ganancia común a los dos canales antes de que la
fuente se mezcle con el resto del audio de OBS.

## Ajustes iniciales

| Control | Valor inicial | Comportamiento |
| --- | --- | --- |
| Objetivo RMS | −18 dBFS | Referencia para la energía media |
| Subida máxima | +3 dB | Permite hasta +6 dB. Con 0 dB, solo atenúa |
| Nivel mínimo de recuperación y subida | −30 dBFS RMS | Con la subida habilitada, por debajo de este nivel se impide aumentar la ganancia |
| Atenuación máxima | 18 dB | Límite de corrección del autogain, independiente del limitador |
| Pausa de ajuste | −42 dBFS RMS | Congela la ganancia por debajo de este nivel de entrada |
| Margen de reacción rápida | +3 dB | Exceso de nivel estimado sobre el objetivo que activa la vía rápida |
| Recuperación | 12 s | Constante de tiempo para devolver ganancia |
| Techo del limitador | −2 dBFS | Techo de pico de muestra a la salida del filtro |

El detector usa ventanas de 100 ms y una media exponencial de 3 s. La
atenuación normal tiene una constante de 2 s. Tras tres ventanas consecutivas
sobre el margen, la vía rápida reduce su constante a 250 ms y toma en cuenta
el nivel de la ventana actual. Los tiempos son constantes exponenciales.
La recuperación efectiva depende también de cuánto tarda en bajar la media.

La ganancia positiva requiere unos 10 s consecutivos con señal por encima
del nivel mínimo y con el promedio lento por debajo del objetivo. Después
sube con la misma constante de recuperación de 12 s. Si la señal cae por
debajo del nivel mínimo, conserva la ganancia aplicada y reinicia la espera.
Por encima del mínimo, una atenuación anterior puede volver hacia 0 dB
durante la espera. Una cama larga y con energía suficiente puede cumplir
las mismas condiciones que un tema bajo.

El limitador actúa en la muestra que rebasa el techo y recupera con una
constante de 80 ms. El procesamiento no añade búfer de anticipación ni
latencia de muestras. El RMS carece de ponderación perceptual y el limitador
mide picos de muestra. Una intervención frecuente del limitador puede
cambiar la pegada. El clipping previo en la mesa o interfaz ya forma parte
de la señal y queda fuera del alcance del filtro.

## Instalación

Los paquetes se publican en [GitHub Releases](https://github.com/MarcSoul/obs-dj-autogain/releases).
Cierra OBS antes de instalar.

- **Windows x64**. Ejecuta el instalador `.exe`.
- **macOS (Apple Silicon e Intel)**. Extrae el ZIP y copia el paquete
  `.plugin` a `~/Library/Application Support/obs-studio/plugins/`.
  El paquete está sin notarizar.

Vuelve a abrir OBS y añade **DJ AutoGain** en los filtros de la fuente de
audio de la mesa o controladora.

## Compilación

Se necesita CMake 3.20 o posterior, un compilador C++17 y el SDK o paquete de
desarrollo de libobs correspondiente a la versión y arquitectura de OBS que
va a cargar el módulo. Si CMake no encuentra el paquete, añade a la primera
orden `-Dlibobs_DIR=/ruta/a/la/carpeta/que/contiene/libobsConfig.cmake`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Instala el módulo compilado en la ruta de plugins que corresponda a tu
instalación de OBS y reinicia OBS. En el mezclador, abre **Filtros** en la
fuente de la controladora y añade **DJ AutoGain**. Si usas otros filtros de
ganancia, coloca DJ AutoGain después de ellos para que el limitador también
recoja sus subidas.

## Pruebas del motor sin OBS

```sh
cmake -S . -B build-dsp -DBUILD_OBS_PLUGIN=OFF
cmake --build build-dsp --config Release
ctest --test-dir build-dsp -C Release --output-on-failure
```

Se ejecutaron pruebas del motor a 44,1 y 48 kHz con subidas sostenidas,
picos, silencios y señales sintéticas que simulan camas e intros. La carga
en OBS y la escucha con un set real están pendientes.
