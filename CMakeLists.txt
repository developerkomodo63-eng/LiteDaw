cmake_minimum_required(VERSION 3.22)

project(LiteDAW VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE ---------------------------------------------------------------
# Se asume JUCE clonado como submódulo/carpeta hermana. Si usás Projucer,
# podés ignorar esto y generar el proyecto desde ahí en su lugar.
add_subdirectory(JUCE)

juce_add_gui_app(LiteDAW
    PRODUCT_NAME "LiteDAW"
    COMPANY_NAME "Developer Komodo"
    VERSION "0.1.0"
)

# --- Habilitar hosting de plugins (VST3) --------------------------------
juce_generate_juce_header(LiteDAW)

target_compile_definitions(LiteDAW
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_VST=0          # VST2 requiere SDK propietario aparte, lo dejamos apagado
        JUCE_APPLICATION_NAME_STRING="\"$<TARGET_PROPERTY:LiteDAW,JUCE_PRODUCT_NAME>\""
        JUCE_APPLICATION_VERSION_STRING="\"$<TARGET_PROPERTY:LiteDAW,JUCE_VERSION>\""
)

target_sources(LiteDAW
    PRIVATE
        Source/Main.cpp
        Source/MainComponent.cpp
        Source/MixerComponent.cpp
        Source/PlaylistComponent.cpp
        Source/PluginHost.cpp
        Source/AudioEngine.cpp
        Source/ProjectState.cpp
)

target_link_libraries(LiteDAW
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# --- Optimización de tamaño / rendimiento para hardware débil -----------
if (MSVC)
    target_compile_options(LiteDAW PRIVATE /O2 /GL)
    target_link_options(LiteDAW PRIVATE /LTCG)
endif()
