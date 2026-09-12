# LiteDAW

DAW ultra liviano con hosting VST3, mixer y playlist, pensado para correr
bien en hardware débil (target: HP Stream 14).

## Estado actual

Ya tiene un motor de audio real, no solo UI de maqueta:

- **`AudioEngine`**: mezcla los clips de audio cargados (leídos desde
  disco, respetando su posición en el tiempo), suma la entrada en vivo
  de la interfaz de audio si el canal tiene una asignada, pasa todo por
  el plugin VST3 del canal si tiene uno cargado, aplica gain/mute/solo,
  y calcula un nivel RMS real para los meters. Es un
  `juce::AudioIODeviceCallback` directo (no un `AudioSource` vía
  `AudioSourcePlayer`) justo para poder leer la entrada real del
  hardware.
- **Entrada de la interfaz por canal**: botón "In:" en cada canal del
  mixer — deja elegir qué entrada física de la interfaz de audio (mic,
  guitarra, etc.) alimenta ese canal en vivo, sumada a lo que venga de
  la playlist. Se puebla desde `AudioEngine::getNumHardwareInputChannels()`,
  que sigue lo que esté activo en "Audio/MIDI...".
- **Playlist**: doble click sobre una pista abre un selector de archivo;
  el clip se agrega con el largo real del audio.
- **Mixer**: fader/mute/solo/plugin/input de cada canal actúan en vivo
  sobre el `AudioEngine` (no son solo controles visuales).
- **Botón "+ Pista"**: agrega pista + canal de mixer en simultáneo (van
  1:1 por índice).
- **Cadena de plugins por canal**: el slot "Plugins (n)" de cualquier
  canal abre un menú que deja **agregar más de un plugin** (se procesan
  en serie, en el orden en que se agregan), **reordenarlos** ("Subir"/
  "Bajar" en la cadena), **quitarlos** individualmente, y **ver la GUI
  nativa** de cada uno ("Ver GUI") en una ventana aparte — la ventana solo
  se crea al pedirla, nunca antes, así que un plugin cuya GUI no se abre
  nunca no paga ese costo. "Escanear VST3..." solo puebla la lista una
  vez — no crea un canal por cada plugin encontrado, para no volverse
  pesado con muchos plugins instalados.
- **Hosting VST3**: escaneo + instanciación de plugins (`PluginHost`).
- **Guardar/Abrir proyecto** (`ProjectState`): un XML de texto plano
  (`.litedaw`) con pistas, clips (ruta de archivo, posición, largo) y el
  gain/mute/solo de cada canal. Deliberadamente no guarda instancias de
  plugin — reinstanciar un VST3 con su estado interno exacto complica el
  formato y le suma peso; después de abrir un proyecto, los plugins se
  vuelven a cargar a mano desde el slot de cada canal.

## Limitaciones conocidas (para no confundirlas con bugs)

1. **Sincronización de la cadena de plugins con el hilo de audio** usa un
   `juce::SpinLock` simple al reemplazar la cadena completa. Es correcto
   pero no es el diseño más sofisticado (un `AudioProcessorGraph` de JUCE
   sería el camino "canónico" a futuro).
2. **Sin compensación de latencia entre canales (PDC)**: si un plugin de
   la cadena reporta `getLatencySamples() > 0` (delay/lookahead interno),
   ese canal queda desalineado en el tiempo contra los demás. Con cadenas
   cortas de plugins pensados para tocar en vivo (baja latencia por
   diseño) esto no suele notarse, pero un plugin de mastering con mucho
   lookahead sí podría notarse.
3. **Los proyectos no guardan la cadena de plugins cargada**, solo
   pistas/clips y gain/mute/solo del mixer (ver arriba, es intencional).
4. **Sin waveform dibujada**: los clips se ven como bloques de color,
   deliberado para no gastar CPU dibujando/cacheando miles de samples.
5. **El input asignado por canal no se guarda en el proyecto** (`.litedaw`
   solo guarda pistas/clips y gain/mute/solo, ver arriba) — después de
   abrir un proyecto hay que reasignar la entrada de interfaz a mano.
6. **Entrada mono por canal**: el selector de input asigna un solo canal
   físico de la interfaz por canal del mixer (no pares estéreo todavía).
7. **ASIO no viene incluido**: por licencia, el SDK de Steinberg no se
   puede redistribuir en este repo. Sin él, la app usa automáticamente el
   mejor driver disponible sin SDK propietario (WASAPI en modo exclusivo
   en Windows). Ver el comentario en `CMakeLists.txt` para habilitarlo.

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

## Usar una interfaz de audio dedicada (ASIO / Focusrite, etc.)

Si tocás en vivo con una interfaz de audio real (Focusrite Scarlett,
Clarett, Vocaster, o cualquier otra marca), esto es lo que conviene
tener instalado para que LiteDAW la agarre automáticamente con la menor
latencia posible:

