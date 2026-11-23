# Define the separate project/target
project(HSMono LANGUAGES C CXX)

add_library(HSMono SHARED
    src/hsmono.cpp
    src/hsmono_imgui.cpp
    src/app_domains.cpp
    src/mono_loader.cpp
    src/mono_runtime.cpp
)

# Dear ImGui: the scripts draw into the client's own ImGui context (host->imgui_context), which needs this plugin to
# compile the very same Dear ImGui with the same settings (docs/plugins.md). So it takes the client's checkout and the
# client's imgui target's definitions, and compiles the core alone (no backends: the client renders).
if(NOT DEFINED imgui_SOURCE_DIR OR NOT TARGET imgui)
    message(FATAL_ERROR "HSMono needs the client's Dear ImGui (build it from the client's CMakeLists.txt)")
endif()
target_sources(HSMono PRIVATE
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
target_include_directories(HSMono SYSTEM PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/misc/cpp)
target_compile_definitions(HSMono PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_COMPILE_DEFINITIONS>)
# This copy's symbols stay inside the plugin: ImGui::* must neither take over the client's nor be taken over by them
# (Linux and macOS; Windows exports nothing unmarked). The plugin's own exports are marked (HAILSTONE_API).
set_target_properties(HSMono PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)

# Mono is loaded at run time (mono_runtime.h), so no Mono library is linked. dlopen needs libdl on older glibc;
# CMAKE_DL_LIBS is empty where it is not needed (Windows, macOS, glibc 2.34+).
target_link_libraries(HSMono PRIVATE ${CMAKE_DL_LIBS})

# Paths here are relative to plugins/hsmono/
target_include_directories(HSMono PRIVATE "${CMAKE_SOURCE_DIR}/sdk")
target_include_directories(HSMono SYSTEM PRIVATE mono/include/mono-2.0)


set_target_properties(HSMono PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY ${HAILSTONE_TREE_DIR}/plugins/hsmono$<0:>
                                            RUNTIME_OUTPUT_DIRECTORY ${HAILSTONE_TREE_DIR}/plugins/hsmono/$<0:>)

install(TARGETS HSMono LIBRARY DESTINATION plugins COMPONENT examples EXCLUDE_FROM_ALL) # a MODULE is a LIBRARY

# Link against parent targets if needed
# target_link_libraries(HSMono PRIVATE MQ2Main)