# LiteDAW

DAW ultra liviano con hosting VST3, mixer y playlist, pensado para correr
bien en hardware débil (target: HP Stream 14).

## Estado actual

Ya tiene un motor de audio real, no solo UI de maqueta:

- **`AudioEngine`**: mezcla los clips de audio cargados (leídos desde
  disco, respetando su posición en el tiempo), los pasa por el plugin
  VST3 del canal si tiene uno cargado, aplica gain/mute/solo, y calcula
  un nivel RMS real para los meters.
- **Playlist**: doble click sobre una pista abre un selector de archivo;
  el clip se agrega con el largo real del audio.
- **Mixer**: fader/mute/solo/plugin de cada canal actúan en vivo sobre
  el `AudioEngine` (no son solo controles visuales).
- **Botón "+ Pista"**: agrega pista + canal de mixer en simultáneo (van
  1:1 por índice).
- **Cargar/quitar plugin por canal**: click en el slot de plugin de
  cualquier canal abre un menú con los plugins ya escaneados y lo asigna
  a ese canal específicamente (pista o no). "Escanear VST3..." ahora
  solo puebla la lista una vez — ya no crea un canal por cada plugin
  encontrado, para no volverse pesado con muchos plugins instalados.
- **Hosting VST3**: escaneo + instanciación de plugins (`PluginHost`).
- **Guardar/Abrir proyecto** (`ProjectState`): un XML de texto plano
  (`.litedaw`) con pistas, clips (ruta de archivo, posición, largo) y el
  gain/mute/solo de cada canal. Deliberadamente no guarda instancias de
  plugin — reinstanciar un VST3 con su estado interno exacto complica el
  formato y le suma peso; después de abrir un proyecto, los plugins se
  vuelven a cargar a mano desde el slot de cada canal.

## Limitaciones conocidas (para no confundirlas con bugs)

1. **Sincronización del plugin con el hilo de audio** usa un
   `juce::SpinLock` simple al cambiar el puntero del plugin. Es correcto
   pero no es el diseño más sofisticado (un `AudioProcessorGraph` de JUCE
   sería el camino "canónico" a futuro).
2. **Sin editor visual de plugin todavía**: el slot carga el plugin y lo
   deja procesando audio, pero no abre su GUI nativa para tocar
   parámetros. Se agrega llamando a `pluginInstance->createEditorIfNeeded()`
   en un `DialogWindow` — pendiente porque la GUI nativa de algunos
   plugins puede ser pesada, y quisimos evaluarlo con más cuidado.
3. **Los proyectos no guardan plugins cargados**, solo pistas/clips y
   gain/mute/solo del mixer (ver arriba, es intencional).
4. **Sin waveform dibujada**: los clips se ven como bloques de color,
   deliberado para no gastar CPU dibujando/cacheando miles de samples.

## Cómo compilar

No hace falta compilar localmente en el Stream 14. El workflow
`.github/workflows/build-windows.yml` compila en un runner de Windows y
sube el `.exe` como artifact — mismo flujo que ya usás para los plugins
de Developer Komodo. Usa Ninja + `ilammy/msvc-dev-cmd` (en vez de fijar
un generador tipo "Visual Studio 17 2022") para no depender de qué
versión de Visual Studio trae instalada la imagen del runner en un
momento dado.

Si querés compilar en otra máquina:

```bash
git clone <este-repo>
cd LiteDAW
git clone --branch master https://github.com/juce-framework/JUCE.git
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

## Por qué estas decisiones de rendimiento

- **Buffer chico por defecto (auto-detectado, con piso de 256 si no hay
  info del dispositivo)**: antes estaba fijo en 1024 muestras
  (~23ms), pensado solo para reproducir pistas ya armadas. Se cambió a
  baja latencia porque ahora la app también sirve para tocar un teclado
  MIDI en vivo o monitorear un efecto en tiempo real, donde 23ms se
  siente como un delay molesto. El botón "Audio/MIDI..." deja ajustarlo
  a mano (y elegir qué entradas MIDI están activas) si hace falta
  compensar con más estabilidad en un CPU muy limitado.
- **MIDI en vivo**: `AudioEngine` recolecta los mensajes de cualquier
  entrada MIDI habilitada con `juce::MidiMessageCollector` y se los
  pasa a cada plugin de canal en cada bloque de audio, toque o no el
  transporte — así un instrumento VST3 responde al teclado sin
  necesidad de tener nada en la playlist.
- **Sin alocación de memoria en el callback de audio**: el buffer
  intermedio por canal se reserva una sola vez (`prepareToPlay`) y se
  reutiliza; alocar en el hilo de audio es una causa clásica de
  clics/xruns, y se nota mucho más cuanto más bajo es el buffer.
- **Sin waveforms renderizadas**: evita procesar/cachear miles de
  samples solo para pintar la UI.
- **Escaneo de plugins manual**: no se escanea al abrir la app, para no
  colgar el arranque con el disco eMMC lento del Stream 14.
- **Meters a 15fps**: de sobra para el ojo humano, mucho más barato que
  los 60fps típicos de una GUI de DAW "pro".
