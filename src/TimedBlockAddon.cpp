#include "TimedBlockAddon.h"
#include "Settings.h"
#include <SKSE/API.h>
#include <SKSE/Events.h>
#include <filesystem>
#include <optional>
#include <fstream>
#include <thread>
#include <cmath>
#include <cctype>
#include <random>
#include <cstring>

// Windows headers for custom WAV playback
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// Undefine Windows PlaySound macro to avoid conflict with RE::PlaySound
#ifdef PlaySound
#undef PlaySound
#endif

// Alias for Windows PlaySound
enum class DebugCategory { kTimedBlock, kCounter, kWard, kDodge };

static void DebugNotify(DebugCategory cat, const char* msg)
{
    auto* s = Settings::GetSingleton();
    bool show = false;
    switch (cat) {
    case DebugCategory::kTimedBlock: show = s->bDebugScreenTimedBlock; break;
    case DebugCategory::kCounter:    show = s->bDebugScreenCounterAttack; break;
    case DebugCategory::kWard:       show = s->bDebugScreenWard; break;
    case DebugCategory::kDodge:      show = s->bDebugScreenDodge; break;
    }
    if (show) {
        RE::DebugNotification(msg);
    }
}

namespace
{
	// vcpkg CommonLibSSE headers omit TESDataHandler::AddFormToDataHandler; engine offsets match CommonLibSSE src.
	bool AddFormToDataHandler(RE::TESDataHandler* a_handler, RE::TESForm* a_form)
	{
		if (!a_handler || !a_form) {
			return false;
		}
		using func_t = bool(RE::TESDataHandler*, RE::TESForm*);
		REL::Relocation<func_t> func{ RELOCATION_ID(13597, 13693) };
		return func(a_handler, a_form);
	}

	bool ActorHasShieldEquipped(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		auto* obj = a_actor->GetEquippedObject(true);
		if (!obj) {
			return false;
		}
		auto* armo = obj->As<RE::TESObjectARMO>();
		return armo && armo->IsShield();
	}

	RE::FormID ParseHexFormID(std::string_view a_hex)
	{
		if (a_hex.empty()) {
			return 0;
		}
		std::string s(a_hex);
		if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
			s = s.substr(2);
		}
		try {
			return static_cast<RE::FormID>(std::stoul(s, nullptr, 16));
		} catch (...) {
			return 0;
		}
	}
}

//=============================================================================
// RNG Utility - Random number generation for chance-based mechanics
//=============================================================================
namespace RNG
{
    inline std::mt19937& GetEngine()
    {
        static std::random_device rd;
        static std::mt19937 engine(rd());
        return engine;
    }
    
    // Returns a random value between 0.0 and 100.0
    inline float GetRandom()
    {
        static std::uniform_real_distribution<float> dist(0.0f, 100.0f);
        return dist(GetEngine());
    }
}

//=============================================================================
// Skill-Based Stagger Chance
//=============================================================================

// Get the weapon skill (One-Handed or Two-Handed) based on equipped weapon
RE::ActorValue GetWeaponSkillForActor(RE::Actor* a_actor)
{
    if (!a_actor) {
        return RE::ActorValue::kOneHanded;  // Default
    }
    
    // Check right hand first (primary weapon)
    auto* equippedRight = a_actor->GetEquippedObject(false);
    if (equippedRight && equippedRight->IsWeapon()) {
        auto* weapon = equippedRight->As<RE::TESObjectWEAP>();
        if (weapon) {
            RE::WEAPON_TYPE weaponType = weapon->GetWeaponType();
            switch (weaponType) {
                case RE::WEAPON_TYPE::kTwoHandSword:
                case RE::WEAPON_TYPE::kTwoHandAxe:
                    return RE::ActorValue::kTwoHanded;
                case RE::WEAPON_TYPE::kBow:
                case RE::WEAPON_TYPE::kCrossbow:
                    return RE::ActorValue::kArchery;
                default:
                    return RE::ActorValue::kOneHanded;
            }
        }
    }
    
    // Check left hand (for dual wielding or if right hand empty)
    auto* equippedLeft = a_actor->GetEquippedObject(true);
    if (equippedLeft && equippedLeft->IsWeapon()) {
        auto* weapon = equippedLeft->As<RE::TESObjectWEAP>();
        if (weapon) {
            RE::WEAPON_TYPE weaponType = weapon->GetWeaponType();
            switch (weaponType) {
                case RE::WEAPON_TYPE::kTwoHandSword:
                case RE::WEAPON_TYPE::kTwoHandAxe:
                    return RE::ActorValue::kTwoHanded;
                default:
                    return RE::ActorValue::kOneHanded;
            }
        }
    }
    
    // Default to one-handed (unarmed uses this)
    return RE::ActorValue::kOneHanded;
}

// Calculate the stagger chance based on player skills
float CalculateStaggerChance(RE::Actor* a_player)
{
    auto* settings = Settings::GetSingleton();
    
    if (!settings->bUseStaggerChance) {
        return 100.0f;  // Guaranteed stagger if skill-based chance is disabled
    }
    
    if (!a_player) {
        return settings->fBaseStaggerChance;
    }
    
    auto* avOwner = a_player->AsActorValueOwner();
    if (!avOwner) {
        return settings->fBaseStaggerChance;
    }
    
    float totalSkill = 0.0f;
    int skillCount = 0;
    
    // Factor in Block skill if enabled
    if (settings->bStaggerUseBlockSkill) {
        float blockSkill = avOwner->GetActorValue(RE::ActorValue::kBlock);
        blockSkill = std::clamp(blockSkill, 0.0f, 100.0f);
        totalSkill += blockSkill;
        skillCount++;
    }
    
    // Factor in weapon skill if enabled
    if (settings->bStaggerUseWeaponSkill) {
        RE::ActorValue weaponAV = GetWeaponSkillForActor(a_player);
        float weaponSkill = avOwner->GetActorValue(weaponAV);
        weaponSkill = std::clamp(weaponSkill, 0.0f, 100.0f);
        totalSkill += weaponSkill;
        skillCount++;
    }
    
    // If no skills are being used, return base chance
    if (skillCount == 0) {
        return settings->fBaseStaggerChance;
    }
    
    // Average the skills
    float averageSkill = totalSkill / static_cast<float>(skillCount);
    float skillRatio = averageSkill / 100.0f;
    
    // Linear interpolation between base and max chance
    float chance = settings->fBaseStaggerChance + 
                   (settings->fMaxStaggerChance - settings->fBaseStaggerChance) * skillRatio;
    
    return std::clamp(chance, 0.0f, 100.0f);
}

// Roll for stagger success based on skill
bool RollStaggerSuccess(RE::Actor* a_player)
{
    auto* settings = Settings::GetSingleton();
    
    if (!settings->bUseStaggerChance) {
        return true;  // Always stagger if skill-based chance is disabled
    }
    
    float chance = CalculateStaggerChance(a_player);
    float roll = RNG::GetRandom();
    bool success = roll <= chance;
    
    if (settings->bDebugLogging) {
        logger::info("[STAGGER CHANCE] Skill-based roll: {:.1f}% chance, rolled {:.1f}, {}", 
            chance, roll, success ? "SUCCESS" : "FAILED");
        
        if (!success) {
            DebugNotify(DebugCategory::kTimedBlock, fmt::format("[TB] Stagger failed ({:.0f}%)", chance).c_str());
        }
    }
    
    return success;
}

//=============================================================================
// AnimSpeedManager Implementation - Per-Actor Animation Freezing
//=============================================================================

void AnimSpeedManager::SetAnimSpeed(RE::ActorHandle a_actorHandle, float a_speedMult, float a_duration)
{
    std::lock_guard<std::mutex> lock(_animSpeedsLock);
    auto it = _animSpeeds.find(a_actorHandle);
    if (it != _animSpeeds.end()) {
        it->second.speedMult = a_speedMult;
        it->second.duration = a_duration;
    } else {
        _animSpeeds.emplace(a_actorHandle, AnimSpeedData{ a_speedMult, a_duration });
    }
}

void AnimSpeedManager::RevertAnimSpeed(RE::ActorHandle a_actorHandle)
{
    std::lock_guard<std::mutex> lock(_animSpeedsLock);
    auto it = _animSpeeds.find(a_actorHandle);
    if (it != _animSpeeds.end()) {
        it->second.speedMult = 1.0f;
        it->second.duration = 0.0f;
    }
}

void AnimSpeedManager::RevertAllAnimSpeed()
{
    std::lock_guard<std::mutex> lock(_animSpeedsLock);
    _animSpeeds.clear();
}

void AnimSpeedManager::Update(RE::ActorHandle a_actorHandle, float& a_deltaTime)
{
    if (a_deltaTime <= 0.f) {
        return;
    }
    
    // Fast path: if no actors have modified speeds, skip the lock entirely
    // This is the common case during normal gameplay (no active hitstop effects)
    // Reading empty() is safe without lock - worst case we miss one frame which is imperceptible
    if (_animSpeeds.empty()) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(_animSpeedsLock);
    auto it = _animSpeeds.find(a_actorHandle);
    if (it != _animSpeeds.end()) {
        float newDuration = it->second.duration - a_deltaTime;
        float speedMult = it->second.speedMult;
        
        if (newDuration <= 0.f) {
            // Calculate partial frame for smooth transition out
            float mult = (a_deltaTime + newDuration) / a_deltaTime;
            a_deltaTime *= speedMult + ((1.f - speedMult) * (1.f - mult));
            _animSpeeds.erase(it);
        } else {
            it->second.duration = newDuration;
            // Apply speed multiplier
            a_deltaTime *= speedMult;
        }
    }
}

void AnimSpeedManager::Init()
{
    PlayerAnimationHook::Install();
    NPCAnimationHook::Install();
    logger::info("AnimSpeedManager initialized");
}

void AnimSpeedManager::PlayerAnimationHook::Install()
{
    REL::Relocation<std::uintptr_t> PlayerCharacterVtbl{ RE::VTABLE_PlayerCharacter[0] };
    _originalFunc = PlayerCharacterVtbl.write_vfunc(0x7D, PlayerCharacter_UpdateAnimation);
    logger::info("Installed PlayerAnimationHook");
}

void AnimSpeedManager::PlayerAnimationHook::PlayerCharacter_UpdateAnimation(RE::PlayerCharacter* a_this, float a_deltaTime)
{
    // Update cooldown state tracking for timed block
    CooldownState::Update();
    
    // Update counter attack window timer
    CounterAttackState::Update();

    WardTimedBlockState::Update();
    
    // Note: lunge position updates happen in the physics hook (after simulation)

    // Update counter slow time (check for timeout)
    CounterSlowTimeState::Update();
    
    // Update timed dodge state (slomo timer, i-frame health tracking, radial blur blending)
    TimedDodgeState::Update();
    
    AnimSpeedManager::Update(a_this->GetHandle(), a_deltaTime);
    _originalFunc(a_this, a_deltaTime);

    // 1.0.2: snapshot player HP at the END of UpdateAnimation, so the
    // TESHitEvent handler that fires later in the frame can compute the
    // exact HP delta produced by a single hit. This replaces the
    // before/after snapshot the removed CombatHit::Thunk used to do.
    if (a_this) {
        if (auto* avOwner = a_this->AsActorValueOwner()) {
            PlayerHealthState::lastFrameHealth = avOwner->GetActorValue(RE::ActorValue::kHealth);
            PlayerHealthState::primed = true;
        }
    }
}

void AnimSpeedManager::NPCAnimationHook::Install()
{
    auto& trampoline = SKSE::GetTrampoline();
    REL::Relocation<uintptr_t> hook{ RELOCATION_ID(40436, 41453) };  // RunOneActorAnimationUpdateJob
    _originalFunc = trampoline.write_call<5>(hook.address() + RELOCATION_OFFSET(0x74, 0x74), UpdateAnimationInternal);
    logger::info("Installed NPCAnimationHook");
}

void AnimSpeedManager::NPCAnimationHook::UpdateAnimationInternal(RE::Actor* a_this, float a_deltaTime)
{
    AnimSpeedManager::Update(a_this->GetHandle(), a_deltaTime);
    _originalFunc(a_this, a_deltaTime);
}

//=============================================================================
// CooldownState Implementation - Tracks parry window effect transitions
//=============================================================================

bool CooldownState::PlayerHasParryWindowEffect()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }
    
    // Get the parry window effect from the TimedBlockAddon
    auto* addon = TimedBlockAddon::GetSingleton();
    if (!addon) {
        return false;
    }
    
    return addon->ActorHasParryWindowEffect(player);
}

bool CooldownState::GetParryEffectCached()
{
    if (!parryEffectCachedThisFrame) {
        // The spell's engine duration is rounded up to 1 s (uint32_t), so the
        // MGEF may still be in the active effects list after the real window
        // has closed.  Gate on the chrono timer first.
        if (parryWindowTimerActive &&
            std::chrono::steady_clock::now() < parryWindowDeadline) {
            cachedParryEffectResult = PlayerHasParryWindowEffect();
        } else {
            cachedParryEffectResult = false;
        }
        parryEffectCachedThisFrame = true;
    }
    return cachedParryEffectResult;
}

bool CooldownState::GetNearbyEnemyCached()
{
    auto* settings = Settings::GetSingleton();
    if (!settings->bIgnoreCooldownOutsideRange || settings->fCooldownIgnoreDistance <= 0.0f) {
        return true;  // Feature disabled, assume enemies nearby
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now - lastEnemyCheckTime >= ENEMY_CHECK_INTERVAL) {
        lastEnemyCheckTime = now;
        
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* addon = TimedBlockAddon::GetSingleton();
        if (player && addon) {
            cachedHasNearbyEnemy = addon->HasNearbyEnemyInCombat(player, settings->fCooldownIgnoreDistance);
        }
    }
    return cachedHasNearbyEnemy;
}

void CooldownState::Update()
{
    auto* settings = Settings::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* addon = TimedBlockAddon::GetSingleton();

    blockedByActiveCooldown = false;
    parryEffectCachedThisFrame = false;

    if (player && addon && addon->HasParryFormsReady()) {
        const bool currentlyBlocking = Offsets::IsBlocking(player);
        if (currentlyBlocking && !wasBlockingLastFrame) {
            if (!WindowExclusion::IsBlocked() && !addon->ActorHasParryWindowEffect(player)) {
                addon->CastParryWindowSpell(player);
            }
        }
        wasBlockingLastFrame = currentlyBlocking;
    } else {
        wasBlockingLastFrame = false;
    }

    // Chrono-based parry window expiry: the spell's integer-second duration
    // can't express sub-second windows, so we force-dispel here.
    if (parryWindowTimerActive &&
        std::chrono::steady_clock::now() >= parryWindowDeadline) {
        parryWindowTimerActive = false;
        if (addon && addon->ActorHasParryWindowEffect(player)) {
            DispelParryWindowSpell();
            if (settings->bDebugLogging) {
                logger::info("[PARRY WINDOW] Chrono timer expired ({:.0f}ms) — spell dispelled",
                    settings->fParryWindowDurationMs);
            }
        }
    }

    if (!settings->bEnableCooldown) {
        inCooldown = false;
        blockedByActiveCooldown = false;
        bool hasParryEffectNow = GetParryEffectCached();
        hadParryEffectLastFrame = hasParryEffectNow;
        return;
    }

    bool hasParryEffectNow = GetParryEffectCached();
    
    // Use cached nearby enemy check (only checks every 100ms instead of every frame)
    bool shouldIgnoreCooldown = !GetNearbyEnemyCached();
    
    if (settings->bDebugLogging && hasParryEffectNow != hadParryEffectLastFrame) {
        logger::info("[COOLDOWN UPDATE] ParryEffect: {} -> {}, InCooldown: {}, IgnoreDueToDistance: {}", 
            hadParryEffectLastFrame, hasParryEffectNow, inCooldown, shouldIgnoreCooldown);
    }
    
    // Detect transition: had effect last frame, doesn't have it now = window ended
    if (hadParryEffectLastFrame && !hasParryEffectNow) {
        // Parry window just ended
        if (!timedBlockTriggeredThisWindow && !shouldIgnoreCooldown) {
            // No timed block was triggered during this window - start/restart cooldown
            // But ONLY if we're not ignoring cooldown due to distance
            StartCooldown("Parry window ended without timed block");
        } else if (timedBlockTriggeredThisWindow) {
            // Successful timed block - CLEAR the cooldown entirely to allow consecutive blocks
            inCooldown = false;
            if (settings->bDebugLogging) {
                logger::info("[COOLDOWN] Parry window ENDED with successful timed block - COOLDOWN CLEARED");
            }
        } else if (shouldIgnoreCooldown) {
            if (settings->bDebugLogging) {
                logger::info("[COOLDOWN] Parry window ENDED without timed block - but IGNORING (no enemies nearby)");
            }
        }
        
        // Reset for next window
        timedBlockTriggeredThisWindow = false;
    }
    
    // Detect new parry window starting
    if (!hadParryEffectLastFrame && hasParryEffectNow) {
        timedBlockTriggeredThisWindow = false;
        
        if (settings->bDebugLogging) {
            logger::info("[COOLDOWN] Parry window STARTED (cooldown active: {}, ignoring: {})", inCooldown, shouldIgnoreCooldown);
        }
        
        // If on cooldown AND not ignoring due to distance, immediately dispel and RESTART the cooldown timer
        if (IsOnCooldownInternal() && !shouldIgnoreCooldown) {
            auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                cooldownEndTime - std::chrono::steady_clock::now()).count();
            logger::info("[COOLDOWN] Blocked during cooldown ({}ms remaining) - RESTARTING cooldown", remainingMs);
            
            // Mark that this frame's block was blocked by cooldown
            blockedByActiveCooldown = true;
            
            // RESTART the cooldown timer (punish spam blocking)
            StartCooldown("Block during cooldown - RESTARTED");
            
            if (settings->bDebugLogging) {
                DebugNotify(DebugCategory::kTimedBlock, "[TB Debug] Block during cooldown - RESTART");
            }
            
            // Dispel the parry window effect
            DispelParryWindowSpell();
            hasParryEffectNow = false;
        } else if (IsOnCooldownInternal() && shouldIgnoreCooldown) {
            // On cooldown but ignoring - allow the block to proceed
            if (settings->bDebugLogging) {
                logger::info("[COOLDOWN] On cooldown but IGNORING (no enemies within {} units)", settings->fCooldownIgnoreDistance);
                DebugNotify(DebugCategory::kTimedBlock, "[TB Debug] Cooldown ignored (no enemies)");
            }
        }
    }
    
    hadParryEffectLastFrame = hasParryEffectNow;
}

void CooldownState::StartCooldown(const char* reason)
{
    auto* settings = Settings::GetSingleton();
    inCooldown = true;
    cooldownEndTime = std::chrono::steady_clock::now() + 
        std::chrono::milliseconds(static_cast<long long>(settings->fCooldownDurationMs));
    
    if (settings->bDebugLogging) {
        logger::info("[COOLDOWN] {} - Starting {}ms cooldown", reason, settings->fCooldownDurationMs);
        DebugNotify(DebugCategory::kTimedBlock, "[TB Debug] Cooldown STARTED");
    }
}

void CooldownState::OnTimedBlockTriggered()
{
    timedBlockTriggeredThisWindow = true;
    
    // CLEAR the cooldown on successful timed block to allow consecutive blocks
    inCooldown = false;
    blockedByActiveCooldown = false;
    
    auto* settings = Settings::GetSingleton();
    if (settings->bDebugLogging) {
        logger::info("[COOLDOWN] TIMED BLOCK SUCCESS! Cooldown CLEARED for consecutive blocks");
        DebugNotify(DebugCategory::kTimedBlock, "[TB Debug] TIMED BLOCK SUCCESS!");
    }
}

bool CooldownState::IsOnCooldownInternal()
{
    if (!inCooldown) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now >= cooldownEndTime) {
        inCooldown = false;
        return false;
    }
    
    return true;
}

bool CooldownState::IsOnCooldown()
{
    auto* settings = Settings::GetSingleton();
    
    // If we already blocked a parry window this frame, we're definitely on cooldown
    if (blockedByActiveCooldown) {
        if (settings->bDebugLogging) {
            logger::info("[COOLDOWN] IsOnCooldown(): YES (blockedByActiveCooldown flag set)");
        }
        return true;
    }
    
    if (!inCooldown) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    if (now >= cooldownEndTime) {
        inCooldown = false;
        
        if (settings->bDebugLogging) {
            logger::info("[COOLDOWN] Cooldown EXPIRED by timer");
            DebugNotify(DebugCategory::kTimedBlock, "[TB Debug] Cooldown EXPIRED");
        }
        return false;
    }
    
    // Calculate remaining time for logging (but don't spam)
    return true;
}


//=============================================================================
// Helper Functions
//=============================================================================

bool IsActorPowerAttacking(RE::Actor* actor, std::string* outReason = nullptr)
{
    if (!actor) {
        return false;
    }
    
    // Method 1: Check attack state (vanilla)
    auto attackState = actor->AsActorState()->GetAttackState();
    
    // Power attacks and bashes
    if (attackState == RE::ATTACK_STATE_ENUM::kBash) {
        if (outReason) *outReason = "Bash";
        return true;
    }
    
    // Method 2: Check vanilla behavior graph variables
    bool isPowerAttacking = false;
    actor->GetGraphVariableBool("bIsPowerAttacking", isPowerAttacking);
    if (isPowerAttacking) {
        if (outReason) *outReason = "bIsPowerAttacking";
        return true;
    }
    
    // Method 3: Check bIsFullBodyPowerAttack (vanilla full body power attacks)
    bool isFullBodyPower = false;
    actor->GetGraphVariableBool("bIsFullBodyPowerAttack", isFullBodyPower);
    if (isFullBodyPower) {
        if (outReason) *outReason = "bIsFullBodyPowerAttack";
        return true;
    }
    
    // Method 4: MCO - Check MCO_IsHeavyAttack behavior variable
    bool mcoHeavyAttack = false;
    if (actor->GetGraphVariableBool("MCO_IsHeavyAttack", mcoHeavyAttack) && mcoHeavyAttack) {
        if (outReason) *outReason = "MCO_IsHeavyAttack";
        return true;
    }
    
    // Method 5: MCO - Check MCO_PowerAttack behavior variable  
    bool mcoPowerAttack = false;
    if (actor->GetGraphVariableBool("MCO_PowerAttack", mcoPowerAttack) && mcoPowerAttack) {
        if (outReason) *outReason = "MCO_PowerAttack";
        return true;
    }
    
    // Method 6: MCO - Check MCO_AttackType (> 0 typically means power/heavy attack)
    int mcoAttackType = 0;
    if (actor->GetGraphVariableInt("MCO_AttackType", mcoAttackType) && mcoAttackType > 0) {
        if (outReason) *outReason = fmt::format("MCO_AttackType={}", mcoAttackType);
        return true;
    }
    
    // Method 7: SCAR - Check SCAR_PowerAttacking
    bool scarPowerAttack = false;
    if (actor->GetGraphVariableBool("SCAR_PowerAttacking", scarPowerAttack) && scarPowerAttack) {
        if (outReason) *outReason = "SCAR_PowerAttacking";
        return true;
    }
    
    // Method 8: SCAR - Check SCAR_IsHeavyAttack
    bool scarHeavyAttack = false;
    if (actor->GetGraphVariableBool("SCAR_IsHeavyAttack", scarHeavyAttack) && scarHeavyAttack) {
        if (outReason) *outReason = "SCAR_IsHeavyAttack";
        return true;
    }
    
    // Method 9: Check SkySA/ABR style - iState variable (3 = power attack)
    int iState = 0;
    if (actor->GetGraphVariableInt("iState", iState) && iState == 3) {
        if (outReason) *outReason = "iState=3 (SkySA/ABR)";
        return true;
    }
    
    // Method 10: Check vanilla attackPower variable
    float attackPowerFloat = 0.0f;
    actor->GetGraphVariableFloat("attackPower", attackPowerFloat);
    if (attackPowerFloat > 0.1f) {
        if (outReason) *outReason = fmt::format("attackPower={:.2f}", attackPowerFloat);
        return true;
    }
    
    // Method 11: Check staggerPower for heavy attacks (high stagger = power attack)
    float staggerPower = 0.0f;
    actor->GetGraphVariableFloat("staggerPower", staggerPower);
    if (staggerPower > 0.5f) {
        if (outReason) *outReason = fmt::format("staggerPower={:.2f}", staggerPower);
        return true;
    }
    
    // Method 12: Check iAttackState for certain values indicating power attacks
    int iAttackState = 0;
    actor->GetGraphVariableInt("iAttackState", iAttackState);
    // iAttackState of 2 or 3 often indicates power attacks in various frameworks
    if (iAttackState >= 2) {
        if (outReason) *outReason = fmt::format("iAttackState={}", iAttackState);
        return true;
    }
    
    // Method 13: Check for ADXP/MCO style heavy attack via IsHeavyAttack boolean
    bool isHeavyAttack = false;
    if (actor->GetGraphVariableBool("IsHeavyAttack", isHeavyAttack) && isHeavyAttack) {
        if (outReason) *outReason = "IsHeavyAttack";
        return true;
    }
    
    // Method 14: Check for bInJumpState combined with attacking (jump attacks are often power attacks)
    bool isJumpAttack = false;
    if (actor->GetGraphVariableBool("bInJumpState", isJumpAttack) && isJumpAttack) {
        bool isAttacking = false;
        actor->GetGraphVariableBool("IsAttacking", isAttacking);
        if (isAttacking) {
            if (outReason) *outReason = "JumpAttack";
            return true;
        }
    }
    
    return false;
}

