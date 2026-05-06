# src/common/CMakeLists.txt
# Common utilities library

add_library(micmap_common STATIC
    src/logger.cpp
    src/cli_flags.cpp
    # P8 LIB-04 / D-19 / D-21: composition-root logger sinks. ILogSink
    # interface in include/micmap/common/log_sink.hpp; MultiSinkLogger
    # fans out to a vector of sinks under a single mutex.
    src/sinks/file_log_sink.cpp
    src/sinks/stdout_log_sink.cpp
    src/multi_sink_logger.cpp
)

target_include_directories(micmap_common
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(micmap_common PUBLIC cxx_std_17)

# Add alias for consistent naming
add_library(micmap::common ALIAS micmap_common)