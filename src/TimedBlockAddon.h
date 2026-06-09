#pragma once

#include "PrecisionAPI.h"
#include "Settings.h"

#include <vector>

namespace PrecisionCache
{
    inline PRECISION_API::IVPrecision1* g_api{ nullptr };

    inline void Init()
    {
        g_api = reinterpret_cast<PRECISION_API::IVPrecision1*>(
            PRECISION_API::RequestPluginAPI(PRECISION_API::InterfaceVersion::V1));
        if (g_api) {
            logger::info("[PrecisionCache] API (V1) cached successfully");
        } else {
            logger::info("[PrecisionCache] Precision not available — strict dodge detection disabled");
        }
    }
}

// RELOCATION_OFFSET macro for SE/AE compatibility
#ifndef RELOCATION_OFFSET
#   ifdef SKYRIM_SUPPORT_AE
#       define RELOCATION_OFFSET(se, ae) ae
#   else
#       define RELOCATION_OFFSET(se, ae) se
#   endif
#endif

// Custom hash for RE::ActorHandle to use in unordered_map
struct ActorHandleHash {
    std::size_t operator()(RE::ActorHandle handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.native_handle());
    }
};

// Animation speed manager - freezes specific actors instead of the whole world
class AnimSpeedManager
{
public:
    static void SetAnimSpeed(RE::ActorHandle a_actorHandle, float a_speedMult, float a_duration);
    static void RevertAnimSpeed(RE::ActorHandle a_actorHandle);
    static void RevertAllAnimSpeed();
    static void Init();

private:
    struct AnimSpeedData
    {
        float speedMult;
        float duration;
    };
    
    // Use custom hash for ActorHandle
    static inline std::unordered_map<RE::ActorHandle, AnimSpeedData, ActorHandleHash> _animSpeeds;
    static inline std::mutex _animSpeedsLock;
    
    static void Update(RE::ActorHandle a_actorHandle, float& a_deltaTime);
    
    // Hook for player animations
    class PlayerAnimationHook
    {
    public:
        static void Install();
    private:
        static void PlayerCharacter_UpdateAnimation(RE::PlayerCharacter* a_this, float a_deltaTime);
        static inline REL::Relocation<decltype(PlayerCharacter_UpdateAnimation)> _originalFunc;
    };
    
    // Hook for NPC animations
    class NPCAnimationHook
    {
    public:
        static void Install();
    private:
        static void UpdateAnimationInternal(RE::Actor* a_this, float a_deltaTime);
        static inline REL::Relocation<decltype(UpdateAnimationInternal)> _originalFunc;
    };
};

// Cooldown state tracking - monitors parry window effect state
namespace CooldownState
{
    inline std::chrono::steady_clock::time_point cooldownEndTime;
    inline bool hadParryEffectLastFrame{ false };
    inline bool timedBlockTriggeredThisWindow{ false };
    inline bool inCooldown{ false };
    inline bool blockedByActiveCooldown{ false };  // Set when a block was prevented this frame
    
    // Optimization: Cache parry effect check (reset each frame)
    inline bool parryEffectCachedThisFrame{ false };
    inline bool cachedParryEffectResult{ false };

    // Vanilla / dual-wield block state (REL:: IsBlocking)
    inline bool wasBlockingLastFrame{ false };

    // Chrono-based parry window: the spell's effectItem.duration is uint32_t
    // (integer seconds, minimum 1), so sub-second windows can't be expressed
    // through the engine.  We track the real deadline here and force-dispel
    // the spell when it elapses.
    inline bool parryWindowTimerActive{ false };
    inline std::chrono::steady_clock::time_point parryWindowDeadline;
    
    // Optimization: Cache nearby enemy check (checked every 100ms instead of every frame)
    inline std::chrono::steady_clock::time_point lastEnemyCheckTime;
    inline bool cachedHasNearbyEnemy{ true };  // Fail-safe: assume enemy nearby
    constexpr auto ENEMY_CHECK_INTERVAL = std::chrono::milliseconds(100);  // 10 checks/sec
    
