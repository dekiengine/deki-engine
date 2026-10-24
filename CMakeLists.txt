# CMake configuration for building deki-engine
#
# Two build modes:
#   1. DEKI_EDITOR=ON: Shared library for editor integration
#   2. Default: Static library for embedded targets
#
# Build instructions:
#   mkdir build && cd build
#   cmake .. -DCMAKE_BUILD_TYPE=Release
#   cmake --build . --config Release
#
# For editor build:
#   cmake .. -DDEKI_EDITOR=ON

cmake_minimum_required(VERSION 3.16)
project(deki-engine VERSION 0.20.1 LANGUAGES C CXX)

# Transform width (include/deki/Object.h, deki-editor/COMPATIBILITY.md).
# Whoever configures this tree says which: the editor forces both on so it can
# open any project, and the generated project builds pass what the project's
# packages declare. An unset value means 2D, so a plain configure of this tree
# is the engine everyone had before the switch existed.
if(NOT DEFINED DEKI_TRANSFORM_2D)
    set(DEKI_TRANSFORM_2D ON)
endif()
if(NOT DEFINED DEKI_TRANSFORM_3D)
    set(DEKI_TRANSFORM_3D OFF)
endif()
if(DEKI_TRANSFORM_3D)
    set(DEKI_TRANSFORM_2D ON)  # 3D implies 2D; Object.h errors on the reverse
endif()
set(_DEKI_TRANSFORM_DEFINES)
if(DEKI_TRANSFORM_2D)
    list(APPEND _DEKI_TRANSFORM_DEFINES DEKI_TRANSFORM_2D)
endif()
if(DEKI_TRANSFORM_3D)
    list(APPEND _DEKI_TRANSFORM_DEFINES DEKI_TRANSFORM_3D)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# Core Engine Library (deki-engine-core)
# =============================================================================
# This static library contains the portable engine core that compiles on all
# targets; which ones exist is not something the engine knows.
#
# Components write to RGB565 buffers - the display backend is separate.

set(DEKI_CORE_SOURCES
    # Core runtime classes
    src/Object.cpp
    src/Component.cpp
    src/Color.cpp
    src/ComponentInterfaceAdapters.cpp
    src/assets/Texture2D.cpp
    src/providers/Memory.cpp
    src/providers/Buffer.cpp
    src/providers/OperatorNew.cpp
)

# =============================================================================
# Packages are not discovered here
# =============================================================================
# Packages live in their own repos, are installed into a project's packages/
# directory, and are built per-project by BuildFileGenerator/CMakeGenUtils,
# which parse each package.cmake at runtime. This file used to glob
# src/packages/*/package.cmake and call deki_add_package() on the matches, but
# src/packages/ has no subdirectories: the glob matched nothing, and every
# block consuming its output (deferred links, core/static source collection,
# gen-file exclusion) was dead along with it.

# Full engine runtime sources (shared between editor and embedded)
list(APPEND DEKI_CORE_SOURCES
    src/Engine.cpp
    src/Storage.cpp
    src/LifecycleManager.cpp
    src/Scene.cpp
    src/SceneSystem.cpp
    src/SceneMessagePack.cpp
    src/ProjectSettings.cpp
    src/FPSCounter.cpp
    src/LogSystem.cpp
    src/Time.cpp
    src/Guid.cpp
    src/reflection/ComponentRegistry.cpp
    src/reflection/ComponentFactory.cpp
    src/reflection/PropertyRef.cpp
    src/reflection/FormerTypeIds.cpp
    src/reflection/SettingsRegistry.cpp
    src/providers/DekiProviderRegistryBase.cpp
    src/providers/Power.cpp
    src/settings/CoreProjectSettings.cpp
    src/settings/UnitProjectSettings.cpp
    src/PlatformSetupComponent.cpp
    src/ScreenAnchor.cpp
    src/SetupComponent.cpp
    src/assets/AssetManager.cpp
    src/assets/AssetLookupTable.cpp
    src/assets/AssetPackReader.cpp
)

