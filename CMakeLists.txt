cmake_minimum_required(VERSION 3.21)

project(TimedBlockDodgeCounter VERSION 1.0.0 LANGUAGES CXX)

# Output folder configuration
if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_FOLDER}/Data")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    set(OUTPUT_FOLDER "$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

# Setup SKSE plugin
find_package(CommonLibSSE CONFIG REQUIRED)
add_commonlibsse_plugin(${PROJECT_NAME} SOURCES
    src/main.cpp
    src/Settings.cpp
    src/TimedBlockAddon.cpp
    src/Papyrus.cpp
    src/Menu.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Release>:/DEBUG>)
endif()

# Game deploy: DLL/PDB/INI under Compile/SKSE/Plugins (and mirrored Release/SKSE/Plugins)
set(TBDC_PLUGIN_DIR "${CMAKE_SOURCE_DIR}/Compile/SKSE/Plugins")
set(TBDC_RELEASE_DIR "${CMAKE_SOURCE_DIR}/Release/SKSE/Plugins")

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TBDC_PLUGIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${TBDC_PLUGIN_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${TBDC_PLUGIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${TBDC_PLUGIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${TBDC_PLUGIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${TBDC_PLUGIN_DIR}"
)

if(DEFINED OUTPUT_FOLDER)
    set(DLL_FOLDER "${OUTPUT_FOLDER}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET "${PROJECT_NAME}"
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${DLL_FOLDER}"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_custom_command(
            TARGET "${PROJECT_NAME}"
            POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endif()

# Always mirror build outputs + default INI into Compile/ and Release/ for manual installs
set(TBDC_INI_SRC "${CMAKE_SOURCE_DIR}/TimedBlockDodgeCounter.ini")
add_custom_command(
    TARGET "${PROJECT_NAME}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TBDC_PLUGIN_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${TBDC_RELEASE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${TBDC_RELEASE_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TBDC_INI_SRC}" "${TBDC_PLUGIN_DIR}/TimedBlockDodgeCounter.ini"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TBDC_INI_SRC}" "${TBDC_RELEASE_DIR}/TimedBlockDodgeCounter.ini"
    VERBATIM
)

add_custom_command(
    TARGET "${PROJECT_NAME}"
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${TBDC_PLUGIN_DIR}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${TBDC_RELEASE_DIR}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
    VERBATIM
)