    // Get cached parry effect state (only checks once per frame)
    bool GetParryEffectCached();
    
    // Get cached nearby enemy state (only checks every ENEMY_CHECK_INTERVAL)
    bool GetNearbyEnemyCached();
    
    // Called each frame to track parry window effect transitions
    void Update();
    
    // Called when a timed block is successfully triggered - CLEARS cooldown
    void OnTimedBlockTriggered();
    
    // Start or restart the cooldown timer
    void StartCooldown(const char* reason);
    
    // Check if currently on cooldown (includes per-frame block flag)
    bool IsOnCooldown();
    
    // Internal check without the per-frame flag
    bool IsOnCooldownInternal();
    
    // Check if player has the parry window effect
    bool PlayerHasParryWindowEffect();
}

class TimedBlockAddon : public RE::BSTEventSink<RE::TESHitEvent> {
public:
    static TimedBlockAddon* GetSingleton();
    
    void Initialize();
    static void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent* a_event,
        RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;

    // Effect methods
    void ApplyTimedBlockEffects(RE::Actor* defender, RE::Actor* attacker, bool skipSlowmo = false, bool fromTimedDodge = false);
    // Ward timed block: melee hit during ward window — no player block/stagger cancels; optional 1H/2H stagger on attacker
    void ApplyWardTimedBlockEffects(RE::Actor* defender, RE::Actor* attacker, bool isDualCastWard);
    void PlayTimedBlockSound();
    void PlayCustomWavSound();
    void ApplySlowmo(float speed, float duration);
    
    bool CreateParryWindowForms();
    void UpdateParryWindowDuration();
    void LoadPerkForms();

    void CastParryWindowSpell(RE::Actor* target);
    bool HasParryFormsReady() const { return mgef_parry_window != nullptr && spell_parry_window != nullptr; }

    RE::BGSPerk* damagePreventPerk{ nullptr };
    RE::BGSPerk* staggerPerk{ nullptr };

    // Check if actor has parry window effect (public for cooldown tracking)
    bool ActorHasParryWindowEffect(RE::Actor* actor);
    bool IsParryWindowMGEF(RE::EffectSetting* mgef) const { return mgef && mgef == mgef_parry_window; }
    
    // Check if there's any enemy within range in combat with the player
    bool HasNearbyEnemyInCombat(RE::Actor* player, float radius);

    std::vector<RE::Actor*> GetNearbyActors(RE::Actor* center, float radius) const;
    
private:
    TimedBlockAddon() = default;
    TimedBlockAddon(const TimedBlockAddon&) = delete;
    TimedBlockAddon(TimedBlockAddon&&) = delete;
    TimedBlockAddon& operator=(const TimedBlockAddon&) = delete;
    TimedBlockAddon& operator=(TimedBlockAddon&&) = delete;
    
    // Force attacker into stagger animation
    void TriggerStagger(RE::Actor* defender, RE::Actor* attacker, float magnitude);
    
    // Camera shake effect
    void ShakeCamera(float strength, const RE::NiPoint3& position, float duration);
    
    RE::EffectSetting* mgef_parry_window{ nullptr };
    RE::SpellItem* spell_parry_window{ nullptr };
    RE::BGSExplosion* timedBlockExplosion{ nullptr };
};


// Offsets and function pointers
namespace Offsets
{
    // Camera shake function
    typedef void(__fastcall* tShakeCamera)(float strength, RE::NiPoint3 source, float duration);
    inline static REL::Relocation<tShakeCamera> ShakeCamera{ RELOCATION_ID(32275, 33012) };

    inline REL::Relocation<bool (*)(RE::Actor*)> IsBlocking{ RELOCATION_ID(36927, 37952) };
}

