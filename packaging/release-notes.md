DJ AutoGain 0.3.0 sustituye la referencia RMS por un objetivo Short-Term en LUFS. El nuevo panel muestra entrada y salida en LUFS-M/S/I, historial, True Peak, reducción del limitador, latencia y guías de sonoridad.

- **DJ Live** parte de −16 LUFS-S con una zona de −18 a −14 y un techo de −2 dBTP. Es un perfil orientativo del plugin.
- **Protección de camas**. Separa la recuperación de atenuación de la amplificación de temas bajos. Permite congelar la ganancia desde el panel o con un atajo.
- **Limitador True Peak**. Estimación 4× y anticipación fija. El panel muestra el retardo introducido.

Cierra OBS antes de instalar. En Windows x64 ejecuta el instalador. En macOS, extrae el ZIP y copia el paquete universal a `~/Library/Application Support/obs-studio/plugins/`. El paquete de macOS está sin notarizar.

Al volver a abrir OBS añade el filtro a tu fuente de audio y abre **Paneles → DJ AutoGain**.

Versión preliminar. Se incluyen pruebas sintéticas a 44,1 y 48 kHz. La escucha con un set real y la validación completa con señales EBU están pendientes.