//=============================================================================
// TimedBlockAddon Implementation
//=============================================================================

TimedBlockAddon* TimedBlockAddon::GetSingleton() {
    static TimedBlockAddon singleton;
    return &singleton;
}

void TimedBlockAddon::Initialize() {
    logger::info("Initializing TimedBlockAddon...");

    AnimSpeedManager::Init();

    LungePhysicsHook::Install();

    CombatHit::Install();

    logger::info("TimedBlockAddon initialized successfully");
}

void TimedBlockAddon::Register() {
    RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink(GetSingleton());
        logger::info("Registered TimedBlockAddon event sink");
    }
}

void TimedBlockAddon::UpdateParryWindowDuration()
{
    if (!mgef_parry_window) {
        return;
    }
    auto* settings = Settings::GetSingleton();
    const float durationSeconds = settings->fParryWindowDurationMs / 1000.0f;
    mgef_parry_window->data.taperDuration = durationSeconds;
    if (spell_parry_window && !spell_parry_window->effects.empty() && spell_parry_window->effects[0]) {
        // Engine duration is uint32_t (integer seconds).  We set it to a
        // generous safety value — the chrono timer + force-dispel in
        // CooldownState::Update() controls the real sub-second window.
        spell_parry_window->effects[0]->effectItem.duration = 5;
    }
}

bool TimedBlockAddon::CreateParryWindowForms()
{
    if (mgef_parry_window && spell_parry_window) {
        UpdateParryWindowDuration();
        return true;
    }

    auto* mgefFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::EffectSetting>();
    auto* spelFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
    if (!mgefFactory || !spelFactory) {
        logger::error("[PARRY WINDOW] Form factory unavailable");
        return false;
    }

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* settings = Settings::GetSingleton();

    if (!mgef_parry_window) {
        auto* mgefForm = mgefFactory->Create();
        mgef_parry_window = mgefForm ? mgefForm->As<RE::EffectSetting>() : nullptr;
        if (!mgef_parry_window) {
            logger::error("[PARRY WINDOW] Failed to allocate EffectSetting");
            return false;
        }

        mgef_parry_window->fullName = "TBDC Parry Window";
        mgef_parry_window->magicItemDescription = "Timed block parry window active.";

        using F = RE::EffectSetting::EffectSettingData::Flag;
        auto& md = mgef_parry_window->data;
        md.archetype = RE::EffectArchetypes::ArchetypeID::kDetectLife;
        md.flags.set(F::kHideInUI);
        md.flags.set(F::kNoHitEffect);
        md.flags.set(F::kNoArea);
        md.baseCost = 0.0f;
        md.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        md.delivery = RE::MagicSystem::Delivery::kSelf;
        md.primaryAV = RE::ActorValue::kNone;
        md.associatedSkill = RE::ActorValue::kNone;
        md.resistVariable = RE::ActorValue::kNone;

        if (dh && !AddFormToDataHandler(dh, mgef_parry_window)) {
            logger::warn("[PARRY WINDOW] AddFormToDataHandler failed for MGEF");
        }
        logger::info("[PARRY WINDOW] Runtime MGEF created (DetectLife marker)");
    }

    if (!spell_parry_window) {
        if (!mgef_parry_window) {
            return false;
        }
        auto* spelForm = spelFactory->Create();
        spell_parry_window = spelForm ? spelForm->As<RE::SpellItem>() : nullptr;
        if (!spell_parry_window) {
            logger::error("[PARRY WINDOW] Failed to allocate SpellItem");
            return false;
        }

        spell_parry_window->fullName = "TBDC Parry Window";
        spell_parry_window->data.spellType = RE::MagicSystem::SpellType::kSpell;
        spell_parry_window->data.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        spell_parry_window->data.delivery = RE::MagicSystem::Delivery::kSelf;
        spell_parry_window->data.chargeTime = 0.0f;
        spell_parry_window->data.castDuration = 0.0f;
        spell_parry_window->data.range = 0.0f;
        spell_parry_window->data.costOverride = 0;
        spell_parry_window->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);

        auto* eff = new RE::Effect();
        eff->baseEffect = mgef_parry_window;
        eff->effectItem.magnitude = 0.0f;
        eff->effectItem.area = 0;
        eff->cost = 0.0f;

        spell_parry_window->effects.push_back(eff);
        if (dh && !AddFormToDataHandler(dh, spell_parry_window)) {
            logger::warn("[PARRY WINDOW] AddFormToDataHandler failed for SPEL");
        }
        logger::info("[PARRY WINDOW] Runtime SPEL created");
    }

    UpdateParryWindowDuration();

    timedBlockExplosion = nullptr;
    if (settings->bEnableExplosion && dh) {
        const auto& pluginName = settings->sExplosionPluginName;
        const RE::FormID explosionId = ParseHexFormID(settings->sExplosionFormID);
        if (!pluginName.empty() && explosionId != 0) {
            timedBlockExplosion = dh->LookupForm<RE::BGSExplosion>(explosionId, pluginName.c_str());
            if (timedBlockExplosion) {
                logger::info("[PARRY WINDOW] Explosion loaded: {} from {}", timedBlockExplosion->GetName(), pluginName);
            } else {
                logger::warn("[PARRY WINDOW] Explosion 0x{:X} not found in {}", explosionId, pluginName);
            }
        } else if (settings->bEnableExplosion) {
            logger::info("[PARRY WINDOW] Explosion enabled but no plugin/formID configured in [Forms]");
        }
    }

    return mgef_parry_window != nullptr && spell_parry_window != nullptr;
}

void TimedBlockAddon::LoadPerkForms()
{
    damagePreventPerk = nullptr;
    staggerPerk = nullptr;

    auto* dh = RE::TESDataHandler::GetSingleton();
    auto* st = Settings::GetSingleton();
    if (!dh || !st) {
        return;
    }

    const auto& plugin = st->sFormsPerkPluginName;
    if (plugin.empty()) {
        return;
    }

    if (const RE::FormID dmgId = ParseHexFormID(st->sFormsDamagePreventPerkID)) {
        damagePreventPerk = skyrim_cast<RE::BGSPerk*>(dh->LookupForm(dmgId, plugin.c_str()));
        if (damagePreventPerk) {
            logger::info("[PERKS] Damage-prevent perk: {}", damagePreventPerk->GetName());
        }
    }
    if (const RE::FormID stgId = ParseHexFormID(st->sFormsStaggerPerkID)) {
        staggerPerk = skyrim_cast<RE::BGSPerk*>(dh->LookupForm(stgId, plugin.c_str()));
        if (staggerPerk) {
            logger::info("[PERKS] Stagger perk: {}", staggerPerk->GetName());
        }
    }
}

void TimedBlockAddon::CastParryWindowSpell(RE::Actor* target)
{
    if (!target || !spell_parry_window) {
        return;
    }
    if (ActorHasParryWindowEffect(target)) {
        return;
    }
    auto* caster = target->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
    if (caster) {
        caster->CastSpellImmediate(spell_parry_window, false, target, 1.0f, false, 0.0f, nullptr);

        auto* settings = Settings::GetSingleton();
        CooldownState::parryWindowTimerActive = true;
        CooldownState::parryWindowDeadline = std::chrono::steady_clock::now() +
            std::chrono::microseconds(static_cast<long long>(settings->fParryWindowDurationMs * 1000.0f));
    }
}

std::vector<RE::Actor*> TimedBlockAddon::GetNearbyActors(RE::Actor* center, float radius) const
{
    std::vector<RE::Actor*> result;
    if (!center || radius <= 0.0f) {
        return result;
    }
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return result;
    }
    const float r2 = radius * radius;
    const auto origin = center->GetPosition();
    result.reserve(processLists->numberHighActors);
    for (auto& h : processLists->highActorHandles) {
        auto* a = h.get().get();
        if (!a || a == center) {
            continue;
        }
        if (origin.GetSquaredDistance(a->GetPosition()) <= r2) {
            result.push_back(a);
        }
    }
    return result;
}

bool TimedBlockAddon::ActorHasParryWindowEffect(RE::Actor* actor) {
    if (!actor || !mgef_parry_window) {
        return false;
    }
    
    auto magicTarget = actor->AsMagicTarget();
    if (!magicTarget) {
        return false;
    }
    
    auto activeEffects = magicTarget->GetActiveEffectList();
    if (!activeEffects || activeEffects->empty()) {
        return false;
    }
    
    for (RE::ActiveEffect* effect : *activeEffects) {
        if (effect && !effect->flags.any(RE::ActiveEffect::Flag::kInactive)) {
            RE::EffectSetting* setting = effect->GetBaseObject();
            if (setting && setting == mgef_parry_window) {
                return true;
            }
        }
    }
    
    return false;
}

bool TimedBlockAddon::HasNearbyEnemyInCombat(RE::Actor* player, float radius) {
    if (!player) {
        return false;  // No player, no check needed
    }
    
    // OPTIMIZATION: Early exit if player is not in combat at all
    // This is the most common case during exploration - skip all actor iteration
    if (!player->IsInCombat()) {
        return false;
    }
    
    auto settings = Settings::GetSingleton();
    auto playerPos = player->GetPosition();
    
    // OPTIMIZATION: Use squared distance to avoid sqrt operations
    float radiusSq = radius * radius;
    
    // Get the process lists to iterate through high actors (nearby, loaded actors)
    auto processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        return true;  // Fail-safe: assume enemy nearby
    }
    
    // Check all high-process actors (actors near the player that are fully loaded)
    for (auto& actorHandle : processLists->highActorHandles) {
        auto actor = actorHandle.get().get();
        if (!actor || actor == player || actor->IsDead()) {
            continue;
        }
        
        // OPTIMIZATION: Check squared distance first (cheaper than combat checks)
        float distSq = playerPos.GetSquaredDistance(actor->GetPosition());
        if (distSq > radiusSq) {
            continue;
        }
        
        // OPTIMIZATION: Simple hostility + combat check instead of iterating combat targets
        // If actor is hostile to player AND in combat, it's likely fighting the player
        if (actor->IsHostileToActor(player) && actor->IsInCombat()) {
            if (settings->bDebugLogging) {
                logger::debug("[DISTANCE] ENEMY FOUND: '{}' at {:.0f} units (hostile + in combat)",
                    actor->GetName(), std::sqrt(distSq));
            }
            return true;
        }
    }
    
    if (settings->bDebugLogging) {
        logger::debug("[DISTANCE] No hostile enemies in combat within {} units", radius);
    }
    return false;
}

RE::BSEventNotifyControl TimedBlockAddon::ProcessEvent(
    const RE::TESHitEvent* a_event,
    RE::BSTEventSource<RE::TESHitEvent>*) 
{
    using Result = RE::BSEventNotifyControl;
    using HitFlag = RE::TESHitEvent::Flag;
    
    if (!a_event || !a_event->target) {
        return Result::kContinue;
    }
    
    RE::Actor* defender = a_event->target->As<RE::Actor>();
    if (!defender || !defender->IsPlayerRef()) {
        return Result::kContinue;  // Only process for player
    }
    
    // I-frame protection: restore health on any hit during timed dodge i-frames
    if (TimedDodgeState::iframesActive) {
        TimedDodgeState::OnPlayerHit(defender);
        return Result::kContinue;
    }

    // Hit-during-dodge: if the player is mid-dodge and gets hit, retroactively
    // trigger the timed dodge with this attacker (the hit proves the dodge was timed).
    // Also skip damage cooldown — hits during a dodge are absorbed by the dodge
    // mod's i-frames, not real damage the player failed to avoid.
    if (TimedDodgeState::inDodgeAnimation) {
        if (!TimedDodgeState::active) {
            RE::Actor* hitAttacker = a_event->cause ? a_event->cause->As<RE::Actor>() : nullptr;
            if (hitAttacker && hitAttacker != defender) {
                TimedDodgeState::TryTriggerFromHit(hitAttacker);
            }
        }
        return Result::kContinue;
    }

    // Melee hit-contact cooldown (not HP): Precision PreHit handles this when
    // Precision is registered; otherwise one TESHitEvent per melee hit approximates it.
    if (!a_event->projectile && !WardTimedBlockState::g_precisionAvailable) {
        TimedDodgeState::OnMeleeWeaponHitContact();
    }

    // 1.0.2: damage-cooldown detection — used to live in CombatHit::Thunk via a
    // before/after health snapshot wrapped around the original DealDamage call.
    // That hook caused trampoline conflicts with other parry plugins, so we now
    // reconstruct the same signal here from PlayerHealthState::lastFrameHealth
    // (snapshotted at the end of every PlayerCharacter::UpdateAnimation) minus
    // the current HP after the engine has already applied this hit's damage.
    auto computePlayerDamageTaken = [&]() -> float {
        if (!PlayerHealthState::primed) return 0.0f;
        auto* avOwner = defender->AsActorValueOwner();
        if (!avOwner) return 0.0f;
        const float currentHp = avOwner->GetActorValue(RE::ActorValue::kHealth);
        return PlayerHealthState::lastFrameHealth - currentHp;
    };

    // Skip projectiles for timed block / ward processing (both are melee-only),
    // BUT we still need to count any HP loss they caused as real damage so the
    // timed-dodge cooldown fires on arrows / spells / crossbow bolts the same
    // way it did under the old hook.
    if (a_event->projectile) {
        const float damageTaken = computePlayerDamageTaken();
        if (damageTaken > 0.5f) {
            auto* dmgSettings = Settings::GetSingleton();
            if (TimedDodgeState::pendingDodge) {
                TimedDodgeState::pendingDodge = false;
                if (dmgSettings && dmgSettings->bDebugLogging) {
                    logger::info("[TIMED DODGE] Pending dodge cancelled - player took {:.1f} ranged damage", damageTaken);
                }
            }
            TimedDodgeState::OnPlayerDamaged();
        }
        return Result::kContinue;
    }

    // Ward timed block: melee hit while player is casting a ward
    {
        auto* wardSettings = Settings::GetSingleton();
        if (wardSettings->bEnableWardTimedBlock && !WardTimedBlockState::IsOnCooldown()) {
            bool eventWindow = WardTimedBlockState::IsInWindow();
            bool castingWard = false;
            bool dualCast = WardTimedBlockState::isDualCast;

            if (!eventWindow) {
                // Real-time check: scan the player's active effects for any ward effect.
                // This works for concentration wards where MagicCaster::currentSpell is null
                // after the initial cast (the effect stays in the active effects list instead).
                auto* mt = defender->AsMagicTarget();
                auto* effectList = mt ? mt->GetActiveEffectList() : nullptr;
                if (effectList) {
                    bool foundLeft = false, foundRight = false;
                    for (auto* ae : *effectList) {
                        if (!ae) continue;
                        if (ae->flags.any(RE::ActiveEffect::Flag::kInactive) ||
                            ae->flags.any(RE::ActiveEffect::Flag::kDispelled)) continue;
                        auto* baseEff = ae->GetBaseObject();
                        if (!baseEff) continue;
                        // Fast keyword check: pointer if available, then string scan
                        bool isWard = (WardTimedBlockState::wardKeyword && baseEff->HasKeyword(WardTimedBlockState::wardKeyword))
                                   || baseEff->HasKeywordString("MagicWard");
                        if (!isWard) continue;
                        castingWard = true;
                        if (ae->castingSource == RE::MagicSystem::CastingSource::kLeftHand)  foundLeft  = true;
                        if (ae->castingSource == RE::MagicSystem::CastingSource::kRightHand) foundRight = true;
                    }
                    if (castingWard) dualCast = foundLeft && foundRight;
                }

                if (wardSettings->bDebugLogging) {
                    if (castingWard) {
                        logger::info("[WARD TB] Active-effect ward detected: dual={}", dualCast);
                    } else {
                        logger::info("[WARD TB] Melee hit on player — no ward active effect found (kw={})",
                            WardTimedBlockState::wardKeyword ? "ok" : "null");
                        DebugNotify(DebugCategory::kWard, "[WARD] Hit — no ward detected");
                    }
                }
            }

            // If the event-based window isn't open but we detected a ward via active effects,
            // route through OnWardActivated — it has the cooldown guard and single-open logic.
            // Never force inWindow directly; let OnWardActivated decide.
            if (!eventWindow && castingWard) {
                WardTimedBlockState::OnWardActivated(dualCast);
                eventWindow = WardTimedBlockState::IsInWindow();
            }

            if (eventWindow) {
                RE::Actor* wardAttacker = a_event->cause ? a_event->cause->As<RE::Actor>() : nullptr;
                if (wardAttacker) {
                    // Reject hits from actors outside melee weapon range.
                    // TESHitEvent fires for any hit source; without this, distant NPCs
                    // (e.g. an archer's "melee fallback" hit) would trigger ward parries.
                    const float wardRange = wardSettings->fWardMeleeDetectionRange;
                    const float kWardMeleeRangeSq = wardRange * wardRange;
                    const float distSq = defender->GetPosition().GetSquaredDistance(wardAttacker->GetPosition());
                    if (distSq > kWardMeleeRangeSq) {
                        if (wardSettings->bDebugLogging) {
                            logger::info("[WARD TB] Hit rejected — attacker too far ({:.0f} units)", std::sqrt(distSq));
                            DebugNotify(DebugCategory::kWard, "[WARD] Hit — attacker out of melee range");
                        }
                        // Fall through to normal hit processing
                    } else {
                        if (wardSettings->bDebugLogging) {
                            DebugNotify(DebugCategory::kWard, fmt::format("[WARD] Hit IN parry window ({:.0f}u)", std::sqrt(distSq)).c_str());
                        }
                        WardTimedBlockState::OnMeleeHit(defender, wardAttacker);
                        return Result::kContinue;
                    }
                }
            } else if (wardSettings->bDebugLogging) {
                DebugNotify(DebugCategory::kWard, "[WARD] Hit — NOT in parry window");
            }
        }
    }
    
    // 1.0.2: unblocked melee hit on the player → real damage. Used to be
    // handled by CombatHit::Thunk's targetIsPlayer + healthDrop>0.5 check.
    if (!a_event->flags.any(HitFlag::kHitBlocked)) {
        const float damageTaken = computePlayerDamageTaken();
        if (damageTaken > 0.5f) {
            auto* dmgSettings = Settings::GetSingleton();
            if (TimedDodgeState::pendingDodge) {
                TimedDodgeState::pendingDodge = false;
                if (dmgSettings && dmgSettings->bDebugLogging) {
                    logger::info("[TIMED DODGE] Pending dodge cancelled - player took {:.1f} real damage", damageTaken);
                }
            }
            TimedDodgeState::OnPlayerDamaged();
        }
        return Result::kContinue;
    }

    auto* settings = Settings::GetSingleton();

    // Get the attacker FIRST - we need it for distance check
    RE::Actor* attacker = a_event->cause ? a_event->cause->As<RE::Actor>() : nullptr;
    if (!attacker) {
        // Blocked hit with no attacker — count any HP loss as real damage so
        // the timed-dodge cooldown still fires (matches old Thunk behaviour).
        const float damageTaken = computePlayerDamageTaken();
        if (damageTaken > 0.5f) {
            TimedDodgeState::OnPlayerDamaged();
        }
        return Result::kContinue;
    }

    // Check if player has the parry window effect (indicating a timed block)
    // Use cached result if available (same frame), otherwise check fresh
    if (!CooldownState::GetParryEffectCached()) {
        // Blocked hit but no parry window — vanilla block reduction may still
        // have left some damage. Treat residual HP loss as real damage so the
        // timed-dodge cooldown behaves identically to the pre-1.0.2 build.
        const float damageTaken = computePlayerDamageTaken();
        if (damageTaken > 0.5f) {
            TimedDodgeState::OnPlayerDamaged();
        }
        return Result::kContinue;
    }

    if (settings->bOnlyWithShield && !ActorHasShieldEquipped(defender)) {
        return Result::kContinue;
    }

    if (settings->bDebugLogging) {
        logger::info("[HITEVENT] Blocked hit detected with parry window effect active!");
    }

    // 1.0.2: parry-window HP restore. Done UNCONDITIONALLY here (before the
    // cooldown / window-exclusion gates) to match v1.0.0 / v1.0.1 semantics:
    // the original CombatHit::Thunk's ShouldPreventDamage gate did NOT
    // consult addon cooldowns or window exclusion — damage prevention was
    // independent of whether the slowmo/sound effects fired. We mirror that
    // here so the cooldown-while-parrying corner case still spares HP.
    // Bonus parity: the bPerkLockedBlock check is honoured here too.
    {
        bool perkOk = true;
        if (settings->bPerkLockedBlock) {
            auto* perk = damagePreventPerk;
            perkOk = (perk != nullptr) && defender->HasPerk(perk);
        }
        if (perkOk) {
            const float damageTaken = computePlayerDamageTaken();
            if (damageTaken > 0.0f) {
                auto* avOwner = defender->AsActorValueOwner();
                if (avOwner) {
                    avOwner->RestoreActorValue(
                        RE::ACTOR_VALUE_MODIFIER::kDamage,
                        RE::ActorValue::kHealth,
                        damageTaken);
                    if (settings->bDebugLogging) {
                        logger::info("[TIMED BLOCK] Parry-window HP restored: +{:.1f}", damageTaken);
                    }
                }
            }
        }
    }

    // *** CHECK COOLDOWN FIRST - before any effects can be applied ***
    if (settings->bEnableCooldown) {
        // Use cached nearby enemy check (checked every 100ms, not every event)
        bool ignoreCooldownDueToDistance = !CooldownState::GetNearbyEnemyCached();
        if (ignoreCooldownDueToDistance && settings->bDebugLogging) {
            logger::debug("[COOLDOWN] No enemies in combat nearby - IGNORING cooldown");
        }
        
        if (!ignoreCooldownDueToDistance && CooldownState::IsOnCooldown()) {
            logger::info("[HITEVENT] Timed block HIT detected but on cooldown - SKIPPING ALL addon effects");
            if (settings->bDebugLogging) {
                DebugNotify(DebugCategory::kTimedBlock, "[TB Debug] WOULD HAVE been timed block (cooldown)");
            }
            return Result::kContinue;  // EXIT EARLY - no effects!
        }
    }
    
    // Mutual exclusion: skip if a ward timed block or timed dodge was triggered recently
    if (WindowExclusion::IsBlocked()) {
        if (settings->bDebugLogging) {
            logger::info("[HITEVENT] Timed block skipped — another window activated too recently");
        }
        return Result::kContinue;
    }

    // Mark that a timed block was triggered (for cooldown tracking)
    CooldownState::OnTimedBlockTriggered();
    WindowExclusion::Stamp();

    // (HP restore was performed earlier, before the cooldown / exclusion
    // gates, to preserve v1.0.0/v1.0.1 ShouldPreventDamage semantics.)

    // Start counter attack window (if enabled) - pass attacker for lunge targeting
    CounterAttackState::StartWindow(attacker);

    logger::info("[HITEVENT] TIMED BLOCK SUCCESS! Applying addon effects...");

    // Apply all effects
    ApplyTimedBlockEffects(defender, attacker);

    return Result::kContinue;
}

