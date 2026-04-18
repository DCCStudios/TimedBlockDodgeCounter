#include "Papyrus.h"
#include "Settings.h"

namespace Papyrus {

    // Papyrus script name for external scripts to call these functions.
    // "_MCM" suffix is legacy; the in-game menu uses SKSEMenuFramework, not MCM Helper.
    constexpr std::string_view SCRIPT_NAME = "TimedBlockDodgeCounter_MCM";

    // ===== Hitstop Settings =====
    
    bool GetEnableHitstop(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bEnableHitstop;
    }
    
    void SetEnableHitstop(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bEnableHitstop = value;
        logger::debug("Papyrus: Set bEnableHitstop = {}", value);
    }
    
    float GetHitstopSpeed(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->fHitstopSpeed;
    }
    
    void SetHitstopSpeed(RE::StaticFunctionTag*, float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        Settings::GetSingleton()->fHitstopSpeed = value;
        logger::debug("Papyrus: Set fHitstopSpeed = {}", value);
    }
    
    float GetHitstopDuration(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->fHitstopDuration;
    }
    
    void SetHitstopDuration(RE::StaticFunctionTag*, float value) {
        value = std::clamp(value, 0.01f, 2.0f);
        Settings::GetSingleton()->fHitstopDuration = value;
        logger::debug("Papyrus: Set fHitstopDuration = {}", value);
    }
    
    // ===== Stagger Settings =====
    
    bool GetEnableStagger(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bEnableStagger;
    }
    
    void SetEnableStagger(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bEnableStagger = value;
        logger::debug("Papyrus: Set bEnableStagger = {}", value);
    }
    
    float GetStaggerMagnitude(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->fStaggerMagnitude;
    }
    
    void SetStaggerMagnitude(RE::StaticFunctionTag*, float value) {
        value = std::clamp(value, 0.0f, 2.0f);
        Settings::GetSingleton()->fStaggerMagnitude = value;
        logger::debug("Papyrus: Set fStaggerMagnitude = {}", value);
    }
    
    // ===== Camera Shake Settings =====
    
    bool GetEnableCameraShake(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bEnableCameraShake;
    }
    
    void SetEnableCameraShake(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bEnableCameraShake = value;
        logger::debug("Papyrus: Set bEnableCameraShake = {}", value);
    }
    
    float GetCameraShakeStrength(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->fCameraShakeStrength;
    }
    
    void SetCameraShakeStrength(RE::StaticFunctionTag*, float value) {
        value = std::clamp(value, 0.0f, 5.0f);
        Settings::GetSingleton()->fCameraShakeStrength = value;
        logger::debug("Papyrus: Set fCameraShakeStrength = {}", value);
    }
    
    float GetCameraShakeDuration(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->fCameraShakeDuration;
    }
    
    void SetCameraShakeDuration(RE::StaticFunctionTag*, float value) {
        value = std::clamp(value, 0.01f, 2.0f);
        Settings::GetSingleton()->fCameraShakeDuration = value;
        logger::debug("Papyrus: Set fCameraShakeDuration = {}", value);
    }
    
    // ===== Sound Settings =====
    
    bool GetEnableSound(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bEnableSound;
    }
    
    void SetEnableSound(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bEnableSound = value;
        logger::debug("Papyrus: Set bEnableSound = {}", value);
    }
    
    std::string GetSoundPath(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->sSoundPath;
    }
    
    void SetSoundPath(RE::StaticFunctionTag*, std::string value) {
        Settings::GetSingleton()->sSoundPath = value;
        logger::debug("Papyrus: Set sSoundPath = {}", value);
    }
    
    // ===== Original Settings (for compatibility) =====
    
    std::string GetBlockKey(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->sBlockKey;
    }
    
    void SetBlockKey(RE::StaticFunctionTag*, std::string value) {
        Settings::GetSingleton()->sBlockKey = value;
        logger::debug("Papyrus: Set sBlockKey = {}", value);
    }
    
    float GetStaggerDistance(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->fStaggerDistance;
    }
    
    void SetStaggerDistance(RE::StaticFunctionTag*, float value) {
        value = std::clamp(value, 0.0f, 1000.0f);
        Settings::GetSingleton()->fStaggerDistance = value;
        logger::debug("Papyrus: Set fStaggerDistance = {}", value);
    }
    
    bool GetPerkLockedBlock(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bPerkLockedBlock;
    }
    
    void SetPerkLockedBlock(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bPerkLockedBlock = value;
        logger::debug("Papyrus: Set bPerkLockedBlock = {}", value);
    }
    
    bool GetOnlyWithShield(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bOnlyWithShield;
    }
    
    void SetOnlyWithShield(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bOnlyWithShield = value;
        logger::debug("Papyrus: Set bOnlyWithShield = {}", value);
    }
    
