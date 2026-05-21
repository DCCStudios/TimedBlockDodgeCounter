#pragma once

class Settings {
public:
    static Settings* GetSingleton();

    void LoadSettings();
    void SaveSettings();
    
    //==========================================================================
    // Hitstop Settings - Freezes attacker's animation (NOT the whole world!)
    //==========================================================================
    bool  bEnableHitstop{ true };
    float fHitstopSpeed{ 0.05f };          // Animation speed during hitstop (0.0 = freeze, 0.1 = very slow)
    float fHitstopDuration{ 0.15f };       // Duration in seconds
    
    //==========================================================================
    // Stagger Settings - Force attacker into stagger animation
    //==========================================================================
    bool  bEnableStagger{ true };
    float fStaggerMagnitude{ 0.7f };            // Stagger for normal attacks (0=small, 0.3=medium, 0.7=large, 1.0+=largest)
    float fPowerAttackStaggerMagnitude{ 1.0f }; // Stagger for power attacks (usually higher)
    
    // Skill-based stagger chance
    bool  bUseStaggerChance{ false };       // Disabled by default (guaranteed stagger)
    float fBaseStaggerChance{ 50.0f };      // Base chance at 0 skill (50%)
    float fMaxStaggerChance{ 100.0f };      // Max chance at 100 skill (100%)
    bool  bStaggerUseBlockSkill{ true };    // Factor in Block skill
    bool  bStaggerUseWeaponSkill{ false };  // Factor in weapon skill (One-Handed/Two-Handed)
    
    //==========================================================================
    // Camera Shake Settings - Impact feel
    //==========================================================================
    bool  bEnableCameraShake{ true };
    float fCameraShakeStrength{ 0.5f };    // Shake intensity
    float fCameraShakeDuration{ 0.2f };    // Shake duration in seconds
    
    //==========================================================================
    // Stamina Restoration - Restore stamina on successful timed block
    //==========================================================================
    bool  bEnableStaminaRestore{ true };   // Enabled by default
    float fStaminaRestorePercent{ 100.0f }; // Percentage of max stamina to restore (100 = full refill)
    
    //==========================================================================
    // Sound Settings
    //==========================================================================
    bool  bEnableSound{ true };
    std::string sSoundPath{ "UIMenuOK" };  // Sound descriptor EditorID
    bool  bUseCustomWav{ false };          // Use custom WAV file instead of sound descriptor
    float fCustomWavVolume{ 1.0f };        // Volume for custom WAV (0.0 - 1.0)
    
    //==========================================================================
    // Slowmo Settings - World slowdown on timed block
    //==========================================================================
    bool  bEnableSlowmo{ false };          // Disabled by default
    float fSlowmoSpeed{ 0.25f };           // 25% of normal game speed
    float fSlowmoDuration{ 0.75f };        // Duration in seconds (real time)
    
    //==========================================================================
    // Counter Attack Settings - Cancel block animation to attack
    //==========================================================================
    bool  bEnableCounterAttack{ false };   // Disabled by default
    float fCounterAttackWindow{ 0.5f };    // Window in seconds to perform counter attack
    bool  bPreventPlayerStagger{ true };   // Prevent player stagger on successful timed block
    
    //==========================================================================
    // Counter Attack Damage Bonus - Increase damage of counter hit
    //==========================================================================
    bool  bEnableCounterDamageBonus{ false };    // Disabled by default
    float fCounterDamageBonusPercent{ 50.0f };   // Damage bonus percentage (50 = +50% damage)
    float fCounterDamageBonusTimeout{ 1.0f };    // Timeout in seconds for damage bonus (if no hit landed)
    
    //==========================================================================
    // Counter Strike Sound - Sound when counter hit connects
    //==========================================================================
    bool  bEnableCounterStrikeSound{ true };     // Enabled by default
    float fCounterStrikeSoundVolume{ 1.0f };     // Volume 0.0 - 1.0
    
    //==========================================================================
    // Counter Attack Lunge Settings - Move toward attacker on counter
    //==========================================================================
    bool  bEnableCounterLunge{ false };     // Disabled by default
    float fCounterLungeDistance{ 150.0f };  // Max distance to lunge (game units)
    float fCounterLungeSpeed{ 2.0f };       // Speed of lunge
    int   iCounterLungeCurve{ 0 };          // Velocity curve: 0=Bell, 1=Linear, 2=EaseIn, 3=EaseOut, 4=CubicIn, 5=CubicOut
    float fCounterLungeMeleeStopDistance{ 128.0f }; // Stop lunge this far from target (timed block counter)
    
