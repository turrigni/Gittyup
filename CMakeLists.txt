cmake_minimum_required(VERSION 3.19)

project(Gittyup)

# --------------------------------------------------
# Version
# --------------------------------------------------

set(GITTYUP_NAME "Gittyup")
set(GITTYUP_EXECUTABLE_NAME "gittyup")
set(GITTYUP_IDENTIFIER "com.github.Murmele.Gittyup")
set(GITTYUP_ORGANIZATION_DOMAIN "Murmele.github.com")

set(GITTYUP_VERSION_MAJOR 2)
set(GITTYUP_VERSION_MINOR 0)
set(GITTYUP_VERSION_PATCH 0)

set(GITTYUP_VERSION
    "${GITTYUP_VERSION_MAJOR}.${GITTYUP_VERSION_MINOR}.${GITTYUP_VERSION_PATCH}"
)

string(TIMESTAMP CURR_YEAR "%Y")
add_compile_definitions(CURR_YEAR=${CURR_YEAR})

configure_file(
    ${CMAKE_SOURCE_DIR}/LICENSE.md.in
    ${CMAKE_SOURCE_DIR}/LICENSE.md
    @ONLY
    NEWLINE_STYLE UNIX
)

file(WRITE "${CMAKE_BINARY_DIR}/Version.txt" ${GITTYUP_VERSION})

# --------------------------------------------------
# Build info
# --------------------------------------------------

set(
    DEV_BUILD
    ""
    CACHE STRING
    "Mark this build as a development build"
)

if(DEV_BUILD)
    set(BUILD_DESCRIPTION " (development build: ${DEV_BUILD})")
else()
    set(BUILD_DESCRIPTION "")
endif()

string(TIMESTAMP GITTYUP_BUILD_DATE)

find_package(Git)

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE}
                --git-dir=${CMAKE_SOURCE_DIR}/.git
                show -s --format=%h HEAD
        OUTPUT_VARIABLE GITTYUP_BUILD_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GITTYUP_BUILD_REVISION "unknown")
endif()

# --------------------------------------------------
# General options
# --------------------------------------------------

set(BUILD_SHARED_LIBS OFF)

option(FLATPAK "Building for flatpak" OFF)
option(DEBUG_FLATPAK "Debug Flatpak" OFF)
option(ENABLE_UPDATE_OVER_GUI "Enable Updates" ON)
option(ENABLE_TESTS "Enable Unit Tests" ON)
option(ENABLE_SHA256 "Enable SHA256 Support" ON)

# Conan handles these libraries
set(USE_SYSTEM_LIBSSH2 ON)
set(USE_SYSTEM_LIBGIT2 OFF)
set(USE_SYSTEM_LUA OFF)
set(USE_SYSTEM_HUNSPELL OFF)
set(USE_SYSTEM_QT OFF)

# --------------------------------------------------
# Language standards
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_STANDARD 99)

# --------------------------------------------------
# Linker flags
# --------------------------------------------------

if(HAIKU)
    set(CMAKE_EXE_LINKER_FLAGS -lgnu)
elseif(UNIX AND NOT APPLE)
    set(CMAKE_EXE_LINKER_FLAGS
        "${CMAKE_EXE_LINKER_FLAGS} -ldl")
endif()

# --------------------------------------------------
# Qt
# --------------------------------------------------

set(QT_MODULES
    Concurrent
    Core
    Core5Compat
    Gui
    Network
    Widgets
    Test
)

if(UNIX)
    list(APPEND QT_MODULES DBus)
endif()

find_package(
    Qt6 6.6 REQUIRED
    COMPONENTS
        ${QT_MODULES}
        LinguistTools
)

# --------------------------------------------------
# Conan packages
# --------------------------------------------------

find_package(libssh2 REQUIRED CONFIG)

find_package(hunspell REQUIRED CONFIG)

find_package(lua REQUIRED CONFIG)

# --------------------------------------------------
# Debug options
# --------------------------------------------------

option(DEBUG_OUTPUT "Enable debug output" ON)
option(DEBUG_OUTPUT_GENERAL "General debug output" ON)
option(DEBUG_OUTPUT_REFRESH "Refresh debug output" OFF)

if(NOT DEBUG_OUTPUT)
    add_compile_definitions(QT_NO_DEBUG_OUTPUT)
else()

    if(DEBUG_OUTPUT_GENERAL)
        add_compile_definitions(DEBUG_OUTPUT_GENERAL)
    endif()

    if(DEBUG_OUTPUT_REFRESH)
        add_compile_definitions(DEBUG_OUTPUT_REFRESH)
    endif()

endif()

if(FLATPAK)
    add_compile_definitions(FLATPAK)
endif()

if(DEBUG_FLATPAK)
    add_compile_definitions(DEBUG_FLATPAK)
endif()

if(ENABLE_UPDATE_OVER_GUI)
    add_compile_definitions(ENABLE_UPDATE_OVER_GUI)
endif()

if(ENABLE_SHA256)
    add_compile_definitions(GIT_EXPERIMENTAL_SHA256)
endif()

# --------------------------------------------------
# Install paths
# --------------------------------------------------

include(GNUInstallDirs)

if(APPLE)

    set(CONTENTS_DIR ${GITTYUP_EXECUTABLE_NAME}.app/Contents)
    set(RESOURCES_DIR ${CONTENTS_DIR}/Resources)
    set(L10N_INSTALL_DIR ${RESOURCES_DIR}/l10n)

elseif(UNIX)

    set(L10N_INSTALL_DIR
        ${CMAKE_INSTALL_LOCALEDIR}/${GITTYUP_NAME})

    set(RESOURCES_DIR
        ${CMAKE_INSTALL_DATADIR}/${GITTYUP_NAME})

else()

    set(L10N_INSTALL_DIR Resources/l10n)
    set(RESOURCES_DIR Resources)

endif()

# --------------------------------------------------
# Subprojects
# --------------------------------------------------

add_subdirectory(dep)
add_subdirectory(src)
add_subdirectory(l10n)
# add_subdirectory(pack)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()