# Portable macOS build: fmt, spdlog and (when asked) yaml-cpp as static libraries built from pinned source.
#
# A plugin dylib has to load on a Mac that has only the system libraries. It cannot link Homebrew's shared libfmt, libspdlog and
# libyaml-cpp: those are not on a player's Mac, and their own dependencies point into /opt/homebrew. This file builds the same versions
# Homebrew ships as static libraries, with the plugin's deployment target, and defines the targets the plugin's CMakeLists links to
# (fmt::fmt, spdlog::spdlog, yaml-cpp::yaml-cpp), so it links them exactly as it links the find_package ones.
#
# Use (the same file sits in Codeware, ArchiveXL and TweakXL), after project() and after the plugin's add_compile_options:
#   set(CP2077_PORTABLE_YAML_CPP ON)            # only when the plugin uses yaml-cpp
#   include("${CMAKE_CURRENT_LIST_DIR}/PortableDeps.cmake")
#
# Sources: the official repositories, cloned by FetchContent at the tags below into <build dir>/_deps. The commit of each tag is checked
# after the fetch, so a moved tag fails the configure. From a local clone instead (offline): -DFETCHCONTENT_SOURCE_DIR_FMT=<dir>
# (likewise _SPDLOG, _YAML-CPP); that clone is checked against the same commit.
#
# What the libraries are built with, to match the Homebrew builds they replace: spdlog compiled (SPDLOG_COMPILED_LIB) on the external
# fmt (SPDLOG_FMT_EXTERNAL), no tests, examples, tools or install rules. They are compiled with hidden symbol visibility, so the
# plugin dylib exports none of their symbols.

include(FetchContent)
find_package(Git REQUIRED)

# Pins: tag and the commit it must resolve to.
set(CP2077_FMT_REPO "https://github.com/fmtlib/fmt.git")
set(CP2077_FMT_TAG "12.2.0")
set(CP2077_FMT_COMMIT "1be298e1bd68957e4cd352e1f676f00e07dcfb57")
set(CP2077_SPDLOG_REPO "https://github.com/gabime/spdlog.git")
set(CP2077_SPDLOG_TAG "v1.17.0")
set(CP2077_SPDLOG_COMMIT "79524ddd08a4ec981b7fea76afd08ee05f83755d")
set(CP2077_YAML_CPP_REPO "https://github.com/jbeder/yaml-cpp.git")
set(CP2077_YAML_CPP_TAG "yaml-cpp-0.9.0")
set(CP2077_YAML_CPP_COMMIT "56e3bb550c91fd7005566f19c079cb7a503223cf")

# Cache entries with FORCE, so the settings hold whatever policy the library's own CMakeLists runs under.
set(BUILD_SHARED_LIBS OFF)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
set(FMT_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(fmt GIT_REPOSITORY "${CP2077_FMT_REPO}" GIT_TAG "${CP2077_FMT_TAG}" GIT_SHALLOW ON GIT_PROGRESS OFF)
FetchContent_Declare(spdlog GIT_REPOSITORY "${CP2077_SPDLOG_REPO}" GIT_TAG "${CP2077_SPDLOG_TAG}" GIT_SHALLOW ON GIT_PROGRESS OFF)
set(_cp2077_deps fmt spdlog)
if(CP2077_PORTABLE_YAML_CPP)
    FetchContent_Declare(yaml-cpp GIT_REPOSITORY "${CP2077_YAML_CPP_REPO}" GIT_TAG "${CP2077_YAML_CPP_TAG}" GIT_SHALLOW ON GIT_PROGRESS OFF)
    list(APPEND _cp2077_deps yaml-cpp)
endif()

FetchContent_MakeAvailable(${_cp2077_deps})

# The commit of every dependency has to be the pinned one.
foreach(_dep IN LISTS _cp2077_deps)
    string(TOUPPER "${_dep}" _up)
    string(REPLACE "-" "_" _up "${_up}")
    FetchContent_GetProperties(${_dep} SOURCE_DIR _src)
    if(NOT EXISTS "${_src}/.git")
        message(WARNING "PortableDeps: ${_src} is not a git checkout; the ${_dep} version is not verified")
        continue()
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${_src}" rev-parse HEAD
                    OUTPUT_VARIABLE _head OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0 OR NOT _head STREQUAL "${CP2077_${_up}_COMMIT}")
        message(FATAL_ERROR "PortableDeps: ${_dep} in ${_src} is at '${_head}', not ${CP2077_${_up}_TAG} (${CP2077_${_up}_COMMIT})")
    endif()
    message(STATUS "PortableDeps: ${_dep} ${CP2077_${_up}_TAG} (${_head}), static")

    set_target_properties(${_dep} PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    # The Release flags of the Homebrew builds, also when the plugin itself is built without a build type.
    if(NOT CMAKE_BUILD_TYPE)
        target_compile_options(${_dep} PRIVATE -O2)
        target_compile_definitions(${_dep} PRIVATE NDEBUG)
    endif()
endforeach()

foreach(_t fmt::fmt spdlog::spdlog)
    if(NOT TARGET ${_t})
        message(FATAL_ERROR "PortableDeps: target ${_t} was not defined")
    endif()
endforeach()
if(CP2077_PORTABLE_YAML_CPP AND NOT TARGET yaml-cpp::yaml-cpp)
    message(FATAL_ERROR "PortableDeps: target yaml-cpp::yaml-cpp was not defined")
endif()