    //==========================================================================
    // Counter Attack Slow Time Settings - Slow time during counter attack
    //==========================================================================
    bool  bEnableCounterSlowTime{ false };       // Disabled by default
    float fCounterSlowTimeScale{ 0.25f };        // Time scale (0.25 = 25% speed)
    float fCounterSlowTimeMaxDuration{ 2.0f };   // Max duration if end event not received
    bool  bCounterSlowStartAfterLunge{ false };  // Start slow time after lunge ends (instead of anim event)
    std::string sCounterSlowStartEvent{ "attackStart" };  // Event to start slow time (if not using lunge trigger)
    std::string sCounterSlowEndEvent{ "weaponSwing" };    // Event to end slow time
    
    //==========================================================================
    // Parry Window Settings - Controls the timed block window duration
    //==========================================================================
    float fParryWindowDurationMs{ 150.0f };     // Duration in milliseconds for the parry/timed block window
    
    //==========================================================================
    // Timed Block Cooldown Settings
    //==========================================================================
    bool  bEnableCooldown{ true };              // Enable cooldown after failed timed block
    float fCooldownDurationMs{ 250.0f };        // Cooldown duration in milliseconds
    bool  bIgnoreCooldownOutsideRange{ false }; // Ignore cooldown if no enemy within range
    float fCooldownIgnoreDistance{ 512.0f };    // Distance threshold for cooldown ignore
    
    //==========================================================================
    // Core timed block (standalone — no longer read-only)
    //==========================================================================
    std::string sBlockKey{ "V" };
    float fStaggerDistance{ 128.0f };
    bool  bPerkLockedBlock{ false };
    bool  bOnlyWithShield{ false };
    bool  bPerkLockedStagger{ false };
    bool  bEnableExplosion{ false };

    // [Forms] — plugin name + hex FormID (no 0x prefix)
    std::string sFormsPerkPluginName;
    std::string sFormsDamagePreventPerkID;
    std::string sFormsStaggerPerkID;
    std::string sExplosionPluginName;
    std::string sExplosionFormID;
    
    //==========================================================================
    // Timed Dodge Settings - Perfect dodge triggers slow-mo, i-frames, blur
    //==========================================================================
    bool  bEnableTimedDodge{ true };              // Master toggle (enabled by default)
    float fTimedDodgeSlomoDuration{ 4.0f };       // Slow-mo duration in seconds
    float fTimedDodgeSlomoSpeed{ 0.05f };         // Game speed during slomo (0.05 = 5%)
    float fTimedDodgeCooldown{ 3.0f };            // Cooldown in seconds
    float fTimedDodgeDamageCooldown{ 1.0f };      // Cooldown after HP loss from a hit (seconds); 0 = off
    float fTimedDodgeHitContactCooldown{ 0.45f }; // After melee *contact* (Precision PreHit); 0 = off. Not DoT-based.
    float fTimedDodgeDetectionRange{ 350.0f };    // Range to detect attacking enemies (game units)
    float fTimedDodgeForgivenessMs{ 500.0f };    // Grace period for early dodges (milliseconds)
    float fTimedDodgeHitWindowMs{ 600.0f };     // Window after dodge start where getting hit triggers timed dodge

    // Timed Dodge I-Frames
    bool  bTimedDodgeIframes{ true };             // Player cannot be damaged during slomo

    // Timed Dodge Counter Attack (reuses timed block counter attack system)
    bool  bTimedDodgeCounterAttack{ true };       // Allow counter attack to cancel slomo
    float fTimedDodgeCounterWindowMs{ 2000.0f };  // How long you have to press attack (capped by slomo end)
    float fTimedDodgeCounterDamagePercent{ 50.0f }; // Counter damage bonus for timed dodge (50 = 1.5x total)
    float fTimedDodgeCounterDamageTimeout{ 3.0f }; // Timeout for dodge counter damage bonus (longer than block due to dodge exit)
    bool  bTimedDodgeCounterLunge{ true };            // Lunge toward attacker on timed dodge counter
    float fTimedDodgeCounterLungeSpeed{ 2.0f };     // Lunge speed for timed dodge counter
    int   iTimedDodgeCounterLungeCurve{ 0 };         // Velocity curve for timed dodge lunge (same enum as iCounterLungeCurve)
    float fTimedDodgeCounterLungeMeleeStopDistance{ 128.0f }; // Stop distance for timed dodge lunge
    bool  bTimedDodgeCounterSpellHit{ true };                  // Allow spell counter attacks from timed dodge
    float fTimedDodgeCounterSpellDamagePercent{ 50.0f };       // Spell counter damage bonus % for timed dodge
    bool  bTimedDodgeCounterRanged{ true };                    // Allow bow/crossbow counter from timed dodge
    float fTimedDodgeCounterRangedDamagePercent{ 50.0f };      // Ranged counter damage bonus %
    float fTimedDodgeCounterRangedWindowMs{ 2500.0f };         // Window for firing the arrow/bolt
    float fTimedDodgeCounterDrawSpeedMult{ 2.0f };             // Temporary draw speed multiplier (1.0 = normal)
    bool  bTimedDodgeCounterRangedSound{ true };               // Play sound on ranged counter hit