void TimedBlockAddon::ApplyTimedBlockEffects(RE::Actor* defender, RE::Actor* attacker, bool skipSlowmo, bool fromTimedDodge) {
    auto settings = Settings::GetSingleton();
    const bool allowStaggerByPerk = !settings->bPerkLockedStagger ||
        (staggerPerk != nullptr && defender && defender->HasPerk(staggerPerk));
    
    // 0. PREVENT player stagger - cancel any stagger/recoil animation on the defender
    // This ensures heavy attacks don't stagger the player on a successful timed block
    if (settings->bPreventPlayerStagger && defender) {
        // Cancel stagger graph variables (vanilla)
        defender->SetGraphVariableBool("IsStaggering", false);
        defender->SetGraphVariableBool("IsRecoiling", false);
        defender->SetGraphVariableBool("IsBlockHit", false);
        defender->SetGraphVariableFloat("staggerMagnitude", 0.0f);
        
        // MaxuBlockOverhaul compatibility
        defender->SetGraphVariableBool("Maxsu_IsBlockHit", false);
        defender->SetGraphVariableBool("bMaxsu_BlockHit", false);
        defender->SetGraphVariableFloat("Maxsu_BlockHitStrength", 0.0f);
        
        // Send animation events to interrupt stagger (vanilla)
        defender->NotifyAnimationGraph("staggerStop");
        defender->NotifyAnimationGraph("BlockHitEnd");
        
        // MaxuBlockOverhaul compatibility
        defender->NotifyAnimationGraph("Maxsu_BlockHitEnd");
        defender->NotifyAnimationGraph("Maxsu_BlockHitInterrupt");
        
        if (settings->bDebugLogging) {
            logger::info("[TIMED BLOCK] Cancelled player stagger/recoil (vanilla + MaxuBlockOverhaul)");
        }
    }
    
    // 1. Freeze attacker's animation (hitstop effect - skip for timed dodge, it has its own attacker slow)
    if (settings->bEnableHitstop && attacker && !fromTimedDodge) {
        float hitstopSpeed = settings->fHitstopSpeed;  // 0.0 = complete freeze, 0.1 = very slow
        float hitstopDuration = settings->fHitstopDuration;
        
        logger::debug("Applying hitstop to attacker: speed={}, duration={}s", hitstopSpeed, hitstopDuration);
        AnimSpeedManager::SetAnimSpeed(attacker->GetHandle(), hitstopSpeed, hitstopDuration);
    }
    
    // 2. Force attacker into stagger animation (with optional skill-based chance)
    // Skip stagger during timed dodge unless explicitly enabled
    if (settings->bEnableStagger && allowStaggerByPerk && attacker && defender && (!fromTimedDodge || settings->bTimedDodgeStagger)) {
        // Detect if attacker is performing a power attack (including MCO/SCAR)
        std::string powerAttackReason;
        bool isPowerAttack = IsActorPowerAttacking(attacker, &powerAttackReason);
        float staggerMagnitude = isPowerAttack ? settings->fPowerAttackStaggerMagnitude : settings->fStaggerMagnitude;
        
        if (settings->bDebugLogging) {
            if (isPowerAttack) {
                logger::info("[TIMED BLOCK] Blocked POWER ATTACK from '{}' (detected via: {}), stagger: {}", 
                    attacker->GetName(), powerAttackReason, staggerMagnitude);
                DebugNotify(DebugCategory::kTimedBlock, fmt::format("[TB] POWER ATTACK blocked ({})", powerAttackReason).c_str());
            } else {
                logger::info("[TIMED BLOCK] Blocked NORMAL ATTACK from '{}', stagger: {}", 
                    attacker->GetName(), staggerMagnitude);
                DebugNotify(DebugCategory::kTimedBlock, "[TB] Normal attack blocked");
            }
        } else {
            logger::debug("Triggering stagger on attacker with magnitude: {}", staggerMagnitude);
        }
        
        // Check skill-based stagger chance (if enabled)
        if (RollStaggerSuccess(defender)) {
            TriggerStagger(defender, attacker, staggerMagnitude);
        } else {
            logger::debug("Stagger skipped due to failed skill-based chance roll");
        }
    }

    if (settings->bEnableStagger && allowStaggerByPerk && defender && settings->fStaggerDistance > 0.0f &&
        (!fromTimedDodge || settings->bTimedDodgeStagger)) {
        for (auto* nearby : GetNearbyActors(defender, settings->fStaggerDistance)) {
            if (!nearby || nearby == attacker || !nearby->IsInCombat()) {
                continue;
            }
            if (RollStaggerSuccess(defender)) {
                TriggerStagger(defender, nearby, settings->fStaggerMagnitude);
            }
        }
    }
    
    // 3. Camera shake for impact feel
    if (settings->bEnableCameraShake && defender) {
        float shakeStrength = settings->fCameraShakeStrength;
        float shakeDuration = settings->fCameraShakeDuration;
        logger::debug("Applying camera shake: strength={}, duration={}s", shakeStrength, shakeDuration);
        ShakeCamera(shakeStrength, defender->GetPosition(), shakeDuration);
    }
    
    // 4. Restore stamina on successful timed block
    if (settings->bEnableStaminaRestore && defender) {
        auto* avOwner = defender->AsActorValueOwner();
        if (avOwner) {
            float maxStamina = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);
            float currentStamina = avOwner->GetActorValue(RE::ActorValue::kStamina);
            float restoreAmount = maxStamina * (settings->fStaminaRestorePercent / 100.0f);
            float actualRestore = (std::min)(restoreAmount, maxStamina - currentStamina);  // Don't over-fill
            
            if (actualRestore > 0.0f) {
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, actualRestore);
                
                if (settings->bDebugLogging) {
                    logger::info("[TIMED BLOCK] Restored {:.1f} stamina ({:.0f}% of max {:.1f})", 
                        actualRestore, settings->fStaminaRestorePercent, maxStamina);
                    DebugNotify(DebugCategory::kTimedBlock, fmt::format("[TB] +{:.0f} Stamina", actualRestore).c_str());
                }
            }
        }
    }
    
    // 5. Play sound effect (timed dodge has its own sound)
    if (settings->bEnableSound && !fromTimedDodge) {
        PlayTimedBlockSound();
    }
    
    // 6. Apply slowmo effect (slows entire world) - skip if timed dodge is handling slow-mo
    if (settings->bEnableSlowmo && !skipSlowmo && !TimedDodgeState::IsSlomoActive()) {
        ApplySlowmo(settings->fSlowmoSpeed, settings->fSlowmoDuration);
    }

    if (settings->bEnableExplosion && timedBlockExplosion && defender) {
        defender->PlaceObjectAtMe(timedBlockExplosion, false);
    }

    if (attacker && defender) {
        if (auto* modSrc = SKSE::GetModCallbackEventSource()) {
            const std::string attackerFormHex = fmt::format("{:08X}", attacker->GetFormID());
            SKSE::ModCallbackEvent evDef;
            evDef.eventName = RE::BSFixedString("STBL_OnTimedBlockDefender");
            evDef.strArg = RE::BSFixedString(attackerFormHex.c_str());
            evDef.numArg = static_cast<float>(attacker->GetLevel());
            evDef.sender = defender;
            modSrc->SendEvent(std::addressof(evDef));

            SKSE::ModCallbackEvent evAtk;
            evAtk.eventName = RE::BSFixedString("STBL_OnTimedBlockAttacker");
            evAtk.strArg = RE::BSFixedString("");
            evAtk.numArg = static_cast<float>(attacker->GetLevel());
            evAtk.sender = attacker;
            modSrc->SendEvent(std::addressof(evAtk));
        }
    }
}

void TimedBlockAddon::ApplyWardTimedBlockEffects(RE::Actor* defender, RE::Actor* attacker, bool isDualCastWard)
{
    auto* settings = Settings::GetSingleton();

    // Cancel the player's hit-react / stagger so the ward visually "stops" the blow.
    // We intentionally skip any block-specific graph events (BlockHitEnd etc.) since
    // the player is casting, not using a shield — only neutral stagger-cancel is needed.
    if (settings->bPreventPlayerStagger && defender) {
        defender->SetGraphVariableBool("IsStaggering", false);
        defender->SetGraphVariableBool("IsRecoiling", false);
        defender->SetGraphVariableFloat("staggerMagnitude", 0.0f);
        defender->NotifyAnimationGraph("staggerStop");
    }

    if (settings->bEnableHitstop && attacker) {
        AnimSpeedManager::SetAnimSpeed(attacker->GetHandle(), settings->fHitstopSpeed, settings->fHitstopDuration);
    }

    if (settings->bWardTimedBlockStagger && attacker && defender) {
        float staggerMag = isDualCastWard ? settings->fWardLargeStaggerMagnitude : settings->fWardSmallStaggerMagnitude;
        if (RollStaggerSuccess(defender)) {
            TriggerStagger(defender, attacker, staggerMag);
        }
    }

    if (settings->bEnableCameraShake && defender) {
        ShakeCamera(settings->fCameraShakeStrength, defender->GetPosition(), settings->fCameraShakeDuration);
    }

    if (settings->bEnableStaminaRestore && defender) {
        auto* avOwner = defender->AsActorValueOwner();
        if (avOwner) {
            float maxStamina = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);
            float currentStamina = avOwner->GetActorValue(RE::ActorValue::kStamina);
            float restoreAmount = maxStamina * (settings->fStaminaRestorePercent / 100.0f);
            float actualRestore = (std::min)(restoreAmount, maxStamina - currentStamina);
            if (actualRestore > 0.0f) {
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, actualRestore);
            }
        }
    }

    if (settings->bWardTimedBlockMagickaRestore && defender) {
        auto* avOwner = defender->AsActorValueOwner();
        if (avOwner) {
            const float maxMp = avOwner->GetPermanentActorValue(RE::ActorValue::kMagicka);
            const float curMp = avOwner->GetActorValue(RE::ActorValue::kMagicka);
            const float restore = maxMp * (settings->fWardMagickaRestorePercent / 100.0f);
            const float actual = (std::min)(restore, maxMp - curMp);
            if (actual > 0.0f) {
                avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kMagicka, actual);
                if (settings->bDebugLogging) {
                    logger::info("[WARD TB] Restored {:.1f} magicka ({:.0f}% of max {:.1f})",
                        actual, settings->fWardMagickaRestorePercent, maxMp);
                }
            }
        }
    }

    if (settings->bWardTimedBlockSound) {
        WardTimedBlockState::PlayWardTimedBlockSound();
    }

    if (settings->bEnableSlowmo && !TimedDodgeState::IsSlomoActive()) {
        ApplySlowmo(settings->fSlowmoSpeed, settings->fSlowmoDuration);
    }
}

void TimedBlockAddon::TriggerStagger(RE::Actor* defender, RE::Actor* attacker, float magnitude) {
    if (!defender || !attacker) {
        return;
    }
    
    // Don't stagger if swimming, in killmove, or not loaded
    if (attacker->AsActorState()->IsSwimming() || attacker->IsInKillMove() || !attacker->Is3DLoaded()) {
        return;
    }
    
    // Calculate stagger direction based on heading angle from attacker to defender
    // This makes the attacker stagger backwards (away from defender)
    RE::NiPoint3 defenderPos = defender->GetPosition();
    float headingAngle = attacker->GetHeadingAngle(defenderPos, false);
    float direction = (headingAngle >= 0.0f) ? headingAngle / 360.0f : (360.0f + headingAngle) / 360.0f;
    
    // Set stagger graph variables
    static const RE::BSFixedString staggerDirection("staggerDirection");
    static const RE::BSFixedString staggerMagnitude("StaggerMagnitude");
    static const RE::BSFixedString staggerStart("staggerStart");
    
    attacker->SetGraphVariableFloat(staggerDirection, direction);
    attacker->SetGraphVariableFloat(staggerMagnitude, magnitude);
    attacker->NotifyAnimationGraph(staggerStart);
    
    logger::debug("Triggered stagger: direction={}, magnitude={}", direction, magnitude);
}

void TimedBlockAddon::ShakeCamera(float strength, const RE::NiPoint3& position, float duration) {
    if (strength <= 0.0f || duration <= 0.0f) {
        return;
    }
    
    // Use the game's native camera shake function
    Offsets::ShakeCamera(strength, position, duration);
}

void TimedBlockAddon::PlayTimedBlockSound() {
    auto settings = Settings::GetSingleton();
    
    if (settings->bUseCustomWav) {
        // Play custom WAV file using Windows API
        PlayCustomWavSound();
    } else {
        // Play game sound descriptor
        if (settings->sSoundPath.empty()) {
            return;
        }
        
        logger::debug("Playing timed block sound: {}", settings->sSoundPath);
        
        // Play the sound on main thread
        SKSE::GetTaskInterface()->AddTask([soundPath = settings->sSoundPath]() {
            RE::PlaySound(soundPath.c_str());
            logger::debug("Played sound descriptor: {}", soundPath);
        });
    }
}

// WAV file header structures for volume adjustment
#pragma pack(push, 1)
struct WAVHeader {
    char     riff[4];        // "RIFF"
    uint32_t fileSize;       // File size - 8
    char     wave[4];        // "WAVE"
};

struct WAVChunkHeader {
    char     id[4];
    uint32_t size;
};

struct WAVFmtChunk {
    uint16_t audioFormat;    // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)

// Per-play WAV buffer kept alive via shared_ptr so SND_ASYNC reads valid memory.
// Each playback captures its own shared_ptr; the previous one is released when
// the next sound of the same type starts (or the shared_ptr naturally expires).
static std::shared_ptr<std::vector<uint8_t>> g_timedBlockAudioBuffer;
static std::shared_ptr<std::vector<uint8_t>> g_counterStrikeAudioBuffer;
static std::shared_ptr<std::vector<uint8_t>> g_timedDodgeAudioBuffer;
static std::shared_ptr<std::vector<uint8_t>> g_wardTimedBlockAudioBuffer;
static std::shared_ptr<std::vector<uint8_t>> g_wardCounterSpellAudioBuffer;

// Load WAV file into a NEW buffer and apply software volume adjustment.
// Caller keeps the returned shared_ptr alive for at least as long as SND_ASYNC needs it.
static std::shared_ptr<std::vector<uint8_t>> LoadWAVWithVolume(const std::filesystem::path& filePath, float volumeScale) {
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return nullptr;
    }
    
    std::streamsize fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    
    auto outBuffer = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(outBuffer->data()), fileSize)) {
        return nullptr;
    }
    file.close();
    
    if (outBuffer->size() < sizeof(WAVHeader)) {
        logger::error("File too small to be a valid WAV");
        return nullptr;
    }
    
    WAVHeader* header = reinterpret_cast<WAVHeader*>(outBuffer->data());
    if (std::memcmp(header->riff, "RIFF", 4) != 0 || std::memcmp(header->wave, "WAVE", 4) != 0) {
        logger::error("Not a valid WAV file");
        return nullptr;
    }
    
    // Find fmt and data chunks
    size_t pos = sizeof(WAVHeader);
    WAVFmtChunk* fmt = nullptr;
    uint8_t* audioData = nullptr;
    uint32_t audioDataSize = 0;
    
    while (pos + sizeof(WAVChunkHeader) <= outBuffer->size()) {
        WAVChunkHeader* chunk = reinterpret_cast<WAVChunkHeader*>(outBuffer->data() + pos);
        
        if (std::memcmp(chunk->id, "fmt ", 4) == 0) {
            fmt = reinterpret_cast<WAVFmtChunk*>(outBuffer->data() + pos + sizeof(WAVChunkHeader));
        } else if (std::memcmp(chunk->id, "data", 4) == 0) {
            audioData = outBuffer->data() + pos + sizeof(WAVChunkHeader);
            audioDataSize = chunk->size;
            break;
        }
        
        pos += sizeof(WAVChunkHeader) + chunk->size;
        if (pos % 2 != 0) pos++;
    }
    
    if (!fmt || !audioData || audioDataSize == 0) {
        logger::error("Could not find fmt or data chunk in WAV");
        return nullptr;
    }
    
    if (fmt->audioFormat != 1) {
        logger::warn("Non-PCM WAV format, volume adjustment skipped");
        return outBuffer;
    }
    
    if (volumeScale >= 0.99f) {
        return outBuffer;
    }
    
    if (fmt->bitsPerSample == 16) {
        int16_t* samples = reinterpret_cast<int16_t*>(audioData);
        size_t numSamples = audioDataSize / sizeof(int16_t);
        for (size_t i = 0; i < numSamples; ++i) {
            float sample = static_cast<float>(samples[i]) * volumeScale;
            samples[i] = static_cast<int16_t>(std::clamp(sample, -32768.0f, 32767.0f));
        }
    } else if (fmt->bitsPerSample == 8) {
        for (size_t i = 0; i < audioDataSize; ++i) {
            float sample = (static_cast<float>(audioData[i]) - 128.0f) * volumeScale;
            audioData[i] = static_cast<uint8_t>(std::clamp(sample + 128.0f, 0.0f, 255.0f));
        }
    } else if (fmt->bitsPerSample == 24) {
        size_t numSamples = audioDataSize / 3;
        for (size_t i = 0; i < numSamples; ++i) {
            int32_t sample = audioData[i * 3] | (audioData[i * 3 + 1] << 8) | (audioData[i * 3 + 2] << 16);
            if (sample & 0x800000) sample |= 0xFF000000;
            float adjusted = static_cast<float>(sample) * volumeScale;
            int32_t result = static_cast<int32_t>(std::clamp(adjusted, -8388608.0f, 8388607.0f));
            audioData[i * 3] = result & 0xFF;
            audioData[i * 3 + 1] = (result >> 8) & 0xFF;
            audioData[i * 3 + 2] = (result >> 16) & 0xFF;
        }
    } else if (fmt->bitsPerSample == 32) {
        int32_t* samples = reinterpret_cast<int32_t*>(audioData);
        size_t numSamples = audioDataSize / sizeof(int32_t);
        for (size_t i = 0; i < numSamples; ++i) {
            double sample = static_cast<double>(samples[i]) * volumeScale;
            samples[i] = static_cast<int32_t>(std::clamp(sample, -2147483648.0, 2147483647.0));
        }
    } else {
        logger::warn("Unsupported WAV bit depth: {}", fmt->bitsPerSample);
    }
    
    return outBuffer;
}

void TimedBlockAddon::PlayCustomWavSound() {
    // Build the path to the WAV file
    // Path: Data/SKSE/Plugins/TimedBlockDodgeCounter/timedblock.wav
    
    auto settings = Settings::GetSingleton();
    float volume = settings->fCustomWavVolume;
    
    SKSE::GetTaskInterface()->AddTask([volume]() {
        std::filesystem::path wavPath = std::filesystem::current_path();
        wavPath /= "Data";
        wavPath /= "SKSE";
        wavPath /= "Plugins";
        wavPath /= "TimedBlockDodgeCounter";
        wavPath /= "timedblock.wav";
        
        if (std::filesystem::exists(wavPath)) {
            auto buf = LoadWAVWithVolume(wavPath, volume);
            if (!buf) {
                logger::error("Failed to load WAV file: {}", wavPath.string());
                RE::PlaySound("UIMenuOK");
                return;
            }
            g_timedBlockAudioBuffer = buf;
            BOOL result = PlaySoundA(reinterpret_cast<LPCSTR>(buf->data()), 
                                     NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            if (!result) {
                logger::error("Failed to play WAV. Error: {}", GetLastError());
                RE::PlaySound("UIMenuOK");
            }
        } else {
            logger::warn("Custom WAV file not found: {}", wavPath.string());
            RE::PlaySound("UIMenuOK");
        }
    });
}

void PlayCounterStrikeSound() {
    // Build the path to the WAV file
    // Path: Data/SKSE/Plugins/TimedBlockDodgeCounter/counterstrike.wav
    
    auto settings = Settings::GetSingleton();
    if (!settings->bEnableCounterStrikeSound) {
        return;
    }
    
    float volume = settings->fCounterStrikeSoundVolume;
    
    SKSE::GetTaskInterface()->AddTask([volume]() {
        std::filesystem::path wavPath = std::filesystem::current_path();
        wavPath /= "Data";
        wavPath /= "SKSE";
        wavPath /= "Plugins";
        wavPath /= "TimedBlockDodgeCounter";
        wavPath /= "counterstrike.wav";
        
        if (std::filesystem::exists(wavPath)) {
            auto buf = LoadWAVWithVolume(wavPath, volume);
            if (!buf) {
                logger::error("Failed to load counter strike WAV file: {}", wavPath.string());
                RE::PlaySound("NPCHumanCombatShieldBlock");
                return;
            }
            g_counterStrikeAudioBuffer = buf;
            BOOL result = PlaySoundA(reinterpret_cast<LPCSTR>(buf->data()), 
                                     NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            if (!result) {
                logger::error("Failed to play counter strike WAV. Error: {}", GetLastError());
                RE::PlaySound("NPCHumanCombatShieldBlock");
            }
        } else {
            logger::warn("Counter strike WAV file not found: {} - using fallback sound", wavPath.string());
            RE::PlaySound("NPCHumanCombatShieldBlock");
        }
    });
}

void TimedDodgeState::PlayDodgeSound() {
    auto settings = Settings::GetSingleton();
    float volume = settings->fTimedDodgeSoundVolume;

    SKSE::GetTaskInterface()->AddTask([volume]() {
        std::filesystem::path wavPath = std::filesystem::current_path();
        wavPath /= "Data";
        wavPath /= "SKSE";
        wavPath /= "Plugins";
        wavPath /= "TimedBlockDodgeCounter";
        wavPath /= "timeddodge.wav";

        if (std::filesystem::exists(wavPath)) {
            auto buf = LoadWAVWithVolume(wavPath, volume);
            if (!buf) {
                logger::error("Failed to load timed dodge WAV: {}", wavPath.string());
                return;
            }
            g_timedDodgeAudioBuffer = buf;
            BOOL result = PlaySoundA(reinterpret_cast<LPCSTR>(buf->data()),
                                     NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
            if (!result) {
                logger::error("Failed to play timed dodge WAV. Error: {}", GetLastError());
            }
        } else {
            logger::warn("Timed dodge WAV not found: {} - using fallback sound", wavPath.string());
            RE::PlaySound("UIMenuOK");
        }
    });
}

static std::atomic<uint32_t> g_slowmoGeneration{ 0 };

void TimedBlockAddon::ApplySlowmo(float speed, float duration) {
    logger::debug("Applying slowmo: speed={}, duration={}s", speed, duration);
    
    const uint32_t gen = ++g_slowmoGeneration;
    
    SKSE::GetTaskInterface()->AddTask([speed]() {
        static REL::Relocation<float*> gtm{ RELOCATION_ID(511883, 388443) };
        *gtm = speed;
        logger::debug("Slowmo activated: {:.0f}%", speed * 100.0f);
    });
    
    std::thread([duration, gen]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<long long>(duration * 1000.0f)));
        
        SKSE::GetTaskInterface()->AddTask([gen]() {
            if (g_slowmoGeneration.load() != gen) {
                logger::debug("Slowmo restoration skipped - superseded by newer slowmo (gen {} vs {})",
                    gen, g_slowmoGeneration.load());
                return;
            }
            if (!TimedDodgeState::IsSlomoActive()) {
                static REL::Relocation<float*> gtm{ RELOCATION_ID(511883, 388443) };
                *gtm = 1.0f;
                logger::debug("Slowmo ended, speed restored to normal");
            } else {
                logger::debug("Slowmo restoration skipped - timed dodge slomo is active");
            }
        });
    }).detach();
}