    bool GetPerkLockedStagger(RE::StaticFunctionTag*) {
        return Settings::GetSingleton()->bPerkLockedStagger;
    }
    
    void SetPerkLockedStagger(RE::StaticFunctionTag*, bool value) {
        Settings::GetSingleton()->bPerkLockedStagger = value;
        logger::debug("Papyrus: Set bPerkLockedStagger = {}", value);
    }
    
    // ===== Settings Management =====
    
    void SaveSettings(RE::StaticFunctionTag*) {
        Settings::GetSingleton()->SaveSettings();
        logger::info("Papyrus: Settings saved to INI");
    }
    
    void ReloadSettings(RE::StaticFunctionTag*) {
        Settings::GetSingleton()->LoadSettings();
        logger::info("Papyrus: Settings reloaded from INI");
    }
    
    // ===== Registration =====
    
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm) {
        if (!vm) {
            logger::error("Failed to register Papyrus functions: VM is null");
            return false;
        }
        
        // Hitstop
        vm->RegisterFunction("GetEnableHitstop", SCRIPT_NAME, GetEnableHitstop);
        vm->RegisterFunction("SetEnableHitstop", SCRIPT_NAME, SetEnableHitstop);
        vm->RegisterFunction("GetHitstopSpeed", SCRIPT_NAME, GetHitstopSpeed);
        vm->RegisterFunction("SetHitstopSpeed", SCRIPT_NAME, SetHitstopSpeed);
        vm->RegisterFunction("GetHitstopDuration", SCRIPT_NAME, GetHitstopDuration);
        vm->RegisterFunction("SetHitstopDuration", SCRIPT_NAME, SetHitstopDuration);
        
        // Stagger
        vm->RegisterFunction("GetEnableStagger", SCRIPT_NAME, GetEnableStagger);
        vm->RegisterFunction("SetEnableStagger", SCRIPT_NAME, SetEnableStagger);
        vm->RegisterFunction("GetStaggerMagnitude", SCRIPT_NAME, GetStaggerMagnitude);
        vm->RegisterFunction("SetStaggerMagnitude", SCRIPT_NAME, SetStaggerMagnitude);
        
        // Camera Shake
        vm->RegisterFunction("GetEnableCameraShake", SCRIPT_NAME, GetEnableCameraShake);
        vm->RegisterFunction("SetEnableCameraShake", SCRIPT_NAME, SetEnableCameraShake);
        vm->RegisterFunction("GetCameraShakeStrength", SCRIPT_NAME, GetCameraShakeStrength);
        vm->RegisterFunction("SetCameraShakeStrength", SCRIPT_NAME, SetCameraShakeStrength);
        vm->RegisterFunction("GetCameraShakeDuration", SCRIPT_NAME, GetCameraShakeDuration);
        vm->RegisterFunction("SetCameraShakeDuration", SCRIPT_NAME, SetCameraShakeDuration);
        
        // Sound
        vm->RegisterFunction("GetEnableSound", SCRIPT_NAME, GetEnableSound);
        vm->RegisterFunction("SetEnableSound", SCRIPT_NAME, SetEnableSound);
        vm->RegisterFunction("GetSoundPath", SCRIPT_NAME, GetSoundPath);
        vm->RegisterFunction("SetSoundPath", SCRIPT_NAME, SetSoundPath);
        
        // Original settings
        vm->RegisterFunction("GetBlockKey", SCRIPT_NAME, GetBlockKey);
        vm->RegisterFunction("SetBlockKey", SCRIPT_NAME, SetBlockKey);
        vm->RegisterFunction("GetStaggerDistance", SCRIPT_NAME, GetStaggerDistance);
        vm->RegisterFunction("SetStaggerDistance", SCRIPT_NAME, SetStaggerDistance);
        vm->RegisterFunction("GetPerkLockedBlock", SCRIPT_NAME, GetPerkLockedBlock);
        vm->RegisterFunction("SetPerkLockedBlock", SCRIPT_NAME, SetPerkLockedBlock);
        vm->RegisterFunction("GetOnlyWithShield", SCRIPT_NAME, GetOnlyWithShield);
        vm->RegisterFunction("SetOnlyWithShield", SCRIPT_NAME, SetOnlyWithShield);
        vm->RegisterFunction("GetPerkLockedStagger", SCRIPT_NAME, GetPerkLockedStagger);
        vm->RegisterFunction("SetPerkLockedStagger", SCRIPT_NAME, SetPerkLockedStagger);
        
        // Settings management
        vm->RegisterFunction("SaveSettings", SCRIPT_NAME, SaveSettings);
        vm->RegisterFunction("ReloadSettings", SCRIPT_NAME, ReloadSettings);
        
        logger::info("Registered Papyrus functions under '{}'", SCRIPT_NAME);
        return true;
    }

}  // namespace Papyrus