// Per-frame snapshot of the player's health, taken at the END of
// PlayerCharacter::UpdateAnimation. TESHitEvent fires AFTER the engine has
// already subtracted damage, so subtracting current HP from this snapshot
// tells us how much damage the just-processed hit dealt (used for both the
// timed-dodge "real damage" cooldown and the parry-window HP restore).
//
// 1.0.2: introduced when CombatHit's Actor::DealDamage entry hook was removed
// to eliminate trampoline conflicts with other parry/combat plugins.
namespace PlayerHealthState
{
    inline float lastFrameHealth{ 0.0f };
    inline bool  primed{ false };  // false until the first frame snapshot is taken
}

namespace CombatHit
{
    // 1.0.3: Install() is intentionally a no-op. The previous v1.0.1 build
    // installed a 5-byte entry detour on Actor::DealDamage. That hook was
    // overwritten/relocated by other SKSE plugins that hook the same entry
    // (DualWieldParryingNG, etc.), and CommonLibSSE-NG's saved-prologue
    // copy did not survive the chain — _original ended up pointing at an
    // unmapped page, crashing on the next hit with EXCEPTION_ACCESS_VIOLATION
    // at an unmapped PC.
    //
    // The functionality (parry HP prevention, real-damage cooldown,
    // hit-during-dodge trigger) is now provided by TWO complementary paths,
    // neither of which patches any engine bytes:
    //
    //   PRIMARY (when Precision is loaded): the plugin-wide PreHit callback
    //     registered in WardTimedBlockState::RegisterPrecision now also
    //     handles the regular parry window. ret.bIgnoreHit = true cancels
    //     the hit at the Havok hitbox level — the engine never calls
    //     DealDamage and the player takes 0 HP loss with no flicker.
    //
    //   FALLBACK (no Precision, or hits Precision didn't intercept): the
    //     TESHitEvent handler in TimedBlockAddon::ProcessEvent restores the
    //     post-hit HP delta using PlayerHealthState::lastFrameHealth, and
    //     also handles the timed-dodge "real damage" cooldown for ranged
    //     and other non-Precision hits.
    void Install();
}

// Dispels the parry window spell from the player (used when on cooldown)
void DispelParryWindowSpell();

// Shared timestamp of the last time ANY timed window (block, ward, dodge) was
// successfully activated.  Each system checks this before opening its own window
// to enforce a mutual-exclusion gap (fWindowExclusionMs).
namespace WindowExclusion
{
    inline std::chrono::steady_clock::time_point lastWindowTime{};

    // Record that a window just opened.
    inline void Stamp()
    {
        lastWindowTime = std::chrono::steady_clock::now();
    }

    // Returns true if another window was activated too recently.
    inline bool IsBlocked()
    {
        auto* settings = Settings::GetSingleton();
        if (settings->fWindowExclusionMs <= 0.0f) return false;
        auto elapsed = std::chrono::steady_clock::now() - lastWindowTime;
        return elapsed < std::chrono::milliseconds(static_cast<long long>(settings->fWindowExclusionMs));
    }
}

// Counter Attack state tracking - allows attacking to cancel block animation
namespace CounterAttackState
{
    inline std::chrono::steady_clock::time_point windowEndTime;
    inline std::chrono::steady_clock::time_point damageBonusEndTime;
    inline bool inWindow{ false };
    inline RE::ActorHandle lastAttackerHandle;
    inline bool damageBonusActive{ false };
    inline float appliedDamageBonus{ 0.0f };
    inline bool fromTimedDodge{ false };
    inline bool fromWardTimedBlock{ false };

    // Set when the player fires a spell during the ward counter window.
    // Prevents the damage bonus from expiring before the projectile lands.
    inline bool spellFiredDuringWindow{ false };

    // Handle of the specific projectile fired by the player.
    // Empty if it's a concentration/beam spell (no projectile to track).
    inline RE::ProjectileHandle trackedSpellProjectile{};

    // Frames remaining to retry finding the projectile in Projectile::Manager
    // after the spell-fire animation event (projectile may spawn one frame later).
    inline int projectileScanRetries{ 0 };

    // Runtime "Damage Health" MGEF + spell — cast on the TARGET on counter hit
    inline RE::EffectSetting* counterMGEF{ nullptr };
    inline RE::SpellItem* counterSpell{ nullptr };