//=============================================================================
// DispelParryWindowSpell - Safely removes parry window effect during cooldown
//=============================================================================

void DispelParryWindowSpell()
{
    auto* addon = TimedBlockAddon::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* settings = Settings::GetSingleton();
    
    if (!addon || !player) {
        return;
    }
    
    // Dispel active parry-window marker effect on the player
    // Access it through the addon's spell_parry_window member
    // Since spell_parry_window is private, we'll use dispel by effect instead
    
    auto magicTarget = player->AsMagicTarget();
    if (!magicTarget) {
        return;
    }
    
    auto activeEffects = magicTarget->GetActiveEffectList();
    if (!activeEffects || activeEffects->empty()) {
        return;
    }
    
    for (RE::ActiveEffect* effect : *activeEffects) {
        if (effect && !effect->flags.any(RE::ActiveEffect::Flag::kInactive)) {
            RE::EffectSetting* setting = effect->GetBaseObject();
            if (addon->IsParryWindowMGEF(setting)) {
                effect->Dispel(true);
                CooldownState::parryWindowTimerActive = false;
                if (settings->bDebugLogging) {
                    logger::info("[COOLDOWN] DISPELLED parry window effect");
                }
                return;
            }
        }
    }
}

//=============================================================================
// Combat hit — zero blocked melee damage during parry window (from simple-timed-block)
//=============================================================================

namespace CombatHit
{
    // ------------------------------------------------------------------------
    // 1.0.2 — Hook-free architecture.
    //
    // Past attempts:
    //   * v1.0.0 patched an in-function CALL site at hardcoded offset
    //     RELOCATION_OFFSET(0x3c0, 0x4A8) inside Actor::DealDamage. The 0x4A8
    //     value matched AE 1.6.640 only; on 1.6.1170 the patch landed in the
    //     middle of an unrelated instruction, decoded later as garbage like
    //     `dec [rbx+rdi*1-0x0D]`, and crashed every hit with
    //     EXCEPTION_ILLEGAL_INSTRUCTION at SkyrimSE.exe+06BAC31..C34.
    //   * v1.0.1 replaced that with a function-entry detour
    //     (write_branch<5> on Actor::DealDamage). That removed the
    //     hardcoded-offset bug, but other SKSE parry/combat plugins
    //     (DualWieldParryingNG, etc.) also install entry hooks on
    //     Actor::DealDamage. When two plugins chain hooks at the same
    //     entry, CommonLibSSE-NG's saved-prologue copy in our trampoline
    //     contained a JMP whose rel32 was relative to the original entry,
    //     not relative to our trampoline page. Calling _original then
    //     jumped to an unmapped page and crashed with
    //     EXCEPTION_ACCESS_VIOLATION ("Tried to execute memory at...").
    //
    // v1.0.2 removes the Actor::DealDamage hook entirely. The same three
    // behaviours are preserved by observation rather than patching:
    //
    //   1. Hit-during-dodge retroactive trigger
    //        — already handled by TimedBlockAddon::ProcessEvent (TESHitEvent)
    //          via TimedDodgeState::TryTriggerFromHit.
    //   2. Parry-window damage prevention (player only)
    //        — handled in TimedBlockAddon::ProcessEvent on a kHitBlocked
    //          event during the parry window: we read PlayerHealthState::
    //          lastFrameHealth, compute the HP drop, and RestoreActorValue
    //          the delta back. Net effect to the player is the same as the
    //          old in-function damage zeroing.
    //   3. Timed-dodge "real damage" cooldown
    //        — handled in TimedBlockAddon::ProcessEvent on a non-kHitBlocked
    //          event for the player: any HP drop > 0.5 since last frame is
    //          treated as real damage and triggers OnPlayerDamaged() and
    //          pendingDodge cancellation.
    //
    // The per-frame health snapshot is taken at the end of
    // PlayerCharacter::UpdateAnimation (PlayerHealthState::lastFrameHealth).
    // ------------------------------------------------------------------------

    void Install()
    {
        logger::info("[CombatHit] 1.0.4 hook-free mode: Actor::DealDamage is NOT patched. "
                     "Parry-window damage prevention via Precision PreHit Damage×0 modifier "
                     "(preserves vanilla block animation/sound/stamina; primary), "
                     "+ TESHitEvent post-hit restore (fallback for non-Precision hits). "
                     "Cooldown via per-frame HP snapshot.");
    }
}

//=============================================================================
// Counter Attack State - allows attacking to cancel block animation
//=============================================================================

void CounterAttackState::StartWindow(RE::Actor* attacker)
{
    auto* settings = Settings::GetSingleton();
    if (!settings->bEnableCounterAttack) {
        return;
    }
    
    inWindow = true;
    fromTimedDodge = false;
    fromWardTimedBlock = false;
    spellFiredDuringWindow = false;
    rangedCounterActive = false;
    trackedSpellProjectile = {};
    projectileScanRetries = 0;
    windowEndTime = std::chrono::steady_clock::now() + 
        std::chrono::milliseconds(static_cast<long long>(settings->fCounterAttackWindow * 1000.0f));
    
    // Store the attacker handle for lunge targeting
    if (attacker) {
        lastAttackerHandle = attacker->GetHandle();
    }
    
    // Arm the counter slow time if enabled
    if (settings->bEnableCounterSlowTime) {
        CounterSlowTimeState::Arm();
    }
    
    if (settings->bDebugLogging) {
        logger::info("[COUNTER] Counter attack window OPENED for {}ms", settings->fCounterAttackWindow * 1000.0f);
        DebugNotify(DebugCategory::kCounter, "[TB] Counter window open");
    }
}

void CounterAttackState::StartWardWindow(RE::Actor* attacker)
{
    auto* settings = Settings::GetSingleton();
    if (!settings->bWardTimedBlockCounterAttack) {
        return;
    }

    inWindow = true;
    fromTimedDodge = false;
    fromWardTimedBlock = true;
    spellFiredDuringWindow = false;
    rangedCounterActive = false;
    trackedSpellProjectile = {};
    projectileScanRetries = 0;
    windowEndTime = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<long long>(settings->fWardCounterWindowMs));

    if (attacker) {
        lastAttackerHandle = attacker->GetHandle();
    }

    if (settings->bEnableCounterSlowTime) {
        CounterSlowTimeState::Arm();
    }

    // Spell counter can land without a prior attack input — arm bonus immediately (same as timed block %)
    if (settings->bEnableCounterDamageBonus) {
        ApplyDamageBonus();
    }

    if (settings->bDebugLogging) {
        logger::info("[COUNTER] Ward counter window OPENED for {:.0f}ms", settings->fWardCounterWindowMs);
    }
}

RE::Actor* CounterAttackState::GetLastAttacker()
{
    auto actorPtr = lastAttackerHandle.get();
    return actorPtr.get();
}

void CounterAttackState::Update()
{
    if (!inWindow && !damageBonusActive) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto* settings = Settings::GetSingleton();
    
    // Check counter attack window timeout
    if (inWindow && now >= windowEndTime) {
        inWindow = false;
        if (!spellFiredDuringWindow) {
            fromWardTimedBlock = false;
        }

        if (settings->bDebugLogging) {
            logger::info("[COUNTER] Counter attack window CLOSED (timed out)");
        }
    }
    
    // --- Continuous projectile scan ---
    // Scan every frame while the ward counter bonus is active and we have not yet
    // latched a projectile.  This catches spells the player started charging before
    // the parry landed (so OnSpellFired never fired) as well as any delayed spawn.
    // Only non-destroyed projectiles count — a freshly-exploded Fireball stays in
    // the Manager briefly with kDestroyed set; we must not latch it at that point.
    constexpr std::uint32_t kProjDestroyed = (1u << 25);

    // --- Spell projectile scan (not used for ranged/arrow counters) ---
    // Arrows and bolts are NOT destroyed after firing — they persist in the world
    // and can be picked up.  Tracking them via Projectile::Manager is unreliable.
    // Ranged counter hits are detected purely via TESHitEvent in the hit handler.
    const bool wantSpellProjectileScan =
        (fromWardTimedBlock || (fromTimedDodge && spellFiredDuringWindow));

    if (wantSpellProjectileScan && damageBonusActive && !trackedSpellProjectile) {
        auto* projMgr = RE::Projectile::Manager::GetSingleton();
        if (projMgr) {
            auto scanArray = [&](RE::BSTArray<RE::ProjectileHandle>& arr) {
                for (std::int32_t i = static_cast<std::int32_t>(arr.size()) - 1; i >= 0; --i) {
                    auto projPtr = arr[i].get();
                    if (!projPtr) continue;
                    auto& rd = projPtr->GetProjectileRuntimeData();
                    if ((rd.flags & kProjDestroyed) != 0) continue;
                    auto shooterREFR = rd.shooter.get();
                    if (!shooterREFR || !shooterREFR.get() || !shooterREFR.get()->IsPlayerRef()) continue;

                    if (rd.spell) {
                        trackedSpellProjectile = arr[i];
                        if (!spellFiredDuringWindow) {
                            spellFiredDuringWindow = true;
                            inWindow = false;
                            damageBonusEndTime = now + std::chrono::milliseconds(
                                static_cast<long long>(settings->fWardCounterSpellInFlightMs));
                        }
                        logger::info("[COUNTER DAMAGE] Latched spell projectile");
                        return;
                    }
                }
            };
            scanArray(projMgr->unlimited);
            if (!trackedSpellProjectile) scanArray(projMgr->limited);
            if (!trackedSpellProjectile) scanArray(projMgr->pending);
        }
    }

    // --- Spell projectile destruction detection ---
    // When a tracked spell projectile (Fireball etc.) gains kDestroyed it has hit
    // something.  Clear the handle and give a 500ms grace window for the AoE event.
    // NOTE: this block intentionally does NOT run for ranged counters — arrows and
    // bolts are physical objects that persist after firing (see comment above).
    if (trackedSpellProjectile && damageBonusActive && wantSpellProjectileScan) {
        auto projPtr = trackedSpellProjectile.get();
        bool destroyed = !projPtr;
        if (!destroyed) {
            auto& rd = projPtr->GetProjectileRuntimeData();
            destroyed = (rd.flags & kProjDestroyed) != 0;
        }
        if (destroyed) {
            logger::info("[COUNTER DAMAGE] Tracked spell projectile destroyed — handle cleared, 500ms AoE grace window");
            spdlog::default_logger()->flush();
            trackedSpellProjectile = {};
            const auto graceDeadline = now + std::chrono::milliseconds(500);
            if (damageBonusEndTime > graceDeadline) {
                damageBonusEndTime = graceDeadline;
            }
        }
    }

    // --- Damage bonus timeout (fallback / concentration spells / ranged) ---
    if (damageBonusActive && now >= damageBonusEndTime) {
        const float timeoutLogged = fromTimedDodge
            ? (rangedCounterActive ? (settings->fTimedDodgeCounterRangedWindowMs / 1000.0f)
                                   : settings->fTimedDodgeCounterDamageTimeout)
            : fromWardTimedBlock ? (spellFiredDuringWindow
                ? settings->fWardCounterSpellInFlightMs / 1000.0f
                : settings->fWardCounterWindowMs / 1000.0f)
            : settings->fCounterDamageBonusTimeout;
        logger::info("[COUNTER DAMAGE] Damage bonus timed out ({:.1f}s, spellInFlight={}, ranged={}, tracked={})",
            timeoutLogged, spellFiredDuringWindow, rangedCounterActive, static_cast<bool>(trackedSpellProjectile));
        spdlog::default_logger()->flush();

        if (settings->bDebugLogging) {
            if (rangedCounterActive) {
                DebugNotify(DebugCategory::kDodge, "[TD] Ranged counter timed out");
            } else if (spellFiredDuringWindow) {
                DebugNotify(DebugCategory::kCounter, "[TB] Counter spell timed out");
            } else {
                DebugNotify(DebugCategory::kCounter, "[TB] Counter damage expired");
            }
        }
        const bool wasWard = fromWardTimedBlock;
        const bool wasDodgeSpell = fromTimedDodge && spellFiredDuringWindow;
        const bool wasDodgeRanged = fromTimedDodge && rangedCounterActive;
        RemoveDamageBonus();
        if (wasWard) {
            fromWardTimedBlock = false;
            inWindow = false;
        }
        if (wasDodgeSpell || wasDodgeRanged) {
            fromTimedDodge = false;
            inWindow = false;
        }
    }
}

bool CounterAttackState::IsInWindow()
{
    return inWindow;
}

void CounterAttackState::OnSpellFired()
{
    if ((!fromWardTimedBlock && !fromTimedDodge) || !damageBonusActive) {
        return;
    }
    // Only latch once per counter window — ignore repeat ticks.
    if (spellFiredDuringWindow) {
        return;
    }

    auto* settings = Settings::GetSingleton();
    spellFiredDuringWindow = true;
    trackedSpellProjectile = {};  // continuous scan in Update() will latch the projectile

    // Close the melee input window — the player has committed to a spell counter.
    // Any further attack-button presses should cast more spells, not trigger a
    // melee counter sound/bonus.  The damage bonus stays open until the projectile
    // lands (tracked by Update()) or the deadline times out.
    inWindow = false;

    // Extend the bonus deadline by the in-flight window.
    damageBonusEndTime = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<long long>(settings->fWardCounterSpellInFlightMs));

    if (settings->bDebugLogging) {
        logger::info("[COUNTER DAMAGE] Spell fired — melee window closed, bonus extended {:.0f}ms",
            settings->fWardCounterSpellInFlightMs);
        DebugNotify(DebugCategory::kCounter, "[TB] Counter spell in flight...");
    }
}

void CounterAttackState::OnRangedCounterInput()
{
    if (!inWindow || !fromTimedDodge) return;
    if (rangedCounterActive) return;

    auto* settings = Settings::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;

    // Apply the draw speed buff before anything else so the game sees the new
    // kWeaponSpeedMult value before the dodge animation is cancelled and the
    // bow draw begins.
    rangedCounterActive = true;
    ApplyDrawSpeedBuff();

    // End dodge slomo so the player can aim freely
    if (TimedDodgeState::IsSlomoActive()) {
        TimedDodgeState::End();
    }
    player->NotifyAnimationGraph("TKDodgeStop");

    ApplyDamageBonus();

    // Close the general counter input window; set a ranged-specific deadline.
    inWindow = false;
    damageBonusEndTime = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeCounterRangedWindowMs));

    // Trigger the bow/crossbow draw animation on the next frame (same pattern as
    // melee counter — the graph needs one tick after TKDodgeStop to leave the
    // dodge state before an attack action can begin).
    RE::ActorHandle playerHandle = player->GetHandle();
    SKSE::GetTaskInterface()->AddTask([playerHandle]() {
        auto playerPtr = playerHandle.get();
        if (auto* p = playerPtr.get()) {
            using PerformAction_t = bool(RE::TESActionData*);
            REL::Relocation<PerformAction_t> performAction{ RELOCATION_ID(40551, 41557) };

            auto* data = RE::TESActionData::Create();
            if (data) {
                data->source = RE::NiPointer<RE::TESObjectREFR>(p);
                data->action = RE::TESForm::LookupByID<RE::BGSAction>(0x13005);
                if (data->action) {
                    performAction(data);
                }
                delete data;
            }
        }
    });

    logger::info("[RANGED COUNTER] Initiated — draw speed buff active, {:.0f}ms window, scan delayed 300ms",
        settings->fTimedDodgeCounterRangedWindowMs);
    spdlog::default_logger()->flush();

    if (settings->bDebugLogging) {
        DebugNotify(DebugCategory::kDodge, "[TD] Ranged counter!");
    }
}

void CounterAttackState::OnAttackInput()
{
    if (!inWindow) {
        return;
    }

    // If the player already fired a counter spell this window, do not also
    // trigger a melee counter — the spell path owns the bonus from here on.
    if (fromWardTimedBlock && spellFiredDuringWindow) {
        return;
    }
    
    auto* settings = Settings::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    
    if (!player) {
        return;
    }

    // Only allow counter attacks with melee weapons or fists.
    // If the right hand has a spell, the player is trying to cast — not counter.
    auto* rightEquipped = player->GetEquippedObject(false);
    if (rightEquipped) {
        if (rightEquipped->IsWeapon()) {
            auto* weap = rightEquipped->As<RE::TESObjectWEAP>();
            if (weap) {
                auto type = weap->GetWeaponType();
                if (type == RE::WEAPON_TYPE::kBow ||
                    type == RE::WEAPON_TYPE::kCrossbow ||
                    type == RE::WEAPON_TYPE::kStaff) {
                    return;
                }
            }
        } else {
            // Non-weapon (SpellItem, torch, etc.) in the right hand.
            return;
        }
    } else {
        // GetEquippedObject can return null even when a spell is equipped
        // (the form is present in the magic system but not the inventory slot).
        // Check via MagicCaster as a fallback before treating it as unarmed.
        if (auto* mc = player->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand)) {
            if (mc->currentSpell) {
                return;
            }
        }
    }
    
    if (fromTimedDodge) {
        // End slomo so animations run at full speed
        if (TimedDodgeState::IsSlomoActive()) {
            TimedDodgeState::End();
        }
        
        // Cancel the dodge animation by sending TKDodgeStop — the same event
        // the dodge clip fires near its end to trigger the idle transition
        bool cancelled = player->NotifyAnimationGraph("TKDodgeStop");
        logger::info("[COUNTER] Dodge cancel via TKDodgeStop: {}", cancelled);
        
        ApplyDamageBonus();
        
        RE::Actor* attacker = GetLastAttacker();
        if (settings->bTimedDodgeCounterLunge) {
            if (attacker && attacker->Is3DLoaded()) {
                CounterLungeState::Start(player, attacker);
                logger::info("[COUNTER] Lunge started toward '{}'", attacker->GetName());
            } else {
                logger::info("[COUNTER] Lunge skipped: attacker={}, 3DLoaded={}",
                    attacker ? attacker->GetName() : "null",
                    attacker ? attacker->Is3DLoaded() : false);
            }
        } else {
            logger::info("[COUNTER] Lunge disabled by bTimedDodgeCounterLunge setting");
        }

        // Explicitly trigger the right-hand attack on the next frame.
        // After TKDodgeStop the graph needs one tick to leave the dodge state;
        // with weapon+spell combos the original button press doesn't propagate
        // into an attack on its own (unlike weapon+empty/shield).
        RE::ActorHandle playerHandle = player->GetHandle();
        SKSE::GetTaskInterface()->AddTask([playerHandle]() {
            auto playerPtr = playerHandle.get();
            if (auto* p = playerPtr.get()) {
                using PerformAction_t = bool(RE::TESActionData*);
                REL::Relocation<PerformAction_t> performAction{ RELOCATION_ID(40551, 41557) };

                auto* data = RE::TESActionData::Create();
                if (data) {
                    data->source = RE::NiPointer<RE::TESObjectREFR>(p);
                    data->action = RE::TESForm::LookupByID<RE::BGSAction>(0x13005);
                    if (data->action) {
                        performAction(data);
                    }
                    delete data;
                }
            }
        });

        spdlog::default_logger()->flush();
        
        if (settings->bDebugLogging) {
            logger::info("[COUNTER] Timed dodge counter attack executed");
            DebugNotify(DebugCategory::kDodge, "[TD] Counter attack!");
        }
        
        inWindow = false;
        return;
    }
    
    // Timed block path: cancel block recoil/stagger so the attack can follow through
    player->SetGraphVariableBool("IsStaggering", false);
    player->SetGraphVariableBool("IsRecoiling", false);
    player->SetGraphVariableBool("IsBlockHit", false);
    player->SetGraphVariableBool("bIsBlocking", false);
    player->SetGraphVariableFloat("staggerMagnitude", 0.0f);
    
    player->SetGraphVariableBool("Maxsu_IsBlockHit", false);
    player->SetGraphVariableBool("bMaxsu_BlockHit", false);
    player->SetGraphVariableFloat("Maxsu_BlockHitStrength", 0.0f);
    
    player->NotifyAnimationGraph("staggerStop");
    player->NotifyAnimationGraph("recoilStop");
    player->NotifyAnimationGraph("blockStop");
    player->NotifyAnimationGraph("BlockHitEnd");
    
    player->NotifyAnimationGraph("Maxsu_BlockHitEnd");
    player->NotifyAnimationGraph("Maxsu_BlockHitInterrupt");
    player->NotifyAnimationGraph("Maxsu_WeaponBlockHitEnd");
    player->NotifyAnimationGraph("Maxsu_ShieldBlockHitEnd");
    
    // If timed dodge slomo is active, cancel it
    if (TimedDodgeState::IsSlomoActive()) {
        TimedDodgeState::End();
        if (settings->bDebugLogging) {
            logger::info("[COUNTER] Cancelled timed dodge slomo via counter attack");
        }
    }
    
    // Start lunge toward attacker
    if (settings->bEnableCounterLunge) {
        RE::Actor* attacker = GetLastAttacker();
        if (attacker && attacker->Is3DLoaded()) {
            CounterLungeState::Start(player, attacker);
        }
    }
    
    // Apply damage bonus (ward counter may have already armed bonus in StartWardWindow)
    if (settings->bEnableCounterDamageBonus) {
        ApplyDamageBonus();
    }
    
    if (settings->bDebugLogging) {
        logger::info("[COUNTER] Attack input during counter window - timed block counter");
        DebugNotify(DebugCategory::kCounter, "[TB] Counter attack!");
    }
    
    inWindow = false;
    fromWardTimedBlock = false;
}

bool CounterAttackState::CreateCounterDamageForms()
{
    if (counterMGEF && counterSpell) {
        return true;
    }

    auto* mgefFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::EffectSetting>();
    auto* spelFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
    if (!mgefFactory || !spelFactory) {
        logger::error("[COUNTER DAMAGE] Form factory unavailable (MGEF and/or SPEL)");
        return false;
    }

    auto* dh = RE::TESDataHandler::GetSingleton();

    if (!counterMGEF) {
        auto* mgefForm = mgefFactory->Create();
        counterMGEF = mgefForm ? mgefForm->As<RE::EffectSetting>() : nullptr;
        if (!counterMGEF) {
            logger::error("[COUNTER DAMAGE] Failed to allocate EffectSetting");
            return false;
        }

        counterMGEF->fullName = "STBA Counter Strike";
        counterMGEF->magicItemDescription = "Counter strike bonus damage.";

        auto& md = counterMGEF->data;
        md.flags.set(RE::EffectSetting::EffectSettingData::Flag::kHostile);
        md.flags.set(RE::EffectSetting::EffectSettingData::Flag::kDetrimental);
        md.flags.set(RE::EffectSetting::EffectSettingData::Flag::kHideInUI);
        md.baseCost = 0.0f;
        md.archetype = RE::EffectArchetypes::ArchetypeID::kValueModifier;
        md.primaryAV = RE::ActorValue::kHealth;
        md.associatedSkill = RE::ActorValue::kNone;
        md.resistVariable = RE::ActorValue::kNone;
        md.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        md.delivery = RE::MagicSystem::Delivery::kTargetActor;

        if (dh && !AddFormToDataHandler(dh, counterMGEF)) {
            logger::warn("[COUNTER DAMAGE] AddFormToDataHandler failed for MGEF");
        }

        logger::info("[COUNTER DAMAGE] MGEF created — Damage Health, archetype={}, primaryAV={}, flags=0x{:08X}",
            static_cast<int>(md.archetype), static_cast<int>(md.primaryAV),
            md.flags.underlying());
    }

    if (!counterSpell) {
        if (!counterMGEF) {
            logger::error("[COUNTER DAMAGE] Missing MGEF — cannot build counter spell");
            return false;
        }

        auto* spelForm = spelFactory->Create();
        counterSpell = spelForm ? spelForm->As<RE::SpellItem>() : nullptr;
        if (!counterSpell) {
            logger::error("[COUNTER DAMAGE] Failed to allocate SpellItem");
            return false;
        }

        counterSpell->fullName = "STBA Counter Strike";
        counterSpell->data.spellType = RE::MagicSystem::SpellType::kSpell;
        counterSpell->data.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        counterSpell->data.delivery = RE::MagicSystem::Delivery::kTargetActor;
        counterSpell->data.chargeTime = 0.0f;
        counterSpell->data.castDuration = 0.0f;
        counterSpell->data.range = 0.0f;
        counterSpell->data.costOverride = 0;
        counterSpell->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);

        auto* eff = new RE::Effect();
        eff->baseEffect = counterMGEF;
        eff->effectItem.magnitude = 0.0f;
        eff->effectItem.duration = 0;
        eff->effectItem.area = 0;
        eff->cost = 0.0f;
        counterSpell->effects.push_back(eff);

        if (dh && !AddFormToDataHandler(dh, counterSpell)) {
            logger::warn("[COUNTER DAMAGE] AddFormToDataHandler failed for SPEL");
        }

        logger::info("[COUNTER DAMAGE] Runtime forms OK — MGEF {:08X}, SPEL {:08X}",
            counterMGEF->GetFormID(), counterSpell->GetFormID());
    }

    return counterMGEF != nullptr && counterSpell != nullptr;
}