# Generated component sources (.gen.cpp files)
# These contain Deserialize() for ALL builds and GetProperties()/GetComponentMeta() for DEKI_EDITOR
# The reflection code inside is wrapped with #ifdef DEKI_EDITOR
file(GLOB GENERATED_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/include/deki/generated/*.gen.cpp"
)

# The merged reflection TU (every reflected core type in one file) is written by the
# codegen step invoked FURTHER DOWN in this same file, so the glob above cannot see it
# on a fresh tree - the glob runs first, add_library() runs next, and the generator only
# runs after both. Declaring it explicitly as GENERATED is what lets add_library() accept
# a path this configure is about to produce, instead of failing with "Cannot find source
# file" and then "No SOURCES given to target". The build-time ordering is already
# guaranteed by add_dependencies(deki-engine-core generate_properties) below.
set(_CORE_REFLECTION_TU "${CMAKE_CURRENT_SOURCE_DIR}/include/deki/generated/CoreReflection0.gen.cpp")
set_source_files_properties("${_CORE_REFLECTION_TU}" PROPERTIES GENERATED TRUE)
list(APPEND GENERATED_SOURCES "${_CORE_REFLECTION_TU}")
list(REMOVE_DUPLICATES GENERATED_SOURCES)

list(APPEND DEKI_CORE_SOURCES ${GENERATED_SOURCES})
message(STATUS "Generated sources: ${GENERATED_SOURCES}")

# Editor-specific sources
if(DEKI_EDITOR)
    # The editor preview display is the only editor-side source in this DLL.
    # Editor application code (asset pack editor, engine editor registration)
    # lives in the editor repo (src/engine_editors/): it uses the deki-editor
    # DLL and ImGui, which this library must not depend on.
    # Note: CoreComponentRegistration.cpp is replaced by auto-generated CoreRegistration.gen.cpp
    list(APPEND DEKI_CORE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/platforms/editor/EditorDisplay.cpp"
    )
endif()

# Platform providers. The prefix routing only; the implementations behind each
# mount live in the package for that target (deki-desktop-integration,
# deki-esp32-integration), because the engine does not know what targets exist.
list(APPEND DEKI_CORE_SOURCES
    src/providers/FileSystem.cpp
)

# Build as shared library for editor (so plugins can link against it)
# Build as static library for embedded targets
if(DEKI_EDITOR)
    add_library(deki-engine-core SHARED ${DEKI_CORE_SOURCES})
    message(STATUS "Building deki-engine-core as SHARED library for EDITOR")
else()
    add_library(deki-engine-core STATIC ${DEKI_CORE_SOURCES})
    message(STATUS "Building deki-engine-core as STATIC library")
endif()

# The engine version, from project() above so there is one source of truth.
# PUBLIC: the editor compares it against each package's package.json minEngine
# and refuses to install a package that needs a newer engine than it embeds.
# The number equals the editor's (deki-editor/tools/ci/check_version.py
# enforces it). A breaking change bumps the MINOR across editor, engine and
# every package together; a change to a class packages subclass also bumps
# kDekiReflectionAbiVersion (deki-editor/tools/ci/check_abi.py enforces that).
# See deki-editor/COMPATIBILITY.md.
#
# DEKI_OLDEST_SUPPORTED_PACKAGE is the other half of that comparison: the
# oldest package generation this engine can still build. Releases are in
# lockstep, so a package's minEngine IS the engine it was built against, and
# the editor refuses a package whose minEngine is older than this.
#
# Without it minEngine is a floor with nothing above it, and a package from an
# older generation installs happily, fails the ABI handshake at load, gets
# rebuilt from source, and fails to compile. The user then sees a compile error
# deep in a project build rather than a sentence naming the package and the
# version, which is exactly what the minEngine check exists to avoid.
#
# Bump it to the release version whenever a release breaks packages, which is
# any release that bumps kDekiReflectionAbiVersion or changes what packages
# compile against. Leave it alone for a release that does not.
target_compile_definitions(deki-engine-core PUBLIC
    "DEKI_ENGINE_VERSION=\"${PROJECT_VERSION}\""
    "DEKI_OLDEST_SUPPORTED_PACKAGE=\"0.19.0\"")

# The transform width, PUBLIC so everything compiled against this target sees
# the same Deki::Object layout the engine itself was built with.
if(_DEKI_TRANSFORM_DEFINES)
    target_compile_definitions(deki-engine-core PUBLIC ${_DEKI_TRANSFORM_DEFINES})
endif()
message(STATUS "deki-engine-core transform: 2D=${DEKI_TRANSFORM_2D} 3D=${DEKI_TRANSFORM_3D}")

# The public headers: include/deki/ is what packages and projects compile
# against (`#include <deki/Object.h>`), the contract COMPATIBILITY.md
# describes. src/ holds the implementation and a few engine-internal headers
# (LifecycleManager, MissingComponent, AuthoredFormat, the provider registry)
# that the editor's own binaries reach through deki-engine-internal below and
# nothing else can.
target_include_directories(deki-engine-core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# For the editor executable, the editor DLL and the tests only: the engine's
# internal headers. A package must not link this.
add_library(deki-engine-internal INTERFACE)
target_include_directories(deki-engine-internal INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(deki-engine-internal INTERFACE deki-engine-core)

# =============================================================================
# Code Generator (C++26 static-reflection metadata + package registration)
# =============================================================================
# Only run for editor builds - runtime uses pre-serialized data
if(DEKI_EDITOR)
    # Output directory for generated files
    set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/deki/generated")

    # C++26 static-reflection generator (needs a GCC 16 with -freflection at
    # DEKI_GXX16; the 16.0.1 snapshot in the Ubuntu toolchain PPA has it). It emits
    # the engine-core metadata + CoreRegistration + aggregation files. The generator
    # runs via a plain g++ call, independent of the project's own compiler.
    if(NOT DEKI_GXX16)
        # Prefer the msys64 GCC 16 install before any older g++ on PATH.
        find_program(DEKI_GXX16 NAMES g++-16 g++ PATHS "C:/msys64/mingw64/bin" NO_DEFAULT_PATH)
        if(NOT DEKI_GXX16)
            find_program(DEKI_GXX16 NAMES g++-16 g++)
        endif()
        if(NOT DEKI_GXX16)
            message(FATAL_ERROR "Deki reflection codegen needs GCC 16 with C++26 reflection (-freflection); pass -DDEKI_GXX16=<path to g++>")
        endif()
    endif()
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/DekiReflectionCodegen.cmake)
    set(_RC_INCS "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
    file(GLOB _RC_MODDIRS LIST_DIRECTORIES true "${CMAKE_CURRENT_SOURCE_DIR}/../../packages/*")
    foreach(_d ${_RC_MODDIRS})
        if(IS_DIRECTORY "${_d}")
            list(APPEND _RC_INCS "${_d}")
        endif()
    endforeach()
    deki_reflection_codegen(
        CORE_BUILD
        UNIT_PREFIX "core"
        PACKAGE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include" PACKAGE_TAGS "deki-engine" PACKAGE_PREFIXES "Core"
        PACKAGE_OUTDIRS "${GENERATED_DIR}"
        GENERATOR_SRC "${CMAKE_CURRENT_SOURCE_DIR}/tools/reflection_codegen.cpp" GXX "${DEKI_GXX16}"
        INCLUDE_DIRS ${_RC_INCS}
        DEFINES DEKI_EDITOR DEKI_ENGINE_EXPORTS DEKI_LOG_ENABLED ${_DEKI_TRANSFORM_DEFINES}
                "DEKI_ENABLE_TRANSPARENCY=true" "DEKI_FAST_ATTR=")
    # The codegen's own build-time target, so building any one target that
    # links the engine regenerates after a reflected header changes, not only
    # a build of ALL.
    add_custom_target(generate_properties)
    add_dependencies(generate_properties ${DEKI_RC_TARGETS_deki_engine})

    # Make engine depend on generated code
    add_dependencies(deki-engine-core generate_properties)
endif()

# Compile definitions based on target
if(DEKI_EDITOR)
    # Export symbols for DLL
    target_compile_definitions(deki-engine-core PRIVATE DEKI_ENGINE_EXPORTS)
    target_compile_definitions(deki-engine-core PUBLIC DEKI_EDITOR)
    target_compile_definitions(deki-engine-core PUBLIC DEKI_LOG_ENABLED)

    # What the engine has to be told about the target it is built for; here
    # that target is the editor itself. No package defines: the engine tests
    # none, and naming packages is not its business.
    target_compile_definitions(deki-engine-core PUBLIC
        "DEKI_ENABLE_TRANSPARENCY=true"

        "DEKI_FAST_ATTR="
    )

    # Editor-specific includes (for component editors that need editor headers)
    # These are passed from the parent CMakeLists (deki-editor)
    if(DEKI_EDITOR_INCLUDE_DIR)
        target_include_directories(deki-engine-core PRIVATE ${DEKI_EDITOR_INCLUDE_DIR})
    endif()
    if(DEKI_IMGUI_INCLUDE_DIR)
        target_include_directories(deki-engine-core PRIVATE ${DEKI_IMGUI_INCLUDE_DIR})
    endif()
    # deki-editor DLL headers (for AssetDatabase, IEditorTool, etc.)
    if(DEKI_EDITOR_DLL_INCLUDE_DIR)
        target_include_directories(deki-engine-core PUBLIC ${DEKI_EDITOR_DLL_INCLUDE_DIR})
    endif()

    # OpenGL, for src/platforms/editor/EditorDisplay.cpp (the only GL user in
    # this DLL). PRIVATE:
    # nothing downstream needs GL through us.
    find_package(OpenGL REQUIRED)
    target_link_libraries(deki-engine-core PRIVATE OpenGL::GL)

    # No SDL3 link. This used to be PUBLIC SDL3::SDL3-static "needed by
    # EditorTimeProvider in Engine.cpp" - there is no EditorTimeProvider in
    # this repo (it lives in the editor app) and src/ contains no SDL header or
    # symbol, only the word in a few comments. It was pushing a statically
    # linked SDL3 onto every consumer of deki-engine-core.

    # ImGui, for the editor tool sources. imgui is defined by the parent
    # CMakeLists (deki-editor).
    target_link_libraries(deki-engine-core PRIVATE imgui)

    # Link deki-profiling-hooks for deep profiling (/Gh /GH hooks)
    # Note: /Gh /GH should NOT be enabled on deki-engine-core - it crashes during DLL init
    # Deep profiling is only safe for game plugins (they load after initialization)
    target_link_libraries(deki-engine-core PUBLIC deki-profiling-hooks)

    # Windows DLL settings
    #
    # KNOWN ISSUE, worth fixing before the API is considered stable: exporting
    # everything makes all 6363 symbols in this DLL public API on the day it is
    # tagged, including registry internals, TU-locals and anything else that
    # happens to be non-static. It is also why the DekiReflectionAbi handshake
    # has to exist - packages reach into DekiObject's layout through inline
    # accessors because there is no narrow ABI to reach through instead.
    #
    # The fix is to mark the intended surface with DEKI_ENGINE_API (see
    # src/DekiExport.h, already present and used by DekiEngine/Scene) and drop
    # both the property and the MinGW link option below. That is a large
    # mechanical pass over ~105 types plus whatever the editor and the 23
    # packages actually depend on, and a half-finished version links but fails
    # at load - so it wants to be done in one deliberate change, not folded into
    # an unrelated one.
    if(WIN32)
        set_target_properties(deki-engine-core PROPERTIES
            WINDOWS_EXPORT_ALL_SYMBOLS ON
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        )
        # WINDOWS_EXPORT_ALL_SYMBOLS is a no-op for GNU ld. MinGW normally
        # auto-exports everything, but the handful of explicit
        # __declspec(dllexport) symbols in the engine headers switch that
        # default off - leaving Scene/Guid/msgpack unresolved for consumers.
        # --export-all-symbols restores the intent of the property above.
        if(MINGW)
            target_link_options(deki-engine-core PRIVATE -Wl,--export-all-symbols)
        endif()
    endif()

else()
    # A game build. The engine does not know which target this is, and sets
    # nothing for it: the screen size, colour format, DEKI_FAST_ATTR and the
    # rest come from whoever adds this directory - the build backend of the
    # target, which has the platform in hand - through
    # target_compile_definitions(deki-engine-core PUBLIC ...).
    #
    # This used to be `if(DEFINED SIMULATOR)`, with the desktop simulator's
    # 320x240 RGB565 and three packages' defines written here. Engine.cpp
    # refuses to compile without the values it needs (see the #error there),
    # so a backend that forgets them finds out at once.
endif()