    // Ranged counter (bow/crossbow from timed dodge)
    inline bool rangedCounterActive{ false };
    inline RE::EffectSetting* drawSpeedMGEF{ nullptr };
    inline RE::SpellItem* drawSpeedSpell{ nullptr };

    bool CreateCounterDamageForms();
    bool CreateDrawSpeedForms();

    // Runtime keywords applied during counter attacks
    inline RE::BGSKeyword* counterAttackKeyword{ nullptr };
    inline RE::BGSKeyword* rangedCounterKeyword{ nullptr };
    inline RE::BGSKeyword* magicCounterKeyword{ nullptr };
    inline bool keywordApplied{ false };
    inline bool rangedKeywordApplied{ false };
    inline bool magicKeywordApplied{ false };
    inline bool counterAttackAnimStarted{ false };
    bool CreateCounterAttackKeywords();
    void ApplyCounterKeyword(bool ranged = false, bool magic = false);
    void RemoveCounterKeyword();
    void OnAnimEvent(const RE::BSFixedString& tag);

    void StartWindow(RE::Actor* attacker = nullptr);
    void StartWardWindow(RE::Actor* attacker = nullptr);
    void OnSpellFired();
    void OnRangedCounterInput();
    void ApplyDrawSpeedBuff();
    void RemoveDrawSpeedBuff();
    void Update();
    bool IsInWindow();
    void OnAttackInput();
    RE::Actor* GetLastAttacker();
    void ApplyDamageBonus(bool isSpellCounter = false);
    void RemoveDamageBonus();
    bool IsDamageBonusActive();
}

// Ward effect apply — opens ward timed block window when player self-applies a ward MGEF
namespace WardTimedBlockState
{
    inline bool inWindow{ false };
    inline bool isDualCast{ false };
    inline float healthSnapshot{ 0.0f };
    inline RE::ActorHandle lastAttackerHandle;
    inline std::chrono::steady_clock::time_point windowEnd;

    inline bool onCooldown{ false };
    inline std::chrono::steady_clock::time_point cooldownEndTime;

    // Attacker immunity: ignore damage from the triggering attacker for a short duration
    inline RE::ActorHandle immuneAttackerHandle;
    inline std::chrono::steady_clock::time_point immuneAttackerExpiry;
    bool IsAttackerImmune(RE::Actor* attacker);

    // Cached MagicWard keyword — set once by InitWardKeyword() at kDataLoaded
    inline RE::BGSKeyword* wardKeyword{ nullptr };

    // Set to true once Precision's PreHit callback is registered successfully.
    // Ward timed block is disabled and falls back to TESHitEvent if false.
    inline bool g_precisionAvailable{ false };

    void InitWardKeyword();
    // Register our PreHit callback with Precision. Called at kDataLoaded.
    void RegisterPrecision();
    void OnWardActivated(bool dualCast);
    // Returns true if the parry was consumed (hit should be ignored).
    // Returns false if the parry was rejected (e.g. 2H ward requirement failed).
    bool OnMeleeHit(RE::Actor* defender, RE::Actor* attacker);
    void Update();
    bool IsInWindow();
    bool IsOnCooldown();
    void StartCooldown();
    void PlayWardTimedBlockSound();
    void PlayWardCounterSpellSound();
}

class WardEffectHandler : public RE::BSTEventSink<RE::TESMagicEffectApplyEvent> {
public:
    static WardEffectHandler* GetSingleton();
    static void Register();

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESMagicEffectApplyEvent* a_event,
        RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override;

private:
    WardEffectHandler() = default;
};

// Hit event handler to remove damage bonus after first hit
class CounterDamageHitHandler : public RE::BSTEventSink<RE::TESHitEvent> {
public:
    static CounterDamageHitHandler* GetSingleton();
    static void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESHitEvent* a_event,
        RE::BSTEventSource<RE::TESHitEvent>* a_eventSource) override;
        
private:
    CounterDamageHitHandler() = default;
};