bool CounterAttackState::CreateDrawSpeedForms()
{
    if (drawSpeedMGEF && drawSpeedSpell) {
        return true;
    }

    auto* mgefFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::EffectSetting>();
    auto* spelFactory = RE::IFormFactory::GetConcreteFormFactoryByType<RE::SpellItem>();
    if (!mgefFactory || !spelFactory) {
        logger::error("[RANGED COUNTER] Form factory unavailable");
        return false;
    }

    auto* dh = RE::TESDataHandler::GetSingleton();

    if (!drawSpeedMGEF) {
        auto* mgefForm = mgefFactory->Create();
        drawSpeedMGEF = mgefForm ? mgefForm->As<RE::EffectSetting>() : nullptr;
        if (!drawSpeedMGEF) {
            logger::error("[RANGED COUNTER] Failed to allocate EffectSetting");
            return false;
        }

        drawSpeedMGEF->fullName = "STBA Draw Speed Boost";

        using Flag = RE::EffectSetting::EffectSettingData::Flag;
        auto& md = drawSpeedMGEF->data;
        md.flags.set(Flag::kRecover);
        md.flags.set(Flag::kNoDuration);
        md.flags.set(Flag::kNoArea);
        md.flags.set(Flag::kHideInUI);
        md.baseCost = 0.0f;
        md.archetype = RE::EffectArchetypes::ArchetypeID::kPeakValueModifier;
        md.primaryAV = RE::ActorValue::kWeaponSpeedMult;
        md.associatedSkill = RE::ActorValue::kNone;
        md.resistVariable = RE::ActorValue::kNone;
        md.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        md.delivery = RE::MagicSystem::Delivery::kSelf;

        if (dh && !AddFormToDataHandler(dh, drawSpeedMGEF)) {
            logger::warn("[RANGED COUNTER] AddFormToDataHandler failed for MGEF");
        }
        logger::info("[RANGED COUNTER] Draw speed MGEF created (PVM on WeaponSpeedMult)");
    }

    if (!drawSpeedSpell) {
        auto* spelForm = spelFactory->Create();
        drawSpeedSpell = spelForm ? spelForm->As<RE::SpellItem>() : nullptr;
        if (!drawSpeedSpell) {
            logger::error("[RANGED COUNTER] Failed to allocate SpellItem");
            return false;
        }

        drawSpeedSpell->fullName = "STBA Draw Speed Boost";
        drawSpeedSpell->data.spellType = RE::MagicSystem::SpellType::kAbility;
        drawSpeedSpell->data.castingType = RE::MagicSystem::CastingType::kFireAndForget;
        drawSpeedSpell->data.delivery = RE::MagicSystem::Delivery::kSelf;
        drawSpeedSpell->data.chargeTime = 0.0f;
        drawSpeedSpell->data.castDuration = 0.0f;
        drawSpeedSpell->data.range = 0.0f;
        drawSpeedSpell->data.costOverride = 0;
        drawSpeedSpell->data.flags.set(RE::SpellItem::SpellFlag::kCostOverride);

        auto* effect = new RE::Effect();
        effect->baseEffect = drawSpeedMGEF;
        effect->effectItem.magnitude = 1.0f;
        effect->effectItem.area = 0;
        effect->effectItem.duration = 0;
        drawSpeedSpell->effects.push_back(effect);

        if (dh && !AddFormToDataHandler(dh, drawSpeedSpell)) {
            logger::warn("[RANGED COUNTER] AddFormToDataHandler failed for SPEL");
        }
        logger::info("[RANGED COUNTER] Draw speed SPEL created");
    }

    return drawSpeedMGEF != nullptr && drawSpeedSpell != nullptr;
}

void CounterAttackState::ApplyDrawSpeedBuff()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* settings = Settings::GetSingleton();
    if (!player || !drawSpeedSpell) return;

    float magnitude = settings->fTimedDodgeCounterDrawSpeedMult - 1.0f;
    if (magnitude <= 0.0f) return;

    if (!drawSpeedSpell->effects.empty() && drawSpeedSpell->effects[0]) {
        drawSpeedSpell->effects[0]->effectItem.magnitude = magnitude;
    }

    auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
    if (caster) {
        caster->CastSpellImmediate(drawSpeedSpell, true, player, 1.0f, false, 0.0f, player);
    }

    logger::info("[RANGED COUNTER] Draw speed buff applied (+{:.0f}% weapon speed)", magnitude * 100.0f);
}

void CounterAttackState::RemoveDrawSpeedBuff()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !drawSpeedMGEF) return;

    auto* magicTarget = player->AsMagicTarget();
    if (!magicTarget) return;

    auto* effects = magicTarget->GetActiveEffectList();
    if (!effects) return;

    for (auto* ae : *effects) {
        if (!ae) continue;
        if (ae->GetBaseObject() == drawSpeedMGEF) {
            ae->Dispel(true);
            logger::info("[RANGED COUNTER] Draw speed buff dispelled");
            return;
        }
    }
}

void CounterAttackState::ApplyDamageBonus(bool isSpellCounter)
{
    auto* settings = Settings::GetSingleton();

    if (damageBonusActive) {
        logger::info("[COUNTER DAMAGE] Bonus already active, skipping");
        return;
    }

    float damagePercent = fromTimedDodge
        ? (isSpellCounter     ? settings->fTimedDodgeCounterSpellDamagePercent
         : rangedCounterActive ? settings->fTimedDodgeCounterRangedDamagePercent
         :                       settings->fTimedDodgeCounterDamagePercent)
        : fromWardTimedBlock ? settings->fWardCounterDamagePercent
        : settings->fCounterDamageBonusPercent;

    appliedDamageBonus = damagePercent;
    damageBonusActive = true;

    float timeout = fromTimedDodge
        ? (rangedCounterActive ? (settings->fTimedDodgeCounterRangedWindowMs / 1000.0f)
                               : settings->fTimedDodgeCounterDamageTimeout)
        : fromWardTimedBlock ? (settings->fWardCounterWindowMs / 1000.0f)
        : settings->fCounterDamageBonusTimeout;
    auto timeoutMs = static_cast<int>(timeout * 1000.0f);
    damageBonusEndTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    const char* src = "timed block";
    if (fromTimedDodge) {
        src = "timed dodge";
    } else if (fromWardTimedBlock) {
        src = "ward timed block";
    }

    logger::info("[COUNTER DAMAGE] Armed +{:.0f}% bonus (timeout {:.1f}s, source {})",
        damagePercent, timeout, src);
    spdlog::default_logger()->flush();

    if (settings->bDebugLogging) {
        DebugNotify(DebugCategory::kCounter, fmt::format("[TB] +{:.0f}% damage ready!", damagePercent).c_str());
    }
}

void CounterAttackState::RemoveDamageBonus()
{
    if (!damageBonusActive) {
        return;
    }

    const float was = appliedDamageBonus;
    appliedDamageBonus = 0.0f;
    damageBonusActive = false;
    spellFiredDuringWindow = false;
    trackedSpellProjectile = {};
    projectileScanRetries = 0;

    if (rangedCounterActive) {
        RemoveDrawSpeedBuff();
        rangedCounterActive = false;
    }

    logger::info("[COUNTER DAMAGE] Bonus cleared (was +{:.0f}%)", was);
    spdlog::default_logger()->flush();
}

bool CounterAttackState::IsDamageBonusActive()
{
    return damageBonusActive;
}

//=============================================================================
// Counter Attack Input Handler
//=============================================================================

namespace OCPAKeys
{
    static inline std::int32_t paKey{ -1 };
    static inline std::int32_t dualPaKey{ -1 };
    static inline bool loaded{ false };

    void Load()
    {
        if (loaded) return;
        loaded = true;

        std::string path = "Data\\MCM\\Config\\OCPA\\settings.ini";
        if (std::filesystem::exists("Data\\MCM\\Settings\\OCPA.ini")) {
            path = "Data\\MCM\\Settings\\OCPA.ini";
        }

        if (!std::filesystem::exists(path)) {
            logger::info("[OCPA] No OCPA INI found — power attack key detection disabled");
            return;
        }

        CSimpleIniA ini;
        if (ini.LoadFile(path.c_str()) >= 0) {
            paKey     = std::atoi(ini.GetValue("General",    "iKeycode", "257"));
            dualPaKey = std::atoi(ini.GetValue("DualAttack", "iKeycode", "257"));
            logger::info("[OCPA] Loaded power attack keys: paKey={}, dualPaKey={}", paKey, dualPaKey);
        }
    }

    // Convert a ButtonEvent to OCPA's unified keycode scheme:
    //   0-255 = keyboard (DX scan codes), 256-265 = mouse, 266-281 = gamepad
    std::int32_t ButtonToUnifiedKey(RE::ButtonEvent* btn)
    {
        auto device = btn->GetDevice();
        auto id     = static_cast<std::int32_t>(btn->GetIDCode());

        switch (device) {
        case RE::INPUT_DEVICE::kKeyboard:
            return id;
        case RE::INPUT_DEVICE::kMouse:
            return 256 + id;
        case RE::INPUT_DEVICE::kGamepad: {
            // OCPA converts gamepad mask to sequential index (same as SKSE convention)
            std::uint32_t mask = static_cast<std::uint32_t>(id);
            std::int32_t idx = 266;
            while (mask > 1 && idx < 282) { mask >>= 1; ++idx; }
            return idx;
        }
        default:
            return -1;
        }
    }

    bool IsOCPAKey(RE::ButtonEvent* btn)
    {
        if (paKey < 0 && dualPaKey < 0) return false;
        std::int32_t key = ButtonToUnifiedKey(btn);
        return (key == paKey || key == dualPaKey);
    }
}

namespace BlockKeyMatch
{
    constexpr std::int32_t kUnset = -2;

    static inline std::int32_t g_cachedUnified{ kUnset };
    static inline std::string g_cachedPattern;

    struct NamedKey { const char* name; std::int32_t code; };

    // SKSE unified key codes: 0-255 keyboard (DX scan), 256-263 mouse buttons,
    // 264-265 mouse wheel, 266-281 gamepad.
    static constexpr NamedKey kNameTable[] = {
        // Function keys (DX scan codes)
        {"F1",  0x3B}, {"F2",  0x3C}, {"F3",  0x3D}, {"F4",  0x3E},
        {"F5",  0x3F}, {"F6",  0x40}, {"F7",  0x41}, {"F8",  0x42},
        {"F9",  0x43}, {"F10", 0x44}, {"F11", 0x57}, {"F12", 0x58},
        // Common named keys
        {"Escape",    0x01}, {"Tab",    0x0F}, {"CapsLock",  0x3A},
        {"LShift",    0x2A}, {"RShift", 0x36}, {"LControl",  0x1D},
        {"RControl",  0x9D}, {"LAlt",   0x38}, {"RAlt",      0xB8},
        {"Space",     0x39}, {"Enter",  0x1C}, {"Backspace", 0x0E},
        {"Insert",    0xD2}, {"Delete", 0xD3}, {"Home",      0xC7},
        {"End",       0xCF}, {"PageUp", 0xC9}, {"PageDown",  0xD1},
        {"Up",        0xC8}, {"Down",   0xD0}, {"Left",      0xCB},
        {"Right",     0xCD}, {"NumLock",0x45}, {"ScrollLock",0x46},
        // Numpad
        {"Numpad0",0x52}, {"Numpad1",0x4F}, {"Numpad2",0x50}, {"Numpad3",0x51},
        {"Numpad4",0x4B}, {"Numpad5",0x4C}, {"Numpad6",0x4D}, {"Numpad7",0x47},
        {"Numpad8",0x48}, {"Numpad9",0x49}, {"NumpadMul",0x37}, {"NumpadAdd",0x4E},
        {"NumpadSub",0x4A}, {"NumpadDot",0x53}, {"NumpadDiv",0xB5}, {"NumpadEnter",0x9C},
        // Mouse buttons (SKSE unified: 256 + button index)
        {"Mouse1",256}, {"Mouse2",257}, {"Mouse3",258}, {"Mouse4",259},
        {"Mouse5",260}, {"Mouse6",261}, {"Mouse7",262}, {"Mouse8",263},
        {"MouseWheelUp",264}, {"MouseWheelDown",265},
        // Gamepad (SKSE unified: 266+)
        {"DpadUp",266}, {"DpadDown",267}, {"DpadLeft",268}, {"DpadRight",269},
        {"Start",270}, {"Back",271}, {"LeftThumb",272}, {"RightThumb",273},
        {"LeftShoulder",274}, {"RightShoulder",275},
        {"GamepadA",276}, {"GamepadB",277}, {"GamepadX",278}, {"GamepadY",279},
        {"LTrigger",280}, {"RTrigger",281},
    };

    static std::int32_t LookupName(const std::string& name)
    {
        for (const auto& k : kNameTable) {
            if (_stricmp(k.name, name.c_str()) == 0) {
                return k.code;
            }
        }
        return -1;
    }

    static std::optional<std::int32_t> CharToKeyboardScan(char c)
    {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        if (c < 'A' || c > 'Z') {
            return std::nullopt;
        }
        const SHORT vkScan = ::VkKeyScanA(c);
        if (vkScan == static_cast<SHORT>(-1)) {
            return std::nullopt;
        }
        const UINT vk = static_cast<UINT>(LOBYTE(vkScan));
        const UINT scan = ::MapVirtualKeyA(vk, MAPVK_VK_TO_VSC_EX);
        if (scan == 0) {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(scan);
    }

    static std::int32_t ResolveUnified(const std::string& pattern)
    {
        if (pattern == g_cachedPattern && g_cachedUnified != kUnset) {
            return g_cachedUnified;
        }
        g_cachedPattern = pattern;
        g_cachedUnified = -1;
        if (pattern.empty()) {
            return g_cachedUnified;
        }

        // 1. Pure numeric → literal unified code
        bool allDigit = true;
        for (unsigned char ch : pattern) {
            if (!std::isdigit(ch)) {
                allDigit = false;
                break;
            }
        }
        if (allDigit) {
            try {
                g_cachedUnified = static_cast<std::int32_t>(std::stoul(pattern));
            } catch (...) {}
            return g_cachedUnified;
        }

        // 2. Named key table (case-insensitive): "Mouse4", "F1", "DpadUp", etc.
        std::int32_t named = LookupName(pattern);
        if (named >= 0) {
            g_cachedUnified = named;
            return g_cachedUnified;
        }

        // 3. Single letter A-Z → DX scan code via Win32
        if (pattern.size() == 1) {
            if (const auto u = CharToKeyboardScan(pattern[0])) {
                g_cachedUnified = *u;
                return g_cachedUnified;
            }
        }

        logger::warn("[BLOCK KEY] Could not resolve sBlockKey '{}' to a key code", pattern);
        return g_cachedUnified;
    }
}

BlockKeyInputHandler* BlockKeyInputHandler::GetSingleton()
{
    static BlockKeyInputHandler singleton;
    return &singleton;
}

void BlockKeyInputHandler::Register()
{
    auto* inputEventSource = RE::BSInputDeviceManager::GetSingleton();
    if (inputEventSource) {
        inputEventSource->AddEventSink(GetSingleton());
        logger::info("Block key (dual-wield) input handler registered");
    }
}

RE::BSEventNotifyControl BlockKeyInputHandler::ProcessEvent(
    RE::InputEvent* const* a_event,
    RE::BSTEventSource<RE::InputEvent*>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* settings = Settings::GetSingleton();
    const std::int32_t want = BlockKeyMatch::ResolveUnified(settings->sBlockKey);
    if (want < 0) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* ui = RE::UI::GetSingleton();
    if (ui && ui->GameIsPaused()) {
        return RE::BSEventNotifyControl::kContinue;
    }

    for (auto* ev = *a_event; ev; ev = ev->next) {
        if (ev->eventType != RE::INPUT_EVENT_TYPE::kButton) {
            continue;
        }
        auto* btn = static_cast<RE::ButtonEvent*>(ev);
        if (!btn->IsDown()) {
            continue;
        }
        if (OCPAKeys::ButtonToUnifiedKey(btn) != want) {
            continue;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            continue;
        }
        auto* addon = TimedBlockAddon::GetSingleton();
        if (!addon || !addon->HasParryFormsReady()) {
            continue;
        }
        if (WindowExclusion::IsBlocked()) {
            continue;
        }
        if (addon->ActorHasParryWindowEffect(player)) {
            continue;
        }
        addon->CastParryWindowSpell(player);
    }

    return RE::BSEventNotifyControl::kContinue;
}

CounterAttackInputHandler* CounterAttackInputHandler::GetSingleton()
{
    static CounterAttackInputHandler singleton;
    return &singleton;
}

void CounterAttackInputHandler::Register()
{
    OCPAKeys::Load();

    auto* inputEventSource = RE::BSInputDeviceManager::GetSingleton();
    if (inputEventSource) {
        inputEventSource->AddEventSink(GetSingleton());
        logger::info("Counter attack input handler registered");
    }
}

RE::BSEventNotifyControl CounterAttackInputHandler::ProcessEvent(
    RE::InputEvent* const* a_event,
    RE::BSTEventSource<RE::InputEvent*>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    auto* settings = Settings::GetSingleton();
    bool counterEnabled = settings->bEnableCounterAttack ||
                          (settings->bTimedDodgeCounterAttack && TimedDodgeState::IsActive()) ||
                          (settings->bWardTimedBlockCounterAttack && CounterAttackState::fromWardTimedBlock);
    if (!counterEnabled || !CounterAttackState::IsInWindow()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Check if UI is open
    auto* ui = RE::UI::GetSingleton();
    if (ui && ui->GameIsPaused()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    for (auto* event = *a_event; event; event = event->next) {
        if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) {
            continue;
        }
        
        auto* buttonEvent = static_cast<RE::ButtonEvent*>(event);
        if (!buttonEvent->IsDown()) {
            continue;
        }
        
        // Check standard Skyrim attack controls
        bool isAttackInput = false;
        bool isLeftHandInput = false;
        auto* controlMap = RE::ControlMap::GetSingleton();
        if (controlMap) {
            std::string_view userEvent = controlMap->GetUserEventName(
                buttonEvent->GetIDCode(), 
                buttonEvent->GetDevice(), 
                RE::ControlMap::InputContextID::kGameplay
            );
            
            if (userEvent == "Right Attack/Block" || userEvent == "Attack" || userEvent == "Power Attack") {
                isAttackInput = true;
                isLeftHandInput = false;
            } else if (userEvent == "Left Attack/Block") {
                isAttackInput = true;
                isLeftHandInput = true;
            }
        }
        
        // Also check OneClickPowerAttack's custom power attack key (always right-hand)
        if (!isAttackInput && OCPAKeys::IsOCPAKey(buttonEvent)) {
            isAttackInput = true;
            isLeftHandInput = false;
        }
        
        if (isAttackInput) {
            // Before treating this as a melee counter, check whether the hand
            // being used has a spell equipped. If so, the player is trying to
            // CAST a spell (e.g. ward counter spell), not punch/swing a weapon.
            auto* player = RE::PlayerCharacter::GetSingleton();
            bool attackingHandHasSpell = false;
            if (player) {
                auto handHasSpell = [&](bool leftHand) -> bool {
                    auto* eq = player->GetEquippedObject(leftHand);
                    if (eq && !eq->IsWeapon()) {
                        return true;  // SpellItem, torch, or other non-weapon
                    }
                    if (!eq) {
                        // GetEquippedObject can return null for a spell that is
                        // equipped but not yet casting. Fall back to MagicCaster.
                        auto src = leftHand ? RE::MagicSystem::CastingSource::kLeftHand
                                            : RE::MagicSystem::CastingSource::kRightHand;
                        if (auto* mc = player->GetMagicCaster(src)) {
                            if (mc->currentSpell) {
                                return true;
                            }
                        }
                    }
                    return false;
                };
                attackingHandHasSpell = handHasSpell(isLeftHandInput);
            }

            if (!attackingHandHasSpell) {
                // Check for ranged counter (bow/crossbow) from timed dodge
                if (CounterAttackState::inWindow && CounterAttackState::fromTimedDodge &&
                    settings->bTimedDodgeCounterRanged && player) {
                    auto* eq = player->GetEquippedObject(false);
                    if (eq && eq->IsWeapon()) {
                        auto* weap = eq->As<RE::TESObjectWEAP>();
                        if (weap && (weap->GetWeaponType() == RE::WEAPON_TYPE::kBow ||
                                     weap->GetWeaponType() == RE::WEAPON_TYPE::kCrossbow)) {
                            CounterAttackState::OnRangedCounterInput();
                            break;
                        }
                    }
                }
                CounterAttackState::OnAttackInput();
            } else if (CounterAttackState::inWindow && CounterAttackState::fromTimedDodge &&
                       settings->bTimedDodgeCounterSpellHit) {
                // Spell in hand + timed dodge counter window → spell counter attack.
                // Cancel the dodge animation so the player can cast freely.
                if (TimedDodgeState::IsSlomoActive()) {
                    TimedDodgeState::End();
                }
                player->NotifyAnimationGraph("TKDodgeStop");
                CounterAttackState::ApplyDamageBonus(true);
                CounterAttackState::OnSpellFired();

                if (settings->bDebugLogging) {
                    logger::info("[COUNTER] Timed dodge spell counter initiated");
                    DebugNotify(DebugCategory::kDodge, "[TD] Counter spell!");
                }
            }
            break;
        }
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

//=============================================================================
// Counter Lunge State - moves player toward attacker
//=============================================================================

void CounterLungeState::Start(RE::Actor* player, RE::Actor* target)
{
    if (!player || !target) {
        return;
    }

    auto* settings = Settings::GetSingleton();
    const float meleeStop = CounterAttackState::fromTimedDodge
        ? settings->fTimedDodgeCounterLungeMeleeStopDistance
        : settings->fCounterLungeMeleeStopDistance;
    
    startPos = player->GetPosition();
    RE::NiPoint3 targetPos = target->GetPosition();
    
    RE::NiPoint3 diff = targetPos - startPos;
    diff.z = 0.0f;
    float distance = diff.Length();
    
    // Already within stop distance, no lunge needed
    if (distance <= meleeStop) {
        if (settings->bDebugLogging) {
            logger::info("[LUNGE] Already within stop distance ({:.1f} <= {:.1f}), skipping", distance, meleeStop);
        }
        return;
    }
    
    targetHandle = target->GetHandle();
    meleeStopDistance = meleeStop;  // cache for ApplyVelocity
    
    // Travel enough to reach stop distance, capped by the max lunge distance setting
    float desiredTravel = distance - meleeStop;
    totalDistance = (std::min)(desiredTravel, settings->fCounterLungeDistance);
    
    const float lungeSpeed = CounterAttackState::fromTimedDodge ? settings->fTimedDodgeCounterLungeSpeed
                                                                : settings->fCounterLungeSpeed;
    curveType = CounterAttackState::fromTimedDodge ? settings->iTimedDodgeCounterLungeCurve
                                                   : settings->iCounterLungeCurve;
    
    // Peak of each velocity curve profile f(t) where ∫f(t)dt = 1 over [0,1]:
    //   Bell (6t(1-t)):  peak = 1.5   @ t=0.5
    //   Linear (1):      peak = 1.0
    //   EaseIn (2t):     peak = 2.0   @ t=1
    //   EaseOut (2(1-t)):peak = 2.0   @ t=0
    //   CubicIn (3t²):   peak = 3.0   @ t=1
    //   CubicOut(3(1-t)²):peak= 3.0   @ t=0
    static constexpr float curvePeaks[] = { 1.5f, 1.0f, 2.0f, 2.0f, 3.0f, 3.0f };
    int clampedCurve = curveType < 0 ? 0 : (curveType > 5 ? 5 : curveType);
    float peakFactor = curvePeaks[clampedCurve];
    
    // duration chosen so peak velocity == lungeSpeed
    duration = peakFactor * totalDistance / lungeSpeed;
    if (duration < 0.05f) duration = 0.05f;
    if (duration > 2.0f)  duration = 2.0f;
    
    elapsed = 0.0f;
    loggedFirstFrame = false;
    active = true;
    
    static const char* curveNames[] = { "Bell", "Linear", "EaseIn", "EaseOut", "CubicIn", "CubicOut" };
    logger::info("[LUNGE] Started: target='{}', dist={:.0f}, travel={:.0f}, duration={:.2f}s, curve={}",
        target->GetName(), distance, totalDistance, duration, curveNames[clampedCurve]);
    spdlog::default_logger()->flush();
}

void CounterLungeState::ApplyVelocity(RE::bhkCharacterController* controller, float deltaTime)
{
    if (!active || !controller || deltaTime <= 0.0f) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        Cancel();
        return;
    }

    auto targetPtr = targetHandle.get();
    RE::Actor* target = targetPtr.get();
    if (!target || !target->Is3DLoaded()) {
        Cancel();
        return;
    }

    RE::NiPoint3 currentPos = player->GetPosition();
    RE::NiPoint3 targetPos  = target->GetPosition();

    RE::NiPoint3 toTarget = targetPos - currentPos;
    toTarget.z = 0.0f;
    float distanceToTarget = toTarget.Length();

    if (!loggedFirstFrame) {
        loggedFirstFrame = true;
        logger::info("[LUNGE] First physics frame: dist={:.0f}, stopDist={:.0f}, dt={:.4f}",
            distanceToTarget, meleeStopDistance, deltaTime);
        spdlog::default_logger()->flush();
    }

    if (distanceToTarget <= meleeStopDistance) {
        logger::info("[LUNGE] Reached stop distance ({:.0f} <= {:.0f}), stopping", distanceToTarget, meleeStopDistance);
        Cancel();
        return;
    }

    elapsed += deltaTime;
    float t = elapsed / duration;

    if (t >= 1.0f) {
        Cancel();
        return;
    }

    // Velocity profile f(t) — each integrates to 1 over [0,1]
    float fvel;
    switch (curveType) {
    case 1:  fvel = 1.0f;                                      break;  // Linear
    case 2:  fvel = 2.0f * t;                                  break;  // Ease In
    case 3:  fvel = 2.0f * (1.0f - t);                        break;  // Ease Out
    case 4:  fvel = 3.0f * t * t;                              break;  // Cubic In
    case 5:  fvel = 3.0f * (1.0f - t) * (1.0f - t);           break;  // Cubic Out
    default: fvel = 6.0f * t * (1.0f - t);                    break;  // Bell
    }
    float currentSpeed = (totalDistance / duration) * fvel;

    // --- Horizontal: velocityMod in character-local space (proven to work) ---
    RE::NiPoint3 worldDir = toTarget / distanceToTarget;
    float heading = player->GetAngleZ();
    float sinH = std::sin(heading);
    float cosH = std::cos(heading);

    float localForward = worldDir.x * sinH + worldDir.y * cosH;
    float localStrafe  = worldDir.x * cosH - worldDir.y * sinH;

    auto* vel = reinterpret_cast<float*>(&(controller->velocityMod));
    vel[0] = currentSpeed * localStrafe;
    vel[1] = currentSpeed * localForward;

    // --- Vertical: raycast to find ground Z, set velocityMod[2] to pull toward it ---
    // Without this the character gets launched off ledge edges by collision normals.
    float groundZ = currentPos.z;
    bool  hitGround = false;

    auto* cell = player->GetParentCell();
    if (cell) {
        auto* bWorld = cell->GetbhkWorld();
        if (bWorld) {
            const float worldScale = RE::bhkWorld::GetWorldScale();

            const float probeFromZ = currentPos.z + 64.0f;
            const float probeToZ   = currentPos.z - 4096.0f;

            RE::hkpWorldRayCastInput  rayIn;
            RE::hkpWorldRayCastOutput rayOut;

            std::uint32_t filterInfo = 0;
            player->GetCollisionFilterInfo(filterInfo);
            const auto group = static_cast<std::uint32_t>(filterInfo >> 16);
            rayIn.filterInfo =
                (group << 16) | static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);

            rayIn.from.quad.m128_f32[0] = currentPos.x * worldScale;
            rayIn.from.quad.m128_f32[1] = currentPos.y * worldScale;
            rayIn.from.quad.m128_f32[2] = probeFromZ * worldScale;
            rayIn.from.quad.m128_f32[3] = 0.0f;

            rayIn.to.quad.m128_f32[0] = currentPos.x * worldScale;
            rayIn.to.quad.m128_f32[1] = currentPos.y * worldScale;
            rayIn.to.quad.m128_f32[2] = probeToZ * worldScale;
            rayIn.to.quad.m128_f32[3] = 0.0f;

            {
                RE::BSReadLockGuard lock(bWorld->worldLock);
                bWorld->GetWorld1()->CastRay(rayIn, rayOut);
            }

            if (rayOut.HasHit()) {
                groundZ  = probeFromZ + (probeToZ - probeFromZ) * rayOut.hitFraction;
                hitGround = true;
            }
        }
    }

    if (hitGround) {
        float zDelta = groundZ - currentPos.z;
        // Compute the vertical velocity needed to reach ground Z this frame.
        // Clamp so we don't overshoot on big drops; ground collision stops us anyway.
        float desiredZVel = zDelta / deltaTime;
        // Cap the downward pull so slopes feel natural (max ~2000 units/s down)
        if (desiredZVel < -2000.0f) desiredZVel = -2000.0f;
        // Never allow upward — ground collision handles walking up slopes naturally.
        if (desiredZVel > 0.0f) desiredZVel = 0.0f;
        vel[2] = desiredZVel;
    } else {
        vel[2] = 0.0f;
    }
}

bool CounterLungeState::IsActive()
{
    return active;
}

void CounterLungeState::Cancel()
{
    bool wasActive = active;
    
    if (active) {
        logger::info("[LUNGE] Ended after {:.3f}s", elapsed);
        spdlog::default_logger()->flush();
    }
    
    active = false;
    elapsed = 0.0f;
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        if (auto* controller = player->GetCharController()) {
            auto* vel = reinterpret_cast<float*>(&(controller->velocityMod));
            vel[0] = 0.0f;
            vel[1] = 0.0f;
            vel[2] = 0.0f;
        }
    }
    
    // If lunge was active and slow time should start after lunge, trigger it now
    if (wasActive) {
        auto* settings = Settings::GetSingleton();
        if (settings->bEnableCounterSlowTime && settings->bCounterSlowStartAfterLunge) {
            // Check if slow time is armed (waiting for trigger)
            if (CounterSlowTimeState::waitingForStartEvent) {
                if (settings->bDebugLogging) {
                    logger::info("[LUNGE] Triggering slow time after lunge end");
                }
                CounterSlowTimeState::Start();
            }
        }
    }
}

//=============================================================================
// Lunge Physics Hook - hooks into Havok physics simulation
//=============================================================================

namespace LungePhysicsHook
{
    // Signature of bhkCharacterStateOnGround::SimulateStatePhysics
    using SimulateStatePhysics_t    = void(RE::bhkCharacterStateOnGround*, RE::bhkCharacterController*);
    using SimulateStatePhysicsAir_t = void(RE::bhkCharacterStateInAir*,     RE::bhkCharacterController*);
    
