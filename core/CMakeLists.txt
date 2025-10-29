cmake_minimum_required(VERSION 3.10)

project(droidstar_core)

# Add all source files
file(GLOB_RECURSE SOURCES
    "*.cpp"
    "*.c"
    "*.cc"
    "codec2/*.cpp"
    "imbe_vocoder/*.cc"
    "mbe/*.c"
    "mbe/*.cpp"
)

# Add include directories
include_directories(
    .
    codec2
    imbe_vocoder
    mbe
)

add_library(
    droidstar_core
    SHARED
    ${SOURCES}
)

# Link against Android libraries
find_library(log-lib log)
target_link_libraries(
    droidstar_core
    ${log-lib}
)
