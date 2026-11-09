DJ AutoGain nivela el audio de una mesa o controladora DJ en OBS Studio. Corrige las subidas sostenidas, puede levantar un tema que se mantiene bajo y protege frente a picos de muestra. Aplica la misma ganancia a ambos canales.

- **Windows x64**. Cierra OBS, ejecuta el instalador y vuelve a abrir OBS.
- **macOS universal (Apple Silicon e Intel)**. Cierra OBS, extrae el ZIP y copia `obs-dj-autogain.plugin` a `~/Library/Application Support/obs-studio/plugins/`. El paquete está sin notarizar.

Añade **DJ AutoGain** en los filtros de la fuente de audio de tu mesa. La subida de ganancia se limita por defecto a +3 dB y espera unos 10 segundos de señal elegible. Una cama musical larga puede superar ese criterio si tiene suficiente nivel.

Esta versión preliminar incluye pruebas del motor con señales sintéticas. La evaluación con un set real sigue pendiente.