// Counter Lunge state tracking - moves player toward attacker
namespace CounterLungeState
{
    inline bool active{ false };
    inline bool loggedFirstFrame{ false };
    inline int  curveType{ 0 };
    inline float meleeStopDistance{ 128.0f };
    inline float elapsed{ 0.0f };
    inline float duration{ 0.0f };
    inline float totalDistance{ 0.0f };
    inline RE::NiPoint3 startPos{};
    inline RE::ActorHandle targetHandle{};
    
    // Start the lunge toward the target position
    void Start(RE::Actor* player, RE::Actor* target);
    
    // Move toward the target via velocityMod (called from physics hook)
    void ApplyVelocity(RE::bhkCharacterController* controller, float deltaTime);

    // Check if lunge is currently active
    bool IsActive();
    
    // Cancel the lunge
    void Cancel();
}

// Physics hook for lunge movement - hooks into Havok physics simulation
namespace LungePhysicsHook
{
    void Install();
}



// Counter Slow Time state tracking - slows time during counter attack
namespace CounterSlowTimeState
{
    inline bool active{ false };
    inline bool waitingForStartEvent{ false };
    inline bool waitingForEndEvent{ false };
    inline std::chrono::steady_clock::time_point maxEndTime;
    
    // Arm the slow time (wait for start event)
    void Arm();
    
    // Start slow time (called when start event is received)
    void Start();
    
    // End slow time (called when end event is received or timeout)
    void End();
    
    // Update each frame
    void Update();
    
    // Called when an animation event is received
    void OnAnimEvent(const std::string_view& eventName);
    
    // Check if slow time is active
    bool IsActive();
}

// Animation event handler for counter slow time
class CounterAnimEventHandler : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
public:
    static CounterAnimEventHandler* GetSingleton();
    static void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        const RE::BSAnimationGraphEvent* a_event,
        RE::BSTEventSource<RE::BSAnimationGraphEvent>* a_eventSource) override;
        
private:
    CounterAnimEventHandler() = default;
};

// Timed Dodge State - tracks perfect dodge slow-motion, i-frames, radial blur
namespace TimedDodgeState
{
    // Core state
    inline bool active{ false };               // Overall timed dodge is in effect (slomo or blur fading)
    inline bool slomoActive{ false };          // Slow-motion is currently running
    inline bool iframesActive{ false };        // Player has i-frames (cannot be damaged)
    inline bool dodgeIframesEnded{ false };    // Dodge animation's own i-frames have ended, we own the graph vars
    inline bool counterWindowOpened{ false };  // Counter window has been opened for this timed dodge
    inline float trackedHealth{ 0.0f };        // Fallback health for non-MaxsuIFrame setups
    
    // Timing
    inline std::chrono::steady_clock::time_point effectStartTime;
    inline std::chrono::steady_clock::time_point effectEndTime;
    
    // Dodge cooldown (per-dodge rate-limit)
    inline bool onCooldown{ false };
    inline std::chrono::steady_clock::time_point cooldownEndTime;

    // Damage cooldown (cannot timed-dodge while recently losing HP from a hit)
    inline bool onDamageCooldown{ false };
    inline std::chrono::steady_clock::time_point damageCooldownEndTime;

    // Melee hit-contact cooldown (Precision PreHit / TESHit fallback) — one shot
    // connected with the player, independent of final HP change (poison/DoT safe).
    inline bool onHitContactCooldown{ false };
    inline std::chrono::steady_clock::time_point hitContactCooldownEndTime;

    // Early dodge forgiveness buffer
    inline bool pendingDodge{ false };
    inline std::chrono::steady_clock::time_point pendingDodgeExpiry;

    // Hit-during-dodge forgiveness: if the player is mid-dodge and gets hit,
    // retroactively trigger the timed dodge with the attacker from the hit event.
    inline bool inDodgeAnimation{ false };
    inline std::chrono::steady_clock::time_point dodgeAnimationExpiry;
    