1. **Instalá el driver oficial del fabricante**, no uses solo el genérico
   de Windows. Para Focusrite: instalá **Focusrite Control** (interfaces
   más viejas) o **Focusrite Control 2** (Scarlett 4ta gen en adelante)
   desde el sitio de Focusrite — eso instala el driver ASIO nativo
   ("Focusrite USB ASIO"), que es muchísimo mejor que dejar que Windows
   la maneje como dispositivo genérico WASAPI.
2. **Conectá la interfaz antes de abrir LiteDAW.** Al arrancar, la app:
   - Elige el driver de menor latencia disponible (ASIO si la app se
     compiló con soporte, ver abajo; si no, WASAPI exclusivo).
   - Dentro de ese driver, busca por nombre tu interfaz (Focusrite,
     Scarlett, RME, etc.) y la selecciona sola en vez del audio
     integrado del laptop.
   - Si más tarde la cambiás a mano en "Audio/MIDI...", esa elección
     queda (la selección automática es solo al arrancar).
3. **Para tener ASIO disponible en el build** (mejor que WASAPI
   exclusivo): el SDK de ASIO es de Steinberg y su licencia no permite
   redistribuirlo en este repo. Descargalo de
   https://www.steinberg.net/developers/, descomprimilo en
   `ThirdParty/ASIOSDK` (junto al `CMakeLists.txt`), y volvé a correr
   cmake — se detecta solo y compila con `JUCE_ASIO=1`. Sin el SDK, la
   app compila y anda igual, solo que usa WASAPI en vez de ASIO.
4. Podés confirmar que quedó bien mirando la etiqueta de latencia en la
   barra de herramientas: debería decir "ASIO" (o "Windows Audio
   (Exclusive Mode)") y el nombre de tu interfaz en el selector de
   "Audio/MIDI...", no "Realtek" ni "Speakers".

## Por qué estas decisiones de rendimiento

- **Tipo de driver de audio de menor latencia disponible, elegido solo**:
  al arrancar, `selectLowestLatencyDeviceType()` recorre los tipos de
  dispositivo que JUCE tiene disponibles y prioriza ASIO (si se compiló
  con soporte, ver `CMakeLists.txt`) > WASAPI en modo exclusivo > WASAPI
  compartido > DirectSound (y CoreAudio/JACK/ALSA en otros sistemas
  operativos). WASAPI compartido/DirectSound pasan el audio por el
  mezclador del sistema operativo, que agrega sus propios buffers extra
  por encima del nuestro — el modo exclusivo (o ASIO) evita eso.
- **Selección automática de la interfaz de audio, si hay una conectada**:
  `preferAudioInterfaceDevice()` corre una sola vez al arrancar (antes de
  que el usuario toque nada) y, dentro del tipo de driver ya elegido,
  busca por nombre una interfaz dedicada conocida (Focusrite
  Scarlett/Clarett/Vocaster, RME, PreSonus, MOTU, Behringer UMC,
  Universal Audio, Audient, Steinberg, Native Instruments, etc.) y la
  prefiere sobre el audio integrado del laptop (Realtek, "Speakers", el
  mic interno). Ignora a propósito wrappers genéricos como "ASIO4ALL" —
  aunque estén envolviendo esa misma interfaz, un driver nativo real
  (cuando existe) da mejor latencia y estabilidad. Si el usuario cambia
  el dispositivo a mano después desde "Audio/MIDI...", esa elección
  queda como está — esta función no la vuelve a pisar.
- **Indicador de latencia real en la barra de herramientas**: muestra el
  tipo de driver activo y una estimación en ms (buffer + latencia de
  entrada/salida que reporte el propio driver), para poder confirmar de
  un vistazo que la configuración actual sirve para tocar en vivo sin
  tener que abrir "Audio/MIDI..." a adivinar. Se actualiza solo cuando el
  dispositivo cambia (vía `juce::ChangeListener`), no en cada frame.
- **Buffer chico por defecto (auto-detectado, con piso de 256 si no hay
  info del dispositivo)**: antes estaba fijo en 1024 muestras
  (~23ms), pensado solo para reproducir pistas ya armadas. Se cambió a
  baja latencia porque ahora la app también sirve para tocar un teclado
  MIDI en vivo o monitorear un efecto en tiempo real, donde 23ms se
  siente como un delay molesto. El botón "Audio/MIDI..." deja ajustarlo
  a mano (y elegir qué entradas MIDI están activas) si hace falta
  compensar con más estabilidad en un CPU muy limitado.
- **Cadena de plugins procesada en serie sin copias de MIDI extra**: cada
  plugin de la cadena de un canal recibe el mismo `MidiBuffer` ya armado
  para ese bloque (no se copia de nuevo por cada eslabón), para no
  arriesgar una realocación dentro del callback de audio — la misma razón
  por la que el resto del motor evita alocar ahí (ver más abajo).
- **La GUI nativa de un plugin se crea recién al pedirla ("Ver GUI")**,
  nunca al cargarlo: instanciar el editor de algunos plugins puede ser
  pesado, y con varios plugins por canal esa GUI casi nunca se abre en
  medio de un show en vivo.
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
