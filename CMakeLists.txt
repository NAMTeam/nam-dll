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
    src/version.rc
)

set(GZCOM_SOURCES
    vendor/gzcom-dll/gzcom-dll/src/cRZBaseString.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZBaseVariant.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZCOMDllDirector.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZMessage2.cpp
    vendor/gzcom-dll/gzcom-dll/src/cRZMessage2Standard.cpp
    vendor/gzcom-dll/gzcom-dll/src/cS3DVector3.cpp
    vendor/gzcom-dll/gzcom-dll/src/cSCBaseProperty.cpp
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

option(NAM_BUILD_TESTS "Build the unit tests" ON)

if(NAM_BUILD_TESTS)
    if(NOT EXISTS "${NAM_ROOT}/vendor/doctest/doctest/doctest.h")
        message(FATAL_ERROR
            "vendor/doctest is missing. Run `git submodule update --init`, "
            "or configure with -DNAM_BUILD_TESTS=OFF."
        )
    endif()

    enable_testing()

    # One executable: the tests share a doctest main and are reported per case.
    add_executable(NAMTests
        tests/TestMain.cpp
        tests/NetworkMaskTests.cpp
        tests/PatchingTests.cpp
        tests/TransitAccessGeometryTests.cpp
        src/DirtRoadAccess.cpp
        src/Patching.cpp
    )

    target_compile_features(NAMTests PRIVATE cxx_std_20)

    target_include_directories(NAMTests PRIVATE
        "${NAM_ROOT}/src"
        "${NAM_ROOT}/vendor/doctest"
        "${NAM_ROOT}/vendor/gzcom-dll/gzcom-dll/include"
        "${NAM_ROOT}/vendor/wil/include"
    )

    target_compile_definitions(NAMTests PRIVATE
        WIN32
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )

    if(MSVC)
        set_property(TARGET NAMTests PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
        )
        target_compile_options(NAMTests PRIVATE /W3 /EHsc /permissive-)
    endif()

    target_link_libraries(NAMTests PRIVATE ole32)

    # Register every doctest case as its own CTest test.
    list(APPEND CMAKE_MODULE_PATH "${NAM_ROOT}/vendor/doctest/scripts/cmake")
    include(doctest)
    doctest_discover_tests(NAMTests)
endif()

set(NAM_PLUGIN_DIR "$ENV{USERPROFILE}/Documents/SimCity 4/Plugins"
    CACHE PATH "SimCity 4 Plugins directory used for post-build deployment"
)

if(NOT DEFINED ENV{CI})
    add_custom_command(TARGET NAM POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NAM_PLUGIN_DIR}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:NAM>"
            "${NAM_PLUGIN_DIR}/$<TARGET_FILE_NAME:NAM>"
        COMMENT "Deploying NAM.dll to ${NAM_PLUGIN_DIR}"
    )
endif()