    static REL::Relocation<SimulateStatePhysics_t>    g_originalSimulate;
    static REL::Relocation<SimulateStatePhysicsAir_t> g_originalSimulateAir;

    void SimulateStatePhysics_Hook(RE::bhkCharacterStateOnGround* a_this, RE::bhkCharacterController* a_controller)
    {
        if (a_controller && CounterLungeState::IsActive()) {
            // Set velocityMod BEFORE physics reads it.
            // XY = lunge horizontal, Z = raycast ground-pull.
            CounterLungeState::ApplyVelocity(a_controller, a_controller->stepInfo.deltaTime);
        }

        g_originalSimulate(a_this, a_controller);

        if (a_controller && CounterLungeState::IsActive()) {
            // After physics, kill any residual upward proxy velocity that ledge-edge
            // collision normals may have introduced despite our downward velocityMod.
            RE::hkVector4 vel;
            a_controller->GetLinearVelocityImpl(vel);
            if (vel.quad.m128_f32[2] > 0.0f) {
                vel.quad.m128_f32[2] = 0.0f;
                a_controller->SetLinearVelocityImpl(vel);
            }
        }
    }

    void SimulateStatePhysics_Air_Hook(RE::bhkCharacterStateInAir* a_this, RE::bhkCharacterController* a_controller)
    {
        if (a_controller && CounterLungeState::IsActive()) {
            CounterLungeState::ApplyVelocity(a_controller, a_controller->stepInfo.deltaTime);
        }

        g_originalSimulateAir(a_this, a_controller);

        if (a_controller && CounterLungeState::IsActive()) {
            RE::hkVector4 vel;
            a_controller->GetLinearVelocityImpl(vel);
            if (vel.quad.m128_f32[2] > 0.0f) {
                vel.quad.m128_f32[2] = 0.0f;
                a_controller->SetLinearVelocityImpl(vel);
            }
        }
    }
    
    void Install()
    {
        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_bhkCharacterStateOnGround[0] };
        g_originalSimulate = vtbl.write_vfunc(8, SimulateStatePhysics_Hook);

        REL::Relocation<std::uintptr_t> vtblAir{ RE::VTABLE_bhkCharacterStateInAir[0] };
        g_originalSimulateAir = vtblAir.write_vfunc(8, SimulateStatePhysics_Air_Hook);
        
        logger::info("Lunge physics hooks installed (ground + air)");
    }
}

//=============================================================================
// Counter Slow Time State - slows time during counter attack animation
//=============================================================================

void CounterSlowTimeState::Arm()
{
    auto* settings = Settings::GetSingleton();
    if (!settings->bEnableCounterSlowTime) {
        return;
    }
    
    waitingForStartEvent = true;
    waitingForEndEvent = false;
    active = false;
    
    // Set max timeout
    maxEndTime = std::chrono::steady_clock::now() + 
        std::chrono::milliseconds(static_cast<long long>(settings->fCounterSlowTimeMaxDuration * 1000.0f + 
                                                         settings->fCounterAttackWindow * 1000.0f));
    
    if (settings->bDebugLogging) {
        if (settings->bCounterSlowStartAfterLunge) {
            logger::info("[COUNTER SLOWTIME] Armed - will start after lunge ends");
        } else {
            logger::info("[COUNTER SLOWTIME] Armed - waiting for '{}' event", settings->sCounterSlowStartEvent);
        }
    }
}

void CounterSlowTimeState::Start()
{
    auto* settings = Settings::GetSingleton();
    
    waitingForStartEvent = false;
    waitingForEndEvent = true;
    active = true;
    
    // Update max end time from when slow time starts
    maxEndTime = std::chrono::steady_clock::now() + 
        std::chrono::milliseconds(static_cast<long long>(settings->fCounterSlowTimeMaxDuration * 1000.0f));
    
    // Apply slow time
    SKSE::GetTaskInterface()->AddTask([scale = settings->fCounterSlowTimeScale]() {
        static REL::Relocation<float*> gtm{ RELOCATION_ID(511883, 388443) };
        *gtm = scale;
    });
    
    if (settings->bDebugLogging) {
        logger::info("[COUNTER SLOWTIME] Started at {}% - waiting for '{}' event", 
            settings->fCounterSlowTimeScale * 100.0f, settings->sCounterSlowEndEvent);
        DebugNotify(DebugCategory::kCounter, "[TB] Slow time!");
    }
}

void CounterSlowTimeState::End()
{
    if (!active && !waitingForStartEvent && !waitingForEndEvent) {
        return;
    }
    
    auto* settings = Settings::GetSingleton();
    
    waitingForStartEvent = false;
    waitingForEndEvent = false;
    active = false;
    
    // Restore normal time
    SKSE::GetTaskInterface()->AddTask([]() {
        static REL::Relocation<float*> gtm{ RELOCATION_ID(511883, 388443) };
        *gtm = 1.0f;
    });
    
    if (settings->bDebugLogging) {
        logger::info("[COUNTER SLOWTIME] Ended");
    }
}

void CounterSlowTimeState::Update()
{
    if (!waitingForStartEvent && !waitingForEndEvent && !active) {
        return;
    }
    
    // Check timeout
    auto now = std::chrono::steady_clock::now();
    if (now >= maxEndTime) {
        auto* settings = Settings::GetSingleton();
        if (settings->bDebugLogging) {
            logger::info("[COUNTER SLOWTIME] Timeout reached");
        }
        End();
    }
}

void CounterSlowTimeState::OnAnimEvent(const std::string_view& eventName)
{
    auto* settings = Settings::GetSingleton();
    
    // Only process START events if we're NOT using lunge trigger
    if (waitingForStartEvent && !settings->bCounterSlowStartAfterLunge) {
        // Check if this is the start event (case-insensitive contains)
        std::string eventLower(eventName);
        std::transform(eventLower.begin(), eventLower.end(), eventLower.begin(), ::tolower);
        std::string startLower = settings->sCounterSlowStartEvent;
        std::transform(startLower.begin(), startLower.end(), startLower.begin(), ::tolower);
        
        if (eventLower.find(startLower) != std::string::npos) {
            if (settings->bDebugLogging) {
                logger::info("[COUNTER SLOWTIME] Start event '{}' matched", eventName);
            }
            Start();
        }
    }
    // END events are always processed (regardless of how we started)
    else if (waitingForEndEvent && active) {
        // Check if this is the end event
        std::string eventLower(eventName);
        std::transform(eventLower.begin(), eventLower.end(), eventLower.begin(), ::tolower);
        std::string endLower = settings->sCounterSlowEndEvent;
        std::transform(endLower.begin(), endLower.end(), endLower.begin(), ::tolower);
        
        if (eventLower.find(endLower) != std::string::npos) {
            if (settings->bDebugLogging) {
                logger::info("[COUNTER SLOWTIME] End event '{}' matched", eventName);
            }
            End();
        }
    }
}

bool CounterSlowTimeState::IsActive()
{
    return active;
}

//=============================================================================
// Counter Animation Event Handler - listens for attack animation events
//=============================================================================

CounterAnimEventHandler* CounterAnimEventHandler::GetSingleton()
{
    static CounterAnimEventHandler singleton;
    return &singleton;
}

void CounterAnimEventHandler::Register()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player) {
        player->AddAnimationGraphEventSink(GetSingleton());
        logger::info("Counter attack animation event handler registered");
    }
}

RE::BSEventNotifyControl CounterAnimEventHandler::ProcessEvent(
    const RE::BSAnimationGraphEvent* a_event,
    RE::BSTEventSource<RE::BSAnimationGraphEvent>*)
{
    if (!a_event || !a_event->tag.data() || a_event->tag.empty()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    // Check for dodge animation events (timed dodge detection)
    TimedDodgeState::OnAnimEvent(a_event->tag.c_str());
    
    // Forward animation events to the counter slow time state
    CounterSlowTimeState::OnAnimEvent(a_event->tag.c_str());

    // Detect spell fire during ward counter window so the damage bonus
    // persists until the projectile actually lands on an enemy.
    if (CounterAttackState::fromWardTimedBlock && CounterAttackState::damageBonusActive) {
        const auto& tag = a_event->tag;
        if (tag == "MRh_SpellFire_Event" || tag == "MLh_SpellFire_Event" ||
            tag == "MRh_Spell_Fire"      || tag == "MLh_Spell_Fire") {
            CounterAttackState::OnSpellFired();
        }
    }

    return RE::BSEventNotifyControl::kContinue;
}

namespace
{
	// Ward effects carry the "MagicWard" keyword (KWDA).  Using HasKeywordString reads
	// the KWDA array directly — no external lookup required, works for vanilla and mods.
	static bool EffectIsWard(RE::EffectSetting* mgef)
	{
		if (!mgef) {
			return false;
		}
		// Fast path: cached pointer (set during InitWardKeyword, may be null if lookup failed)
		if (WardTimedBlockState::wardKeyword && mgef->HasKeyword(WardTimedBlockState::wardKeyword)) {
			return true;
		}
		// Reliable fallback: scan the KWDA array by EditorID string — never needs a pointer
		return mgef->HasKeywordString("MagicWard");
	}

	bool MagicItemHasWardEffect(RE::MagicItem* a_spell)
	{
		if (!a_spell) {
			return false;
		}
		for (auto& eff : a_spell->effects) {
			if (eff && eff->baseEffect && EffectIsWard(eff->baseEffect)) {
				return true;
			}
		}
		return false;
	}

	bool IsDualCastWard(RE::Actor* a_player)
	{
		if (!a_player) {
			return false;
		}
		auto* left = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kLeftHand);
		auto* right = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kRightHand);
		const bool lw = left && MagicItemHasWardEffect(left->currentSpell);
		const bool rw = right && MagicItemHasWardEffect(right->currentSpell);
		return lw && rw;
	}

	float SumMagicDamageApprox(RE::MagicItem* a_magic)
	{
		if (!a_magic) {
			return 0.0f;
		}
		float sum = 0.0f;
		for (auto& eff : a_magic->effects) {
			if (eff && eff->baseEffect) {
				const auto arch = eff->baseEffect->data.archetype;
				if (arch == RE::EffectArchetypes::ArchetypeID::kValueModifier &&
					eff->baseEffect->data.primaryAV == RE::ActorValue::kHealth) {
					sum += eff->effectItem.magnitude;
				}
			}
		}
		if (sum <= 0.0f && !a_magic->effects.empty() && a_magic->effects[0]) {
			sum = a_magic->effects[0]->effectItem.magnitude;
		}
		return sum;
	}

	// Ward spell counter: concentration/direct spell (source) or magic projectile (spell on projectile)
	bool IsWardSpellCounterHit(const RE::TESHitEvent* a_event, bool& a_outSpell)
	{
		a_outSpell = false;
		if (!a_event) {
			return false;
		}
		if (a_event->source != 0) {
			auto* src = RE::TESForm::LookupByID(a_event->source);
			if (src && src->IsMagicItem()) {
				auto* mi = src->As<RE::MagicItem>();
				if (mi && !MagicItemHasWardEffect(mi)) {
					a_outSpell = true;
					return true;
				}
			}
		}
		if (a_event->projectile != 0) {
			RE::NiPointer<RE::TESObjectREFR> refr =
				RE::TESObjectREFR::LookupByHandle(static_cast<RE::RefHandle>(a_event->projectile));
			if (!refr) {
				return false;
			}
			auto* proj = refr->As<RE::Projectile>();
			if (!proj) {
				return false;
			}
			auto& rd = proj->GetProjectileRuntimeData();
			if (rd.spell) {
				a_outSpell = true;
				return true;
			}
		}
		return false;
	}
}  // namespace

//=============================================================================
// Ward timed block — window from ward MGEF apply; melee cancels damage + effects
//=============================================================================

void WardTimedBlockState::InitWardKeyword()
{
	// MagicWard vanilla KYWD full FormID = 0x0001EA6A (Skyrim.esm, load-order 0x00)
	wardKeyword = RE::TESForm::LookupByID<RE::BGSKeyword>(0x1EA6A);
	if (wardKeyword) {
		logger::info("[WARD TB] MagicWard keyword loaded via LookupByID (FormID {:08X})", wardKeyword->GetFormID());
		return;
	}
	// Fallback: TESDataHandler by plugin name
	auto* dh = RE::TESDataHandler::GetSingleton();
	if (dh) {
		wardKeyword = dh->LookupForm<RE::BGSKeyword>(0x1EA6A, "Skyrim.esm");
		if (wardKeyword) {
			logger::info("[WARD TB] MagicWard keyword loaded via TESDataHandler (FormID {:08X})", wardKeyword->GetFormID());
			return;
		}
	}
	// HasKeywordString("MagicWard") will be used as the primary check regardless — this is non-fatal
	logger::warn("[WARD TB] MagicWard keyword pointer lookup failed — HasKeywordString fallback will be used");
}

bool WardTimedBlockState::IsInWindow()
{
	return inWindow;
}

bool WardTimedBlockState::IsOnCooldown()
{
	if (!onCooldown) {
		return false;
	}
	return std::chrono::steady_clock::now() < cooldownEndTime;
}

void WardTimedBlockState::StartCooldown()
{
	auto* settings = Settings::GetSingleton();
	onCooldown = true;
	cooldownEndTime = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(static_cast<long long>(settings->fWardTimedBlockCooldown * 1000.0f));
}

void WardTimedBlockState::Update()
{
	const auto now = std::chrono::steady_clock::now();
	if (onCooldown && now >= cooldownEndTime) {
		onCooldown = false;
	}
	if (!inWindow) {
		return;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}
	// Keep health snapshot current so the TESHitEvent fallback path (non-Precision) can restore
	// health accurately even if the player healed between window open and the hit landing.
	auto* av = player->AsActorValueOwner();
	if (av) {
		const float hp = av->GetActorValue(RE::ActorValue::kHealth);
		if (hp > healthSnapshot) {
			healthSnapshot = hp;
		}
	}
	if (now >= windowEnd) {
		inWindow = false;
		StartCooldown();
		auto* settings = Settings::GetSingleton();
		if (settings->bDebugLogging) {
			logger::info("[WARD TB] Window expired without parry — cooldown started");
		}
	}
}

