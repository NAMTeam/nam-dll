cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio" AND NOT DEFINED CMAKE_GENERATOR_PLATFORM)
    set(CMAKE_GENERATOR_PLATFORM Win32 CACHE STRING "Target platform" FORCE)
endif()

project(NAMDll VERSION 1.3.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "NAM.dll is a Windows DLL and must be built for Win32.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "NAM.dll must be built as 32-bit. Configure with -A Win32 when using Visual Studio.")
endif()

set(NAM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")

set(NAM_SOURCES
    src/Check4GBPatch.cpp
    src/CommuteLoop.cpp
    src/DirtRoadAccess.cpp
    src/FlexPieces.cpp
    src/Logger.cpp
    src/NAMDllDirector.cpp
    src/NetworkSlopes.cpp
    src/Patching.cpp
    src/RuleEquivalence.cpp
    src/Rul2Engine.cpp
    src/SC4VersionDetection.cpp
    src/Settings.cpp
    src/TransitAccess.cpp
    src/TunnelPortalTool.cpp
    src/tunnelportal/Debug.cpp
    src/tunnelportal/GameAccess.cpp
    src/tunnelportal/PathMapView.cpp
    src/tunnelportal/PathStitcher.cpp
    src/tunnelportal/PieceInserter.cpp
    src/tunnelportal/PortalFootprint.cpp
    src/tunnelportal/PortalGeometry.cpp
    src/tunnelportal/PortalPlacer.cpp
    src/tunnelportal/RouteEdgeFixes.cpp
    src/tunnelportal/SavedTunnelScanner.cpp
    src/tunnelportal/Style.cpp
    src/tunnelportal/TerrainPinning.cpp
    src/tunnelportal/TrafficSimTunnels.cpp
    src/tunnelportal/ViewInputControl.cpp
    src/version.rc
)

set(GZCOM_SOURCES
    vendor/gzcom-dll/gzcom-dll/src/cRZBaseUnknown.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZBaseWinProc.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZBaseString.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZBaseVariant.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZCOMDllDirector.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZMessage2.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZMessage2Standard.cpp
    vendor/gzcom-dll/gzcom-dll/src/cS3DVector3.cpp
    vendor/gzcom-dll/gzcom-dll/src/cSC4BaseViewInputControl.cpp
    vendor/gzcom-dll/gzcom-dll/src/cSCBaseProperty.cpp
    vendor/gzcom-dll/gzcom-dll/src/SC4UI.cpp
)

add_library(NAM SHARED ${NAM_SOURCES} ${GZCOM_SOURCES})

target_compile_features(NAM PRIVATE cxx_std_20)

target_include_directories(NAM PRIVATE
    "${NAM_ROOT}/src"
    "${NAM_ROOT}/vendor/gzcom-dll/gzcom-dll/include"
    "${NAM_ROOT}/vendor/mINI/src"
    "${NAM_ROOT}/vendor/wil/include"
)

target_compile_definitions(NAM PRIVATE
    WIN32
    _WINDOWS
    _USRDLL
    _WINDLL
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

if(MSVC)
    set_property(TARGET NAM PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )

    target_compile_options(NAM PRIVATE /W3 /EHsc /permissive-)
    target_compile_options(NAM PRIVATE
        "$<$<CONFIG:Release>:/O2;/Oi;/Gy>"
        "$<$<CONFIG:RelWithDebInfo>:/O2;/Oi;/Gy>"
        "$<$<CONFIG:MinSizeRel>:/O1;/Oi;/Gy>"
    )

    target_link_options(NAM PRIVATE
        "$<$<CONFIG:Release>:/OPT:REF;/OPT:ICF>"
        "$<$<CONFIG:RelWithDebInfo>:/OPT:REF;/OPT:ICF>"
        "$<$<CONFIG:MinSizeRel>:/OPT:REF;/OPT:ICF>"
    )
endif()

target_link_libraries(NAM PRIVATE version ole32)

set_target_properties(NAM PROPERTIES
    OUTPUT_NAME "NAM"
    PREFIX ""
    SUFFIX ".dll"
)

set(NAM_PLUGIN_DIR "$ENV{USERPROFILE}/Documents/SimCity 4/Plugins"
    CACHE PATH "SimCity 4 Plugins directory used for post-build deployment"
)

if(NOT DEFINED ENV{CI})
    add_custom_command(TARGET NAM POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NAM_PLUGIN_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:NAM>"
            "${NAM_PLUGIN_DIR}/$<TARGET_FILE_NAME:NAM>"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NAM_ROOT}/resources/tunnel-portal-styles/NAM_TunnelPortalStyles.dat"
            "${NAM_PLUGIN_DIR}/NAM_TunnelPortalStyles.dat"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${NAM_ROOT}/resources/tunnel-portal-styles/NAM_TunnelPortalStyleExamples.dat"
            "${NAM_PLUGIN_DIR}/NAM_TunnelPortalStyleExamples.dat"
        COMMENT "Deploying NAM.dll to ${NAM_PLUGIN_DIR}"
    )
endif()
