#pragma once

namespace Papyrus {
    
    // Registration function
    bool RegisterFunctions(RE::BSScript::IVirtualMachine* vm);
    
    // ===== Hitstop Settings =====
    bool GetEnableHitstop(RE::StaticFunctionTag*);
    void SetEnableHitstop(RE::StaticFunctionTag*, bool value);
    float GetHitstopSpeed(RE::StaticFunctionTag*);
    void SetHitstopSpeed(RE::StaticFunctionTag*, float value);
    float GetHitstopDuration(RE::StaticFunctionTag*);
    void SetHitstopDuration(RE::StaticFunctionTag*, float value);
    
    // ===== Stagger Settings =====
    bool GetEnableStagger(RE::StaticFunctionTag*);
    void SetEnableStagger(RE::StaticFunctionTag*, bool value);
    float GetStaggerMagnitude(RE::StaticFunctionTag*);
    void SetStaggerMagnitude(RE::StaticFunctionTag*, float value);
    
    // ===== Camera Shake Settings =====
    bool GetEnableCameraShake(RE::StaticFunctionTag*);
    void SetEnableCameraShake(RE::StaticFunctionTag*, bool value);
    float GetCameraShakeStrength(RE::StaticFunctionTag*);
    void SetCameraShakeStrength(RE::StaticFunctionTag*, float value);
    float GetCameraShakeDuration(RE::StaticFunctionTag*);
    void SetCameraShakeDuration(RE::StaticFunctionTag*, float value);
    
    // ===== Sound Settings =====
    bool GetEnableSound(RE::StaticFunctionTag*);
    void SetEnableSound(RE::StaticFunctionTag*, bool value);
    std::string GetSoundPath(RE::StaticFunctionTag*);
    void SetSoundPath(RE::StaticFunctionTag*, std::string value);
    
    // ===== Original Settings (for compatibility) =====
    std::string GetBlockKey(RE::StaticFunctionTag*);
    void SetBlockKey(RE::StaticFunctionTag*, std::string value);
    float GetStaggerDistance(RE::StaticFunctionTag*);
    void SetStaggerDistance(RE::StaticFunctionTag*, float value);
    bool GetPerkLockedBlock(RE::StaticFunctionTag*);
    void SetPerkLockedBlock(RE::StaticFunctionTag*, bool value);
    bool GetOnlyWithShield(RE::StaticFunctionTag*);
    void SetOnlyWithShield(RE::StaticFunctionTag*, bool value);
    bool GetPerkLockedStagger(RE::StaticFunctionTag*);
    void SetPerkLockedStagger(RE::StaticFunctionTag*, bool value);
    
    // ===== Settings Management =====
    void SaveSettings(RE::StaticFunctionTag*);
    void ReloadSettings(RE::StaticFunctionTag*);
    
}  // namespace Papyrus