void WardTimedBlockState::RegisterPrecision()
{
	auto* api = reinterpret_cast<PRECISION_API::IVPrecision1*>(
		PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V1));

	if (!api) {
		logger::warn("[WARD TB] Precision not found — ward timed block will fall back to TESHitEvent (post-damage restore)."
		             " Install Precision for true pre-hit prevention.");
		return;
	}

	auto result = api->AddPreHitCallback(
		SKSE::GetPluginHandle(),
		[](const PRECISION_API::PrecisionHitData& hitData) -> PRECISION_API::PreHitCallbackReturn {
			PRECISION_API::PreHitCallbackReturn ret;  // bIgnoreHit defaults to false

			// Precision allows ONE PreHit callback per plugin handle, so this
			// single lambda handles BOTH the ward timed block and the regular
			// parry-window timed block. Ward is checked first because it has
			// a stricter gate (window must be open from a recent ward cast);
			// if it doesn't consume the hit we fall through to the parry-window
			// path below, which mirrors the post-hit TESHitEvent path but
			// cancels the hit at Havok level (no HP flicker, no restore).

			auto* settings = Settings::GetSingleton();
			if (!settings) return ret;

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || hitData.target != player) return ret;

			auto* attacker = hitData.attacker;
			if (!attacker || attacker == player) return ret;

			//------------------------------------------------------------
			// 1) Ward timed block (unchanged)
			//------------------------------------------------------------
			if (settings->bEnableWardTimedBlock && inWindow) {
				bool wardCovers = true;

				// Vanilla wards cover the front 180° unless overridden.
				if (!settings->bWardOmnidirectional) {
					const float yaw = player->GetAngle().z;
					const RE::NiPoint3 playerFwd{ std::sin(yaw), std::cos(yaw), 0.0f };
					RE::NiPoint3 toAttacker = attacker->GetPosition() - player->GetPosition();
					toAttacker.z = 0.0f;
					const float lenSq = toAttacker.SqrLength();
					if (lenSq > 0.001f) {
						toAttacker /= std::sqrt(lenSq);
					}
					if (playerFwd.Dot(toAttacker) < 0.0f) {
						wardCovers = false;
						if (settings->bDebugLogging) {
							DebugNotify(DebugCategory::kWard, "[WARD] Hit from behind — not parried (non-omni)");
						}
					}
				}

				if (wardCovers) {
					const bool parried = OnMeleeHit(player, attacker);
					if (settings->bDebugLogging) {
						logger::info("[WARD TB] Precision PreHit: attacker='{}', parried={}", attacker->GetName(), parried);
					}
					if (parried) {
						ret.bIgnoreHit = true;
						return ret;
					}
					// Ward rejected the hit (e.g. 2H rule) — fall through and
					// let the regular parry-window path get a shot at it.
				}
			}

			//------------------------------------------------------------
			// 2) Regular parry-window timed block.
			//
			//    1.0.4: switched from `bIgnoreHit = true` (which cancelled
			//    the entire hit, including the vanilla block animation /
			//    block sound / stamina drain / kHitBlocked flag /
			//    TESHitEvent — the player visually saw "nothing happened")
			//    to a `PreHitModifier{Damage × 0}`. With bIgnoreHit left
			//    false the engine processes the block 100% normally and
			//    TESHitEvent fires with kHitBlocked; the modifier just
			//    multiplies the incoming damage by 0 so the player loses
			//    0 HP regardless of whether they have Shield-of-Stamina
			//    style mods that already zero blocked damage.
			//
			//    Gating mirrors the v1.0.0 ShouldPreventDamage helper:
			//      - parry-window MGEF active on player
			//      - player is blocking
			//      - shield (bOnlyWithShield)
			//      - perk (bPerkLockedBlock)
			//
			//    Effects (slowmo / sound / counter window) are fired by
			//    the existing TESHitEvent handler in
			//    TimedBlockAddon::ProcessEvent on the resulting kHitBlocked
			//    event — NOT here — so cooldown / exclusion / perk-effect
			//    gating stays consistent across the Precision and
			//    no-Precision paths. The TESHitEvent HP-restore path will
			//    observe damageTaken ≈ 0 and no-op.
			//------------------------------------------------------------
			if (!CooldownState::GetParryEffectCached()) {
				TimedDodgeState::OnMeleeWeaponHitContact();
				return ret;
			}
			if (!Offsets::IsBlocking(player)) {
				TimedDodgeState::OnMeleeWeaponHitContact();
				return ret;
			}
			if (settings->bOnlyWithShield && !ActorHasShieldEquipped(player)) {
				TimedDodgeState::OnMeleeWeaponHitContact();
				return ret;
			}
			auto* addon = TimedBlockAddon::GetSingleton();
			if (settings->bPerkLockedBlock) {
				auto* perk = addon ? addon->damagePreventPerk : nullptr;
				if (!perk || !player->HasPerk(perk)) {
					TimedDodgeState::OnMeleeWeaponHitContact();
					return ret;
				}
			}

			// Zero the damage but let the hit through so the engine plays
			// the vanilla block animation/sound and TESHitEvent fires.
			PRECISION_API::PreHitModifier dmgZero;
			dmgZero.modifierType = PRECISION_API::PreHitModifier::ModifierType::Damage;
			dmgZero.modifierOperation = PRECISION_API::PreHitModifier::ModifierOperation::Multiplicative;
			dmgZero.modifierValue = 0.0f;
			ret.modifiers.push_back(dmgZero);
			// ret.bIgnoreHit stays false — engine processes the block normally.

			if (settings->bDebugLogging) {
				logger::info("[TB Precision] Parry-window damage zeroed (block animation/sound preserved) — attacker='{}'",
					attacker->GetName());
			}

			TimedDodgeState::OnMeleeWeaponHitContact();
			return ret;
		});

	if (result == PRECISION_API::APIResult::OK) {
		g_precisionAvailable = true;
		logger::info("[WARD TB] Precision PreHit callback registered — hitbox-level ward + parry-window prevention active");
	} else {
		logger::warn("[WARD TB] Precision AddPreHitCallback failed (result={})", static_cast<uint8_t>(result));
	}
}

void WardTimedBlockState::OnWardActivated(bool dualCast)
{
	auto* settings = Settings::GetSingleton();
	if (!settings->bEnableWardTimedBlock) {
		return;
	}
	if (WindowExclusion::IsBlocked()) {
		if (settings->bDebugLogging) {
			logger::info("[WARD TB] Skipped — another window activated too recently");
		}
		return;
	}
	if (IsOnCooldown()) {
		// Mirror the regular timed block rule: if there are no enemies in combat nearby,
		// ignore the cooldown entirely (same distance threshold, same cache).
		// If enemies ARE nearby, restart the cooldown timer to punish spam-casting.
		bool shouldIgnore = !CooldownState::GetNearbyEnemyCached();
		if (shouldIgnore) {
			if (settings->bDebugLogging) {
				logger::info("[WARD TB] On cooldown but no enemies nearby — ignoring cooldown");
			}
		} else {
			StartCooldown();
			if (settings->bDebugLogging) {
				logger::info("[WARD TB] Ward cast during cooldown — cooldown RESTARTED");
				DebugNotify(DebugCategory::kWard, "[WARD] Cooldown — blocked");
			}
			return;
		}
	}

	auto* player = RE::PlayerCharacter::GetSingleton();
	if (!player) {
		return;
	}

	// Window opens exactly once per cooldown cycle. The concentration spell ticks
	// every ~250ms, but we must NOT refresh windowEnd on each tick — that would
	// make the window perpetually open while the ward is held.
	if (inWindow) {
		// Already open — keep health snapshot current for the TESHitEvent fallback path.
		auto* av = player->AsActorValueOwner();
		if (av) {
			const float hp = av->GetActorValue(RE::ActorValue::kHealth);
			if (hp > healthSnapshot) healthSnapshot = hp;
		}
		return;
	}

	isDualCast = dualCast;
	auto* av = player->AsActorValueOwner();
	healthSnapshot = av ? av->GetActorValue(RE::ActorValue::kHealth) : 0.0f;
	inWindow = true;
	WindowExclusion::Stamp();
	windowEnd = std::chrono::steady_clock::now() +
		std::chrono::milliseconds(static_cast<long long>(settings->fWardTimedBlockWindowMs));

	if (settings->bDebugLogging) {
		logger::info("[WARD TB] Window opened (dualCast={}, {:.0f}ms, precision={})",
			dualCast, settings->fWardTimedBlockWindowMs, g_precisionAvailable);
		DebugNotify(DebugCategory::kWard, dualCast ? "[WARD] 2H ward active — parry open" : "[WARD] Ward active — parry open");
	}
}

bool WardTimedBlockState::OnMeleeHit(RE::Actor* defender, RE::Actor* attacker)
{
	if (!inWindow) {
		return false;
	}

	auto* settings = Settings::GetSingleton();

	// Optional: require a dual-cast (2H) ward to parry two-handed weapon attacks
	if (settings->bWardRequire2HForTwoHanders && !isDualCast && attacker) {
		auto* weapon = attacker->GetEquippedObject(false);
		if (weapon && weapon->IsWeapon()) {
			auto* weap = weapon->As<RE::TESObjectWEAP>();
			if (weap) {
				const auto type = weap->GetWeaponType();
				if (type == RE::WEAPON_TYPE::kTwoHandSword || type == RE::WEAPON_TYPE::kTwoHandAxe) {
					inWindow = false;
					logger::info("[WARD TB] 2H weapon attack — 1H ward insufficient (bWardRequire2HForTwoHanders=true)");
					if (settings->bDebugLogging) {
						DebugNotify(DebugCategory::kWard, "[WARD TB] Need dual-cast ward for this attack");
					}
					return false;  // Rejected — hit should proceed normally
				}
			}
		}
	}

	inWindow = false;

	// Fallback damage cancel: when Precision is not available the hit fires through
	// TESHitEvent (post-damage), so we restore health here.  When Precision IS available
	// the hit is cancelled at the Havok level before any health loss, so this is a no-op.
	if (settings->bWardTimedBlockDamageCancel && defender) {
		auto* avOwner = defender->AsActorValueOwner();
		if (avOwner) {
			const float cur = avOwner->GetActorValue(RE::ActorValue::kHealth);
			if (cur < healthSnapshot) {
				avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, healthSnapshot - cur);
			}
		}
	}

	lastAttackerHandle = attacker ? attacker->GetHandle() : RE::ActorHandle{};

	auto* addon = TimedBlockAddon::GetSingleton();
	addon->ApplyWardTimedBlockEffects(defender, attacker, isDualCast);
	CounterAttackState::StartWardWindow(attacker);

	// Successful parry — clear cooldown (consecutive ward parries are allowed, same rule as regular timed block).
	onCooldown = false;

	if (settings->bDebugLogging) {
		logger::info("[WARD TB] Melee parry — attacker='{}', dualCast={}, precision={}, cooldown CLEARED",
			attacker ? attacker->GetName() : "?", isDualCast, g_precisionAvailable);
	}

	return true;  // Parry consumed — Precision should ignore the hit
}

void WardTimedBlockState::PlayWardTimedBlockSound()
{
	auto* settings = Settings::GetSingleton();
	if (!settings->bWardTimedBlockSound) {
		return;
	}
	const float vol = settings->fWardTimedBlockSoundVolume;
	const std::string file = settings->sWardTimedBlockSoundFile;

	SKSE::GetTaskInterface()->AddTask([vol, file]() {
		std::filesystem::path wavPath = std::filesystem::current_path();
		wavPath /= "Data";
		wavPath /= "SKSE";
		wavPath /= "Plugins";
		wavPath /= "TimedBlockDodgeCounter";
		wavPath /= file;

		if (std::filesystem::exists(wavPath)) {
			auto buf = LoadWAVWithVolume(wavPath, vol);
			if (!buf) {
				logger::error("[WARD TB] Failed to load WAV: {}", wavPath.string());
				return;
			}
			g_wardTimedBlockAudioBuffer = buf;
			const BOOL ok = PlaySoundA(reinterpret_cast<LPCSTR>(buf->data()),
				NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
			if (!ok) {
				logger::error("[WARD TB] PlaySoundA failed for {}", wavPath.string());
			}
		} else {
			logger::warn("[WARD TB] WAV not found: {}", wavPath.string());
		}
	});
}

void WardTimedBlockState::PlayWardCounterSpellSound()
{
	auto* settings = Settings::GetSingleton();
	if (!settings->bWardCounterSpellSound) {
		return;
	}
	const float vol = settings->fWardCounterSpellSoundVolume;
	const std::string file = settings->sWardCounterSpellSoundFile;

	SKSE::GetTaskInterface()->AddTask([vol, file]() {
		std::filesystem::path wavPath = std::filesystem::current_path();
		wavPath /= "Data";
		wavPath /= "SKSE";
		wavPath /= "Plugins";
		wavPath /= "TimedBlockDodgeCounter";
		wavPath /= file;

		if (std::filesystem::exists(wavPath)) {
			auto buf = LoadWAVWithVolume(wavPath, vol);
			if (!buf) {
				logger::error("[WARD COUNTER SPELL] Failed to load WAV: {}", wavPath.string());
				return;
			}
			g_wardCounterSpellAudioBuffer = buf;
			const BOOL ok = PlaySoundA(reinterpret_cast<LPCSTR>(buf->data()),
				NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
			if (!ok) {
				logger::error("[WARD COUNTER SPELL] PlaySoundA failed for {}", wavPath.string());
			}
		} else {
			logger::warn("[WARD COUNTER SPELL] WAV not found: {}", wavPath.string());
		}
	});
}

WardEffectHandler* WardEffectHandler::GetSingleton()
{
	static WardEffectHandler singleton;
	return &singleton;
}

void WardEffectHandler::Register()
{
	WardTimedBlockState::InitWardKeyword();
	RE::ScriptEventSourceHolder* holder = RE::ScriptEventSourceHolder::GetSingleton();
	if (holder) {
		holder->AddEventSink(GetSingleton());
		logger::info("Ward effect handler (TESMagicEffectApplyEvent) registered");
	}
}

RE::BSEventNotifyControl WardEffectHandler::ProcessEvent(
	const RE::TESMagicEffectApplyEvent* a_event,
	RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*)
{
	if (!a_event) {
		return RE::BSEventNotifyControl::kContinue;
	}

	auto* target = a_event->target.get();
	auto* caster = a_event->caster.get();
	if (!target || !caster) {
		return RE::BSEventNotifyControl::kContinue;
	}

	auto* targetActor = target->As<RE::Actor>();
	auto* casterActor = caster->As<RE::Actor>();
	if (!targetActor || !casterActor || !targetActor->IsPlayerRef() || casterActor != targetActor) {
		return RE::BSEventNotifyControl::kContinue;
	}

	auto* mgef = RE::TESForm::LookupByID<RE::EffectSetting>(a_event->magicEffect);
	if (!mgef) {
		return RE::BSEventNotifyControl::kContinue;
	}

	auto* settings = Settings::GetSingleton();
	if (settings->bDebugLogging) {
		logger::info("[WARD TB] MagicEffectApply on player: '{}' (FormID {:08X}), archetype={}",
			mgef->GetFullName(), mgef->GetFormID(),
			static_cast<int>(mgef->data.archetype));
	}

	if (!EffectIsWard(mgef)) {
		return RE::BSEventNotifyControl::kContinue;
	}

	const bool dual = IsDualCastWard(targetActor);
	if (settings->bDebugLogging) {
		logger::info("[WARD TB] Ward effect detected via event: '{}', dual={}", mgef->GetFullName(), dual);
	}
	WardTimedBlockState::OnWardActivated(dual);
	return RE::BSEventNotifyControl::kContinue;
}

//=============================================================================
// Counter Damage Hit Handler - removes damage bonus after first hit
//=============================================================================

CounterDamageHitHandler* CounterDamageHitHandler::GetSingleton()
{
    static CounterDamageHitHandler singleton;
    return &singleton;
}

void CounterDamageHitHandler::Register()
{
    RE::ScriptEventSourceHolder* eventHolder = RE::ScriptEventSourceHolder::GetSingleton();
    if (eventHolder) {
        eventHolder->AddEventSink(GetSingleton());
        logger::info("Counter damage hit handler registered");
    }
}

RE::BSEventNotifyControl CounterDamageHitHandler::ProcessEvent(
    const RE::TESHitEvent* a_event,
    RE::BSTEventSource<RE::TESHitEvent>*)
{
    if (!CounterAttackState::IsDamageBonusActive()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    if (!a_event->cause) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    RE::Actor* player = a_event->cause->As<RE::Actor>();
    if (!player || !player->IsPlayerRef()) {
        return RE::BSEventNotifyControl::kContinue;
    }
    
    RE::Actor* target = a_event->target ? a_event->target->As<RE::Actor>() : nullptr;
    if (!target) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* settings = Settings::GetSingleton();

    bool isSpellCounter = false;
    bool isRangedCounter = false;
    bool isProjectileHit = false;

    const bool spellCounterAllowed =
        (CounterAttackState::fromWardTimedBlock && settings->bWardCounterSpellHit) ||
        (CounterAttackState::fromTimedDodge && settings->bTimedDodgeCounterSpellHit &&
         CounterAttackState::spellFiredDuringWindow);

    const bool rangedCounterAllowed =
        CounterAttackState::rangedCounterActive && CounterAttackState::damageBonusActive;

    if (a_event->projectile) {
        // Ranged counter: arrow/bolt from player (non-spell source)
        if (rangedCounterAllowed) {
            auto* srcForm = a_event->source != 0 ? RE::TESForm::LookupByID(a_event->source) : nullptr;
            if (!srcForm || !srcForm->IsMagicItem()) {
                isRangedCounter = true;
                isProjectileHit = true;
            }
        }

        if (!isRangedCounter) {
            if (!spellCounterAllowed) {
                return RE::BSEventNotifyControl::kContinue;
            }
            bool sp = false;
            if (!IsWardSpellCounterHit(a_event, sp) || !sp) {
                return RE::BSEventNotifyControl::kContinue;
            }
            isSpellCounter = true;
            isProjectileHit = true;
        }
    } else if (spellCounterAllowed) {
        // Non-projectile hit (projectile==0).  Two possibilities:
        //   A) Concentration/beam spell — these NEVER produce projectiles, so
        //      projectile==0 IS the real hit.  Accept it.
        //   B) Fire-and-forget (charge-and-release) spell — Skyrim fires a
        //      TESHitEvent with projectile==0 at the instant the spell leaves the
        //      player's hand ("muzzle event").  The real impact arrives later as a
        //      separate event with projectile!=0.  ALWAYS reject it here.
        //
        // Distinguishing the two by scanning Projectile::Manager is unreliable
        // because the projectile may not have spawned yet on the same frame as the
        // muzzle event.  Instead we check the spell's CastingType directly — this
        // is authoritative and frame-independent.
        bool isConcentration = false;
        if (a_event->source != 0) {
            auto* src = RE::TESForm::LookupByID(a_event->source);
            if (src && src->IsMagicItem()) {
                auto* mi = src->As<RE::MagicItem>();
                if (mi) {
                    isConcentration =
                        (mi->GetCastingType() == RE::MagicSystem::CastingType::kConcentration);
                }
            }
        }

        if (isConcentration) {
            bool sp = false;
            if (IsWardSpellCounterHit(a_event, sp) && sp) {
                isSpellCounter = true;
            }
        }
        // Fire-and-forget with projectile==0 → muzzle event; the real projectile
        // impact will arrive via the projectile!=0 path above.
    }

    // If the hit came from a spell source but wasn't classified as a valid spell
    // counter, never fall through to the melee path — a spell hit should not
    // consume the melee counter bonus. (Projectile spell hits that aren't ward
    // counters already return early above; this guards the projectile==0 case.)
    if (!isSpellCounter && !isRangedCounter && a_event->source != 0) {
        auto* hitSrc = RE::TESForm::LookupByID(a_event->source);
        if (hitSrc && hitSrc->IsMagicItem()) {
            return RE::BSEventNotifyControl::kContinue;
        }
    }

    const bool fromWard = CounterAttackState::fromWardTimedBlock;
    const bool fromDodge = CounterAttackState::fromTimedDodge;

    float baseDamage = 0.0f;
    if (isRangedCounter) {
        auto* weapon = player->GetEquippedObject(false);
        if (weapon && weapon->IsWeapon()) {
            baseDamage = static_cast<float>(weapon->As<RE::TESObjectWEAP>()->GetAttackDamage());
        }
        // Add ammo damage from the projectile's runtime data
        if (a_event->projectile != 0) {
            RE::NiPointer<RE::TESObjectREFR> refr =
                RE::TESObjectREFR::LookupByHandle(static_cast<RE::RefHandle>(a_event->projectile));
            if (refr) {
                if (auto* proj = refr->As<RE::Projectile>()) {
                    auto& rd = proj->GetProjectileRuntimeData();
                    if (rd.ammoSource) {
                        auto& projData = rd.ammoSource->data;
                        baseDamage += static_cast<float>(projData.damage);
                    }
                }
            }
        }
        if (baseDamage <= 0.0f) {
            baseDamage = 15.0f;
        }
        float damageMult = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAttackDamageMult);
        baseDamage *= damageMult;
    } else if (isSpellCounter) {
        if (a_event->source != 0) {
            auto* src = RE::TESForm::LookupByID(a_event->source);
            if (src && src->IsMagicItem()) {
                baseDamage = SumMagicDamageApprox(src->As<RE::MagicItem>());
            }
        }
        if (baseDamage <= 0.0f && a_event->projectile != 0) {
            RE::NiPointer<RE::TESObjectREFR> refr =
                RE::TESObjectREFR::LookupByHandle(static_cast<RE::RefHandle>(a_event->projectile));
            if (refr) {
                if (auto* proj = refr->As<RE::Projectile>()) {
                    auto& rd = proj->GetProjectileRuntimeData();
                    if (rd.spell) {
                        baseDamage = SumMagicDamageApprox(rd.spell);
                    }
                }
            }
        }
        // For non-projectile (AoE explosion) hits, a_event->source is often an
        // internal sub-effect with magnitude ~1.0, not the actual SpellItem.
        // If the damage looks implausibly low, check the player's equipped spells —
        // the real SpellItem (which returns the correct magnitude) is still there.
        if (!isProjectileHit && baseDamage <= 2.0f) {
            for (bool lh : { false, true }) {
                auto* eq = player->GetEquippedObject(lh);
                if (eq && eq->IsMagicItem()) {
                    auto* mi = eq->As<RE::MagicItem>();
                    if (mi && !MagicItemHasWardEffect(mi)) {
                        float d = SumMagicDamageApprox(mi);
                        if (d > baseDamage) {
                            baseDamage = d;
                        }
                    }
                }
            }
        }
        if (baseDamage <= 0.0f) {
            baseDamage = 20.0f;
        }
    } else {
        auto* weapon = player->GetEquippedObject(false);
        if (weapon && weapon->IsWeapon()) {
            auto* weap = weapon->As<RE::TESObjectWEAP>();
            if (weap) {
                baseDamage = static_cast<float>(weap->GetAttackDamage());
            }
        }
        if (baseDamage <= 0.0f) {
            baseDamage = 10.0f;
        }

        float damageMult = player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kAttackDamageMult);
        baseDamage *= damageMult;
    }

    float bonusPercent = CounterAttackState::appliedDamageBonus / 100.0f;
    float bonusDamage = baseDamage * bonusPercent;

    // Clear bonus BEFORE casting — CastSpellImmediate is synchronous and will
    // re-enter this handler via a new TESHitEvent; the guard at the top must
    // see damageBonusActive == false to prevent infinite recursion.
    CounterAttackState::RemoveDamageBonus();

    if (bonusDamage > 0.0f) {
        if ((fromWard || fromDodge) && (isSpellCounter || isRangedCounter) && settings->bEnableCounterSlowTime) {
            CounterSlowTimeState::End();
            CounterSlowTimeState::Start();
        }

        bool castOk = false;

        if (CounterAttackState::counterSpell && !CounterAttackState::counterSpell->effects.empty()) {
            RE::Effect* eff = CounterAttackState::counterSpell->effects[0];
            if (eff) {
                eff->effectItem.magnitude = bonusDamage;

                auto* caster = player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
                if (caster) {
                    caster->CastSpellImmediate(
                        CounterAttackState::counterSpell,
                        true,
                        target,
                        1.0f,
                        false,
                        0.0f,
                        player);
                    castOk = true;
                }
            }
        }

        if (!castOk) {
            target->AsActorValueOwner()->RestoreActorValue(
                RE::ACTOR_VALUE_MODIFIER::kDamage,
                RE::ActorValue::kHealth,
                -bonusDamage);
        }

        const char* hitType = isRangedCounter ? "ranged" : (isSpellCounter ? "spell" : "melee");
        logger::info("[COUNTER DAMAGE] Hit '{}': base ~{:.1f}, +{:.0f}% = +{:.1f} bonus damage (type={}, cast={})",
            target->GetName(), baseDamage, bonusPercent * 100.0f,
            bonusDamage, hitType, castOk ? "yes" : "fallback");
        spdlog::default_logger()->flush();

        if (settings->bDebugLogging) {
            DebugNotify(DebugCategory::kCounter, fmt::format("[TB] Counter! +{:.0f} damage", bonusDamage).c_str());
        }

        if (isRangedCounter && settings->bTimedDodgeCounterRangedSound) {
            PlayCounterStrikeSound();
        } else if (isSpellCounter && settings->bWardCounterSpellSound) {
            WardTimedBlockState::PlayWardCounterSpellSound();
        } else if (!isSpellCounter && !isRangedCounter) {
            PlayCounterStrikeSound();
        }
    }

    if (fromWard) {
        CounterAttackState::inWindow = false;
        CounterAttackState::fromWardTimedBlock = false;
    }

    if (isRangedCounter) {
        CounterAttackState::RemoveDrawSpeedBuff();
        CounterAttackState::rangedCounterActive = false;
        CounterAttackState::fromTimedDodge = false;
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

//=============================================================================
// Timed Dodge State - Perfect dodge triggers slow-mo, i-frames, radial blur
//=============================================================================

bool TimedDodgeState::IsDodgeEvent(const char* eventName)
{
    if (!eventName) return false;
    
    // Known dodge animation events from popular dodge mods
    static const char* dodgeEvents[] = {
        "MCO_DodgeInitiate",     // DMCO / MCO
        "TKDR_DodgeStart",       // TK Dodge RE
        "SidestepTrigger",       // Ultimate Dodge (sidestep)
        "RollTrigger",           // Ultimate Dodge (roll)
        "DodgeStart",            // Generic dodge event
    };
    
    for (const auto& evt : dodgeEvents) {
        if (std::strcmp(eventName, evt) == 0) {
            return true;
        }
    }
    
    return false;
}

void TimedDodgeState::OnAnimEvent(const char* eventName)
{
    if (!IsDodgeEvent(eventName)) return;
    
    auto* settings = Settings::GetSingleton();
    if (!settings->bEnableTimedDodge) return;
    
    if (settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Dodge event detected: {}", eventName);
    }
    
    // Mark that the player is in a dodge animation for the hit-during-dodge window.
    // If the player gets hit during this window, it retroactively triggers a timed dodge.
    if (settings->fTimedDodgeHitWindowMs > 0.0f) {
        inDodgeAnimation = true;
        dodgeAnimationExpiry = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeHitWindowMs));
    }
    
    // If timed dodge is already active, cancel it (another dodge cancels slomo)
    if (slomoActive) {
        if (settings->bDebugLogging) {
            logger::info("[TIMED DODGE] Cancelling active timed dodge via another dodge");
            DebugNotify(DebugCategory::kDodge, "[TD] Dodge cancelled slomo");
        }
        End();
        return;
    }
    
    OnDodgeEvent();
}

void TimedDodgeState::OnDodgeEvent()
{
    auto* settings = Settings::GetSingleton();

    if (WindowExclusion::IsBlocked()) {
        if (settings->bDebugLogging) {
            logger::info("[TIMED DODGE] Skipped — another window activated too recently");
        }
        return;
    }
    
    if (IsOnCooldown()) {
        if (settings->bDebugLogging) {
            auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                cooldownEndTime - std::chrono::steady_clock::now()).count();
            logger::info("[TIMED DODGE] On cooldown ({}ms remaining)", remainingMs);
        }
        return;
    }

    // Damage cooldown: player took a hit too recently to timed dodge
    if (onDamageCooldown) {
        auto now = std::chrono::steady_clock::now();
        if (now < damageCooldownEndTime) {
            if (settings->bDebugLogging) {
                auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    damageCooldownEndTime - now).count();
                logger::info("[TIMED DODGE] Blocked by damage cooldown ({}ms remaining)", remainingMs);
            }
            return;
        }
        onDamageCooldown = false;
    }

    if (onHitContactCooldown) {
        auto nowHit = std::chrono::steady_clock::now();
        if (nowHit < hitContactCooldownEndTime) {
            if (settings->bDebugLogging) {
                auto remainingHitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    hitContactCooldownEndTime - nowHit).count();
                logger::info("[TIMED DODGE] Blocked by melee hit-contact cooldown ({}ms remaining)", remainingHitMs);
            }
            return;
        }
        onHitContactCooldown = false;
    }
    
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    RE::Actor* attacker = FindAttackingEnemyInRange(player, settings->fTimedDodgeDetectionRange);
    if (attacker) {
        logger::info("[TIMED DODGE] SUCCESS! Enemy '{}' was in attack swing phase within range",
            attacker->GetName());
        pendingDodge = false;
        Start(attacker);
        return;
    }
    
    // No attacker found yet — buffer the dodge for the forgiveness window
    if (settings->fTimedDodgeForgivenessMs > 0.0f) {
        pendingDodge = true;
        pendingDodgeExpiry = std::chrono::steady_clock::now() +
            std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeForgivenessMs));
        if (settings->bDebugLogging) {
            logger::info("[TIMED DODGE] No attacker yet, buffering dodge for {:.0f}ms", settings->fTimedDodgeForgivenessMs);
        }
    } else {
        if (settings->bDebugLogging) {
            logger::debug("[TIMED DODGE] No attacking enemy in range ({:.0f} units)", settings->fTimedDodgeDetectionRange);
        }
    }
}

