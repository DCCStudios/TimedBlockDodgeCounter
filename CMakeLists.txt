cmake_minimum_required(VERSION 3.21)

project(TimedBlockDodgeCounter VERSION 1.0.4 LANGUAGES CXX)
# 1.0.4: switches the regular parry-window Precision PreHit branch from
#   `bIgnoreHit = true` to `PreHitModifier{Damage × 0}`.
#
#   v1.0.3 used bIgnoreHit, which cancels the entire hit at the Havok
#   level — no vanilla block animation, no block sound, no stamina drain,
#   no kHitBlocked flag, no TESHitEvent. To the player it just looks like
#   the swing missed. Players without Shield-of-Stamina-style mods take
#   damage from vanilla blocks, so the user wanted timed blocks to look
#   like real blocks but cost 0 HP.
#
#   The new modifier path leaves bIgnoreHit = false, so the engine plays
#   the block animation, the block sound, drains stamina the vanilla way,
#   sets kHitBlocked, and fires TESHitEvent — but multiplies the incoming
#   damage by 0 so the player still takes 0 HP. The slowmo/counter
#   effects are then fired by the existing TESHitEvent handler on the
#   kHitBlocked event, with no double-trigger: the TESHitEvent HP-restore
#   sees damageTaken ≈ 0 and no-ops.
#
#   The ward branch keeps bIgnoreHit = true (wards are intended to
#   absorb the hit completely; the ward visual is the feedback).
#
# 1.0.3: adds Precision PreHit handling for the parry window. Superseded.
#
# 1.0.2: removes the Actor::DealDamage entry hook entirely.
#   v1.0.0 patched a call-site INSIDE DealDamage at hardcoded offset
#   RELOCATION_OFFSET(0x3c0, 0x4A8) — broken on every non-1.6.640 runtime.
#   v1.0.1 replaced that with a function-entry detour (write_branch<5>) on
#   Actor::DealDamage. Address-Library based, runtime-version independent.
#   But: other SKSE parry/combat plugins (DualWieldParryingNG etc.) also
#   install entry hooks on the same function. When two write_branch<5>
#   hooks chain at the same entry, CommonLibSSE-NG's saved-prologue copy
#   in the trampoline contains a JMP whose rel32 is relative to the
#   ORIGINAL entry, not relative to our trampoline page. Calling _original
#   then jumped to an unmapped page and crashed with
#   EXCEPTION_ACCESS_VIOLATION ("Tried to execute memory at 0x7FF5B...").
#
#   v1.0.2 has no entry hook at all. The same three behaviours
#   (parry-window HP restore, real-damage cooldown, hit-during-dodge
#   trigger) are reconstructed from TESHitEvent + a per-frame health
#   snapshot taken at the end of PlayerCharacter::UpdateAnimation. No
#   patched bytes anywhere in DealDamage → cannot collide with any other
#   plugin's hook chain.
#
# 1.0.1: replaced 1.0.0's hardcoded in-function call-site patch with an
#   address-library function-entry detour. Superseded by 1.0.2 above.

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
