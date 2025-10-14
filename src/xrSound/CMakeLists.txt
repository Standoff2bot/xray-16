add_library(xrSound)

target_sources_grouped(
    TARGET xrSound
    NAME "Core"
    FILES
    SoundRender_Core.cpp
    SoundRender_Core.h
    SoundRender_Core_Processor.cpp
    SoundRender_Core_SourceManager.cpp
    SoundRender_Core_StartStop.cpp
)

target_sources_grouped(
    TARGET xrSound
    NAME "Core\\OpenAL"
    FILES
    OpenALDeviceList.cpp
    OpenALDeviceList.h
    SoundRender_CoreA.cpp
    SoundRender_CoreA.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Effects"
    FILES
    SoundRender_Effects.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Effects\\OpenAL"
    FILES
    SoundRender_EffectsA_EAX.cpp
    SoundRender_EffectsA_EAX.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Emitter"
    FILES
    SoundRender_Emitter.cpp
    SoundRender_Emitter.h
    SoundRender_Emitter_FSM.cpp
    SoundRender_Emitter_StartStop.cpp
)

target_sources_grouped(
    TARGET xrSound
    NAME "Environment"
    FILES
    SoundRender_Environment.cpp
    SoundRender_Environment.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Scene"
    FILES
    SoundRender_Scene.cpp
    SoundRender_Scene.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Kernel"
    FILES
    guids.cpp
    Sound.cpp
    Sound.h
    SoundRender.h
    stdafx.cpp
    stdafx.h
)

set_source_files_properties(guids.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

# Steam Audio integration (conditional - enabled if phonon.h is found)
if (EXISTS "${CMAKE_SOURCE_DIR}/sdk/include/phonon.h")
    target_sources_grouped(
        TARGET xrSound
        NAME "SteamAudio"
        FILES
        SteamAudio/SteamAudioContext.cpp
        SteamAudio/SteamAudioContext.h
        SteamAudio/SteamAudioScene.cpp
        SteamAudio/SteamAudioScene.h
        SteamAudio/SteamAudioSource.cpp
        SteamAudio/SteamAudioSource.h
        SteamAudio/SteamAudioMaterials.cpp
        SteamAudio/SteamAudioMaterials.h
    )
endif()

target_sources_grouped(
    TARGET xrSound
    NAME "Source"
    FILES
    SoundRender_Source.cpp
    SoundRender_Source.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Target"
    FILES
    SoundRender_Target.cpp
    SoundRender_Target.h
)

target_sources_grouped(
    TARGET xrSound
    NAME "Target\\OpenAL"
    FILES
    SoundRender_TargetA.cpp
    SoundRender_TargetA.h
)

#target_sources_grouped(
#    TARGET xrSound
#    NAME "Stream"
#    FILES
#    MusicStream.cpp
#    MusicStream.h
#    xr_streamsnd.cpp
#    xr_streamsnd.h
#)

#target_sources_grouped(
#    TARGET xrSound
#    NAME "CDA"
#    FILES
#    xr_cda.cpp
#    xr_cda.h
#)

target_include_directories(xrSound
    PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/xrEngine"
    "${CMAKE_SOURCE_DIR}/sdk/include"
)

target_link_libraries(xrSound
    PRIVATE
    xrCore
    xrMiscMath
    xrAPI
    xrCDB
    xrMaterialSystem
    OpenAL::OpenAL
    Ogg::Ogg
    Vorbis::Vorbis
    Vorbis::VorbisFile
)

# Link Steam Audio if available
if (EXISTS "${CMAKE_SOURCE_DIR}/sdk/include/phonon.h")
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        # 64-bit build
        target_link_libraries(xrSound PRIVATE "${CMAKE_SOURCE_DIR}/sdk/libraries/x64/phonon.lib")
    else()
        # 32-bit build
        target_link_libraries(xrSound PRIVATE "${CMAKE_SOURCE_DIR}/sdk/libraries/x86/phonon.lib")
    endif()
    message(STATUS "Steam Audio enabled for xrSound")
endif()

target_compile_definitions(xrSound
    PRIVATE
    XRSOUND_EXPORTS
)

set_target_properties(xrSound PROPERTIES
    PREFIX ""
)

target_precompile_headers(xrSound
    PRIVATE
    stdafx.h
)

if (BUILD_SHARED_LIBS)
    install(TARGETS xrSound LIBRARY
        DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    )
endif()