void TimedDodgeState::Start(RE::Actor* attacker)
{
    auto* settings = Settings::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return;
    
    active = true;
    slomoActive = true;
    inDodgeAnimation = false;
    pendingDodge = false;
    WindowExclusion::Stamp();
    
    auto now = std::chrono::steady_clock::now();
    effectStartTime = now;
    effectEndTime = now + std::chrono::milliseconds(
        static_cast<long long>(settings->fTimedDodgeSlomoDuration * 1000.0f));
    lastBlurUpdateTime = now;
    
    // Store attacker
    if (attacker) {
        attackerHandle = attacker->GetHandle();
    }
    
    // Start slow-motion via global time multiplier
    SKSE::GetTaskInterface()->AddTask([speed = settings->fTimedDodgeSlomoSpeed]() {
        static REL::Relocation<float*> gtm{ RELOCATION_ID(511883, 388443) };
        *gtm = speed;
    });
    
    // Arm extended i-frames (dodge animation's own i-frames play out first via MaxsuIFrame,
    // then we take over with graph variables for the remaining slomo duration)
    if (settings->bTimedDodgeIframes) {
        iframesActive = true;
        dodgeIframesEnded = false;
        counterWindowOpened = false;
    }
    
    // Start radial blur (set target, let Update() handle fade-in)
    if (settings->bTimedDodgeRadialBlur) {
        targetBlurStrength = settings->fTimedDodgeBlurStrength;
    }
    
    // Start cooldown immediately (not stackable)
    StartCooldown();
    
    // Open counter attack window (ends early when fTimedDodgeCounterWindowMs elapses, or slomo ends—whichever is first)
    if (settings->bTimedDodgeCounterAttack) {
        CounterAttackState::inWindow = true;
        CounterAttackState::fromTimedDodge = true;
        CounterAttackState::fromWardTimedBlock = false;
        CounterAttackState::spellFiredDuringWindow = false;
        CounterAttackState::rangedCounterActive = false;
        CounterAttackState::trackedSpellProjectile = {};
        CounterAttackState::projectileScanRetries = 0;
        CounterAttackState::windowEndTime = now + std::chrono::milliseconds(
            static_cast<long long>(settings->fTimedDodgeCounterWindowMs));
        if (attacker) {
            CounterAttackState::lastAttackerHandle = attacker->GetHandle();
        }
    }
    
    // Apply timed block visual effects on the attacker (hitstop, camera shake, stamina, etc.)
    if (settings->bTimedDodgeApplyBlockEffects) {
        auto* addon = TimedBlockAddon::GetSingleton();
        addon->ApplyTimedBlockEffects(player, attacker, true, true);
    }

    // Slow attacker's animation speed (per-actor, independent of global game speed)
    if (settings->bTimedDodgeAttackerSlow && attacker) {
        AnimSpeedManager::SetAnimSpeed(attacker->GetHandle(),
            settings->fTimedDodgeAttackerSlowSpeed,
            settings->fTimedDodgeAttackerSlowDuration);
    }

    // Play timed dodge sound
    if (settings->bTimedDodgeSound) {
        PlayDodgeSound();
    }
    
    if (settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Started: slomo={}s@{}%, iframes={}, blur={}, counter={}",
            settings->fTimedDodgeSlomoDuration, settings->fTimedDodgeSlomoSpeed * 100.0f,
            settings->bTimedDodgeIframes, settings->bTimedDodgeRadialBlur, settings->bTimedDodgeCounterAttack);
        DebugNotify(DebugCategory::kDodge, "[TD] Timed Dodge!");
    }
}

void TimedDodgeState::End()
{
    auto* settings = Settings::GetSingleton();
    
    // Restore normal game speed immediately (not via AddTask, so lunge/counter starts at full speed)
    if (slomoActive) {
        static REL::Relocation<float*> gtm{ RELOCATION_ID(511883, 388443) };
        *gtm = 1.0f;
        slomoActive = false;
    }
    
    // End i-frames - only clear graph variables if we took over from the dodge
    if (iframesActive) {
        if (dodgeIframesEnded) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                player->SetGraphVariableBool("bIframeActive", false);
                player->SetGraphVariableBool("bInIframe", false);
            }
        }
        iframesActive = false;
        dodgeIframesEnded = false;
        counterWindowOpened = false;
    }
    
    // Start blur fade-out (let Update() handle the actual blending)
    targetBlurStrength = 0.0f;
    
    if (settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Ended (slomo restored, i-frames off, blur fading out)");
    }
}

void TimedDodgeState::Update()
{
    auto* settings = Settings::GetSingleton();
    if (!settings->bEnableTimedDodge) {
        if (active) {
            End();
            active = false;
            currentBlurStrength = 0.0f;
            if (blurEffectActive && dodgeImod) {
                RE::ImageSpaceModifierInstanceForm::Stop(dodgeImod);
                dodgeImodInstance = nullptr;
                blurEffectActive = false;
                if (dodgeImod->radialBlur.strength)  dodgeImod->radialBlur.strength->floatValue = originalBlurStrength;
                if (dodgeImod->radialBlur.rampUp)    dodgeImod->radialBlur.rampUp->floatValue   = originalBlurRampUp;
                if (dodgeImod->radialBlur.rampDown)  dodgeImod->radialBlur.rampDown->floatValue = originalBlurRampDown;
                if (dodgeImod->radialBlur.start)     dodgeImod->radialBlur.start->floatValue    = originalBlurStart;
            }
        }
        pendingDodge = false;
        inDodgeAnimation = false;
        return;
    }
    
    // Expire the hit-during-dodge window
    if (inDodgeAnimation && std::chrono::steady_clock::now() >= dodgeAnimationExpiry) {
        inDodgeAnimation = false;
    }
    
    // Check pending early-dodge buffer
    if (pendingDodge) {
        auto now = std::chrono::steady_clock::now();
        if (now >= pendingDodgeExpiry) {
            pendingDodge = false;
            if (settings->bDebugLogging) {
                logger::debug("[TIMED DODGE] Forgiveness window expired, no attacker entered swing");
            }
        } else {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                RE::Actor* attacker = FindAttackingEnemyInRange(player, settings->fTimedDodgeDetectionRange);
                if (attacker) {
                    pendingDodge = false;
                    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - (pendingDodgeExpiry -
                        std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeForgivenessMs)))).count();
                    logger::info("[TIMED DODGE] Forgiveness hit! Enemy '{}' entered swing {}ms after dodge",
                        attacker->GetName(), elapsedMs);
                    Start(attacker);
                }
            }
        }
    }

    if (!active) return;
    
    auto now = std::chrono::steady_clock::now();
    
    // Calculate real-time delta for blur blending (not affected by game slow-mo)
    float realDelta = std::chrono::duration<float>(now - lastBlurUpdateTime).count();
    lastBlurUpdateTime = now;
    realDelta = std::clamp(realDelta, 0.0f, 0.1f);  // Cap to avoid huge jumps
    
    // Check if slomo duration has expired
    if (slomoActive && now >= effectEndTime) {
        if (settings->bDebugLogging) {
            logger::info("[TIMED DODGE] Slomo duration expired");
        }
        End();
    }
    
    // Extended i-frames: let the dodge animation's own i-frames finish first,
    // then we take over with graph variables for the remaining slomo duration
    if (iframesActive) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player) {
            if (!dodgeIframesEnded) {
                bool bInIframe = false;
                player->GetGraphVariableBool("bInIframe", bInIframe);
                if (!bInIframe) {
                    dodgeIframesEnded = true;
                    auto* avOwner = player->AsActorValueOwner();
                    if (avOwner) {
                        trackedHealth = avOwner->GetActorValue(RE::ActorValue::kHealth);
                    }
                    if (settings->bDebugLogging) {
                        logger::info("[TIMED DODGE] Dodge i-frames ended, taking over with extended i-frames");
                    }
                }
            }
            
            if (dodgeIframesEnded) {
                player->SetGraphVariableBool("bIframeActive", true);
                player->SetGraphVariableBool("bInIframe", true);
            }
        }
    }
    
    // Update radial blur blending
    if (dodgeImod && dodgeImod->radialBlur.strength) {
        // Smoothly blend toward target blur strength using real-time delta
        float blendArg = settings->fTimedDodgeBlurBlendSpeed * realDelta;
        if (blendArg > 0.99f) blendArg = 0.99f;
        float blurBlendFactor = 1.0f - std::pow(1.0f - blendArg, 1.0f);
        currentBlurStrength = currentBlurStrength + 
            (targetBlurStrength - currentBlurStrength) * blurBlendFactor;
        
        // Snap to target when very close
        if (std::abs(currentBlurStrength - targetBlurStrength) < 0.005f) {
            currentBlurStrength = targetBlurStrength;
        }
        
        // Update IMOD parameters
        dodgeImod->radialBlur.strength->floatValue = currentBlurStrength;
        if (dodgeImod->radialBlur.rampUp) {
            dodgeImod->radialBlur.rampUp->floatValue = settings->fTimedDodgeBlurRampUp;
        }
        if (dodgeImod->radialBlur.rampDown) {
            dodgeImod->radialBlur.rampDown->floatValue = settings->fTimedDodgeBlurRampDown;
        }
        if (dodgeImod->radialBlur.start) {
            dodgeImod->radialBlur.start->floatValue = settings->fTimedDodgeBlurRadius;
        }
        
        // Trigger IMOD when blur strength rises above threshold
        if (currentBlurStrength > 0.01f) {
            if (!blurEffectActive) {
                dodgeImodInstance = RE::ImageSpaceModifierInstanceForm::Trigger(dodgeImod, 1.0f, nullptr);
                blurEffectActive = true;
                if (settings->bDebugLogging) {
                    logger::info("[TIMED DODGE] Radial blur activated (strength: {:.2f})", currentBlurStrength);
                }
            }
        } else if (blurEffectActive) {
            // Stop IMOD and restore original values
            RE::ImageSpaceModifierInstanceForm::Stop(dodgeImod);
            dodgeImodInstance = nullptr;
            blurEffectActive = false;
            dodgeImod->radialBlur.strength->floatValue = originalBlurStrength;
            if (dodgeImod->radialBlur.rampUp)   dodgeImod->radialBlur.rampUp->floatValue   = originalBlurRampUp;
            if (dodgeImod->radialBlur.rampDown) dodgeImod->radialBlur.rampDown->floatValue = originalBlurRampDown;
            if (dodgeImod->radialBlur.start)    dodgeImod->radialBlur.start->floatValue    = originalBlurStart;
            if (settings->bDebugLogging) {
                logger::info("[TIMED DODGE] Radial blur deactivated (originals restored)");
            }
        }
    }
    
    // Deactivate the timed dodge entirely once slomo is done AND blur has faded out
    if (!slomoActive && currentBlurStrength <= 0.01f) {
        active = false;
        if (settings->bDebugLogging) {
            logger::info("[TIMED DODGE] Fully deactivated (blur fade-out complete)");
        }
    }
}

bool TimedDodgeState::IsActive()
{
    return active;
}

bool TimedDodgeState::IsSlomoActive()
{
    return slomoActive;
}

bool TimedDodgeState::IsOnCooldown()
{
    if (!onCooldown) return false;
    
    auto now = std::chrono::steady_clock::now();
    if (now >= cooldownEndTime) {
        onCooldown = false;
        return false;
    }
    return true;
}

void TimedDodgeState::StartCooldown()
{
    auto* settings = Settings::GetSingleton();
    onCooldown = true;
    cooldownEndTime = std::chrono::steady_clock::now() + 
        std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeCooldown * 1000.0f));
    
    if (settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Cooldown started ({:.1f}s)", settings->fTimedDodgeCooldown);
    }
}

void TimedDodgeState::OnPlayerDamaged()
{
    auto* settings = Settings::GetSingleton();
    if (settings->fTimedDodgeDamageCooldown <= 0.0f) return;

    onDamageCooldown = true;
    damageCooldownEndTime = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeDamageCooldown * 1000.0f));

    if (settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Damage cooldown started ({:.1f}s)", settings->fTimedDodgeDamageCooldown);
        DebugNotify(DebugCategory::kDodge, "[TD] Damage cooldown!");
    }
}

void TimedDodgeState::OnMeleeWeaponHitContact()
{
    if (active || slomoActive || inDodgeAnimation) {
        return;
    }

    auto* settings = Settings::GetSingleton();
    if (!settings || !settings->bEnableTimedDodge || settings->fTimedDodgeHitContactCooldown <= 0.0f) {
        return;
    }

    onHitContactCooldown = true;
    hitContactCooldownEndTime = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeHitContactCooldown * 1000.0f));
    pendingDodge = false;

    if (settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Melee hit-contact cooldown started ({:.2f}s)", settings->fTimedDodgeHitContactCooldown);
    }
}

bool TimedDodgeState::TryTriggerFromHit(RE::Actor* attacker)
{
    if (!inDodgeAnimation || active || slomoActive) {
        return false;
    }

    auto now = std::chrono::steady_clock::now();
    if (now >= dodgeAnimationExpiry) {
        inDodgeAnimation = false;
        return false;
    }

    auto* settings = Settings::GetSingleton();
    if (!settings->bEnableTimedDodge) {
        return false;
    }

    if (WindowExclusion::IsBlocked()) {
        return false;
    }

    if (IsOnCooldown()) {
        return false;
    }

    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - (dodgeAnimationExpiry -
        std::chrono::milliseconds(static_cast<long long>(settings->fTimedDodgeHitWindowMs)))).count();

    logger::info("[TIMED DODGE] Hit-during-dodge! Attacker '{}' hit player {}ms into dodge — triggering timed dodge",
        attacker ? attacker->GetName() : "unknown", elapsedMs);
    DebugNotify(DebugCategory::kDodge, "[TD] Hit-during-dodge!");

    inDodgeAnimation = false;
    pendingDodge = false;
    Start(attacker);
    return true;
}

void TimedDodgeState::OnPlayerHit(RE::Actor* player)
{
    // Fallback only for our extended i-frame window (after dodge's own i-frames ended)
    if (!iframesActive || !dodgeIframesEnded || !player) return;
    
    auto* settings = Settings::GetSingleton();
    auto* avOwner = player->AsActorValueOwner();
    if (!avOwner) return;
    
    float currentHealth = avOwner->GetActorValue(RE::ActorValue::kHealth);
    
    if (currentHealth < trackedHealth) {
        float restoreAmount = trackedHealth - currentHealth;
        avOwner->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kHealth, restoreAmount);
        trackedHealth = currentHealth + restoreAmount;
        
        if (settings->bDebugLogging) {
            logger::info("[TIMED DODGE I-FRAME FALLBACK] Restored {:.1f} damage (MaxsuIFrame may not be installed)", 
                restoreAmount);
        }
    }
}

void TimedDodgeState::InitializeBlurIMOD()
{
    // Use the GetHit IMOD (0x162) directly instead of cloning
    auto* form = RE::TESForm::LookupByID(0x162);
    if (form) {
        auto* imod = form->As<RE::TESImageSpaceModifier>();
        if (imod && imod->radialBlur.strength) {
            dodgeImod = imod;
            // Save original values for restoration
            originalBlurStrength = imod->radialBlur.strength->floatValue;
            if (imod->radialBlur.rampUp)   originalBlurRampUp   = imod->radialBlur.rampUp->floatValue;
            if (imod->radialBlur.rampDown) originalBlurRampDown = imod->radialBlur.rampDown->floatValue;
            if (imod->radialBlur.start)    originalBlurStart    = imod->radialBlur.start->floatValue;
            logger::info("[TIMED DODGE] Using GetHit IMOD (0x162) for radial blur (saved originals: str={:.2f}, up={:.2f}, dn={:.2f}, start={:.2f})",
                originalBlurStrength, originalBlurRampUp, originalBlurRampDown, originalBlurStart);
            return;
        }
    }
    
    // Fallback: search all IMODs for one with radial blur
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (dataHandler) {
        for (auto* imod : dataHandler->GetFormArray<RE::TESImageSpaceModifier>()) {
            if (imod && imod->radialBlur.strength) {
                dodgeImod = imod;
                originalBlurStrength = imod->radialBlur.strength->floatValue;
                if (imod->radialBlur.rampUp)   originalBlurRampUp   = imod->radialBlur.rampUp->floatValue;
                if (imod->radialBlur.rampDown) originalBlurRampDown = imod->radialBlur.rampDown->floatValue;
                if (imod->radialBlur.start)    originalBlurStart    = imod->radialBlur.start->floatValue;
                const char* editorID = imod->GetFormEditorID();
                logger::info("[TIMED DODGE] Using fallback IMOD: {} (FormID: {:X})",
                    editorID ? editorID : "unknown", imod->GetFormID());
                return;
            }
        }
    }
    
    logger::error("[TIMED DODGE] No IMOD with radial blur found - blur effect disabled");
    dodgeImod = nullptr;
}

RE::Actor* TimedDodgeState::FindAttackingEnemyInRange(RE::Actor* player, float range)
{
    if (!player) return nullptr;
    
    auto* settings = Settings::GetSingleton();
    auto playerPos = player->GetPosition();
    float rangeSq = range * range;
    
    RE::Actor* closestAttacker = nullptr;
    float closestDistSq = rangeSq + 1.0f;
    
    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) return nullptr;
    
    for (auto& actorHandle : processLists->highActorHandles) {
        auto actor = actorHandle.get().get();
        if (!actor || actor == player || actor->IsDead()) {
            continue;
        }
        
        if (!actor->IsHostileToActor(player)) {
            continue;
        }
        
        float distSq = playerPos.GetSquaredDistance(actor->GetPosition());
        if (distSq > rangeSq) {
            continue;
        }
        
        if (!actor->IsInCombat()) {
            continue;
        }
        
        // Accept any hostile actor in combat that is actively attacking.
        // No currentCombatTarget check — enemies frequently re-target mid-swing
        // in group combat, and an attack aimed at a follower near the player
        // still counts as something worth dodging.
        auto attackState = actor->AsActorState()->GetAttackState();
        bool isAttacking = (attackState == RE::ATTACK_STATE_ENUM::kDraw ||
                           attackState == RE::ATTACK_STATE_ENUM::kSwing ||
                           attackState == RE::ATTACK_STATE_ENUM::kHit ||
                           attackState == RE::ATTACK_STATE_ENUM::kNextAttack ||
                           attackState == RE::ATTACK_STATE_ENUM::kFollowThrough ||
                           attackState == RE::ATTACK_STATE_ENUM::kBash);
        
        if (!isAttacking) {
            continue;
        }
        
        // Track closest attacker
        if (distSq < closestDistSq) {
            closestDistSq = distSq;
            closestAttacker = actor;
        }
    }
    
    if (closestAttacker && settings->bDebugLogging) {
        logger::info("[TIMED DODGE] Found attacking enemy: '{}' at {:.0f} units (attack state: {})",
            closestAttacker->GetName(), std::sqrt(closestDistSq),
            static_cast<int>(closestAttacker->AsActorState()->GetAttackState()));
    } else if (!closestAttacker && settings->bDebugLogging) {
        for (auto& actorHandle : processLists->highActorHandles) {
            auto actor = actorHandle.get().get();
            if (!actor || actor == player || actor->IsDead()) continue;
            if (!actor->IsHostileToActor(player)) continue;
            float distSq = playerPos.GetSquaredDistance(actor->GetPosition());
            if (distSq > rangeSq) continue;
            auto state = actor->AsActorState()->GetAttackState();
            logger::debug("[TIMED DODGE] Nearby '{}' at {:.0f}u: attackState={}, inCombat={}",
                actor->GetName(), std::sqrt(distSq), static_cast<int>(state), actor->IsInCombat());
        }
    }
    
    return closestAttacker;
}