    // Radial blur IMOD state (uses the source IMOD directly, saves/restores original values)
    inline RE::TESImageSpaceModifier* dodgeImod{ nullptr };
    inline RE::ImageSpaceModifierInstanceForm* dodgeImodInstance{ nullptr };
    inline bool blurEffectActive{ false };
    inline float currentBlurStrength{ 0.0f };
    inline float targetBlurStrength{ 0.0f };
    inline std::chrono::steady_clock::time_point lastBlurUpdateTime;
    
    // Original IMOD values to restore after blur effect ends
    inline float originalBlurStrength{ 0.0f };
    inline float originalBlurRampUp{ 0.0f };
    inline float originalBlurRampDown{ 0.0f };
    inline float originalBlurStart{ 0.0f };
    
    // Attacker tracking
    inline RE::ActorHandle attackerHandle;
    
    // Attacker immunity: ignore damage from the triggering attacker for a short duration
    inline RE::ActorHandle immuneAttackerHandle;
    inline std::chrono::steady_clock::time_point immuneAttackerExpiry;
    
    bool IsAttackerImmune(RE::Actor* attacker);
    
    // Custom Dodge plugin integration (CustomDodge.esp global polling)
    inline RE::TESGlobal* customDodgeGlobal{ nullptr };
    inline bool customDodgeWasActive{ false };
    void InitCustomDodge();
    void PollCustomDodge();

    // Check if an animation event name is a dodge event
    bool IsDodgeEvent(const char* eventName);
    
    // Called when a dodge animation event fires on the player
    void OnAnimEvent(const char* eventName);
    
    // Attempt to trigger a timed dodge (checks for attacking enemies, cooldown, etc.)
    void OnDodgeEvent();
    
    // Start all timed dodge effects (slomo, i-frames, blur, counter window)
    void Start(RE::Actor* attacker);
    
    // End all timed dodge effects (restore game speed, remove i-frames, fade blur)
    void End();
    
    // Per-frame update: radial blur blending, i-frame health tracking, timer checks
    void Update();
    
    // Is the timed dodge currently active (slomo or blur still fading)?
    bool IsActive();
    
    // Is the slow-motion phase running?
    bool IsSlomoActive();
    
    // Cooldown management
    bool IsOnCooldown();
    void StartCooldown();

    // Damage cooldown: called when the player loses HP from a hit (TESHit + snapshot)
    void OnPlayerDamaged();

    // Hit-contact cooldown: melee weapon contact on player (Precision PreHit; TESHit if no Precision)
    void OnMeleeWeaponHitContact();

    // Hit-during-dodge: player got hit while in a dodge animation but timed dodge
    // hadn't triggered yet.  Returns true if this retroactively starts a timed dodge.
    bool TryTriggerFromHit(RE::Actor* attacker);
    
    // Called when the player is hit during i-frames (restores health)
    void OnPlayerHit(RE::Actor* player);
    
    // Create the radial blur IMOD (called once during initialization)
    void InitializeBlurIMOD();
    
    // Find the closest hostile enemy in melee attack swing phase within range
    RE::Actor* FindAttackingEnemyInRange(RE::Actor* player, float range);

    // Play the timed dodge WAV sound
    void PlayDodgeSound();
}

// Input event sink for detecting attack input during counter attack window
class CounterAttackInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
public:
    static CounterAttackInputHandler* GetSingleton();
    static void Register();
    
    RE::BSEventNotifyControl ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;
        
private:
    CounterAttackInputHandler() = default;
};

// Dual-wield / custom block key — mirrors original simple-timed-block sBlockKey (e.g. V)
class BlockKeyInputHandler : public RE::BSTEventSink<RE::InputEvent*> {
public:
    static BlockKeyInputHandler* GetSingleton();
    static void Register();

    RE::BSEventNotifyControl ProcessEvent(
        RE::InputEvent* const* a_event,
        RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;

private:
    BlockKeyInputHandler() = default;
};

// In-Game Audio Engine — plays sounds through the game's 3D audio pipeline
namespace InGameAudio
{
    enum class Slot { kTrigger = 0, kCounter = 1 };
    void Init();
    void PlaySound(const std::string& baseFilename, Slot slot, float volume);
}
