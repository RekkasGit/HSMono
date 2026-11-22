# Define the separate project/target
project(HSMono LANGUAGES C CXX)

add_library(HSMono SHARED
    src/hsmono.cpp
    # ... other source files
)

# Paths here are relative to plugins/hsmono/
target_include_directories(HSMono PRIVATE "${CMAKE_SOURCE_DIR}/sdk")
target_include_directories(HSMono SYSTEM PRIVATE mono/include/mono-2.0)


set_target_properties(HSMono PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${HAILSTONE_TREE_DIR}/plugins$<0:>
                                            RUNTIME_OUTPUT_DIRECTORY ${HAILSTONE_TREE_DIR}/plugins$<0:>)

install(TARGETS HSMono LIBRARY DESTINATION plugins COMPONENT examples EXCLUDE_FROM_ALL) # a MODULE is a LIBRARY

# Link against parent targets if needed
# target_link_libraries(HSMono PRIVATE MQ2Main)