    // Timed Dodge Radial Blur
    bool  bTimedDodgeRadialBlur{ true };          // Radial blur during slomo
    float fTimedDodgeBlurStrength{ 0.3f };        // Blur strength (0-1)
    float fTimedDodgeBlurBlendSpeed{ 5.0f };      // Blend speed for fade in/out
    float fTimedDodgeBlurRampUp{ 0.1f };          // IMOD ramp up time (seconds)
    float fTimedDodgeBlurRampDown{ 0.2f };        // IMOD ramp down time (seconds)
    float fTimedDodgeBlurRadius{ 0.4f };          // Center clarity radius (0=center, 1=edges only)

    // Timed Dodge - Apply timed block visual effects (hitstop, camera shake, stamina, etc.)
    bool  bTimedDodgeApplyBlockEffects{ true };   // Apply same effects as timed block on the attacker

    // Timed Dodge - Stagger attacker (separate from timed block stagger)
    bool  bTimedDodgeStagger{ false };            // Stagger the attacker on timed dodge (default off)

    // Timed Dodge - Attacker animation slow (per-actor, not game speed)
    bool  bTimedDodgeAttackerSlow{ true };        // Slow the attacker's animation during timed dodge
    float fTimedDodgeAttackerSlowSpeed{ 0.05f };  // Animation speed multiplier (0.05 = 5%)
    float fTimedDodgeAttackerSlowDuration{ 1.5f };// Duration in seconds

    // Timed Dodge Sound
    bool  bTimedDodgeSound{ true };               // Play sound on timed dodge
    float fTimedDodgeSoundVolume{ 1.0f };         // Volume for timed dodge WAV (0.0 - 1.0)

    //==========================================================================
    // Ward Timed Block — melee hits during ward timing window (separate from shield block)
    //==========================================================================
    bool  bEnableWardTimedBlock{ true };
    float fWardTimedBlockWindowMs{ 500.0f };

    bool  bWardTimedBlockStagger{ true };
    float fWardSmallStaggerMagnitude{ 0.5f };   // Single-hand ward
    float fWardLargeStaggerMagnitude{ 1.0f };    // Dual-cast ward (both hands)

    bool  bWardTimedBlockDamageCancel{ true };

    bool  bWardTimedBlockSound{ true };
    std::string sWardTimedBlockSoundFile{ "wardtimedblock.wav" };
    float fWardTimedBlockSoundVolume{ 1.0f };

    bool  bWardTimedBlockCounterAttack{ true };
    float fWardCounterWindowMs{ 2000.0f };
    float fWardCounterDamagePercent{ 50.0f };  // Separate damage bonus % for ward magic counter

    bool  bWardCounterSpellHit{ true };
    // How long (ms) after a counter spell is CAST to keep the bonus alive while
    // the projectile travels (or a concentration spell starts hitting).
    // If nothing lands in this window the bonus expires.
    float fWardCounterSpellInFlightMs{ 5000.0f };
    bool  bWardCounterSpellSound{ true };
    std::string sWardCounterSpellSoundFile{ "wardcounterspell.wav" };
    float fWardCounterSpellSoundVolume{ 1.0f };

    float fWardTimedBlockCooldown{ 1.0f };

    // Melee range for ward hit detection (game units)
    float fWardMeleeDetectionRange{ 300.0f };

    // 2H requirement: dual-cast ward needed to parry 2H weapon attacks
    bool  bWardRequire2HForTwoHanders{ false };

    // Omnidirectional ward — if false (default), only attacks from the front 180° are parried
    bool  bWardOmnidirectional{ false };

    // Magicka restore on successful ward timed block
    bool  bWardTimedBlockMagickaRestore{ true };
    float fWardMagickaRestorePercent{ 50.0f };

    // Window Mutual Exclusion — prevents timed block, ward, and dodge from chaining
    float fWindowExclusionMs{ 1000.0f };

    // Debug
    bool bDebugLogging{ false };
    bool bDebugScreenTimedBlock{ false };
    bool bDebugScreenCounterAttack{ false };
    bool bDebugScreenWard{ false };
    bool bDebugScreenDodge{ false };

private:
    Settings() = default;
    Settings(const Settings&) = delete;
    Settings(Settings&&) = delete;
    Settings& operator=(const Settings&) = delete;
    Settings& operator=(Settings&&) = delete;

    static constexpr const char* INI_PATH = R"(.\Data\SKSE\Plugins\TimedBlockDodgeCounter.ini)";
};
