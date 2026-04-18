#include "Menu.h"
#include "Settings.h"
#include "TimedBlockAddon.h"
#include "SKSEMenuFramework.h"

namespace Menu
{
    namespace State
    {
        // Track if we have changes that haven't been saved to INI
        inline bool initialized{ false };
        inline bool hasUnsavedChanges{ false };
        
        // Sound path buffer for text input
        inline char soundPathBuffer[256]{ "UIMenuOK" };
        inline char wardSoundFileBuffer[128]{ "wardtimedblock.wav" };
        inline char wardCounterSpellFileBuffer[128]{ "wardcounterspell.wav" };
        inline char blockKeyBuffer[32]{ "V" };
        inline char formsPerkPluginBuffer[256]{};
        inline char formsDamagePerkBuffer[32]{};
        inline char formsStaggerPerkBuffer[32]{};
        inline char explosionPluginBuffer[256]{};
        inline char explosionFormIDBuffer[64]{};
        
        void Initialize()
        {
            auto* settings = Settings::GetSingleton();
            // Copy sound path to buffer for text input
            strncpy_s(soundPathBuffer, settings->sSoundPath.c_str(), sizeof(soundPathBuffer) - 1);
            soundPathBuffer[sizeof(soundPathBuffer) - 1] = '\0';
            strncpy_s(wardSoundFileBuffer, settings->sWardTimedBlockSoundFile.c_str(), sizeof(wardSoundFileBuffer) - 1);
            wardSoundFileBuffer[sizeof(wardSoundFileBuffer) - 1] = '\0';
            strncpy_s(wardCounterSpellFileBuffer, settings->sWardCounterSpellSoundFile.c_str(), sizeof(wardCounterSpellFileBuffer) - 1);
            wardCounterSpellFileBuffer[sizeof(wardCounterSpellFileBuffer) - 1] = '\0';
            strncpy_s(blockKeyBuffer, settings->sBlockKey.c_str(), sizeof(blockKeyBuffer) - 1);
            blockKeyBuffer[sizeof(blockKeyBuffer) - 1] = '\0';
            strncpy_s(formsPerkPluginBuffer, settings->sFormsPerkPluginName.c_str(), sizeof(formsPerkPluginBuffer) - 1);
            formsPerkPluginBuffer[sizeof(formsPerkPluginBuffer) - 1] = '\0';
            strncpy_s(formsDamagePerkBuffer, settings->sFormsDamagePreventPerkID.c_str(), sizeof(formsDamagePerkBuffer) - 1);
            formsDamagePerkBuffer[sizeof(formsDamagePerkBuffer) - 1] = '\0';
            strncpy_s(formsStaggerPerkBuffer, settings->sFormsStaggerPerkID.c_str(), sizeof(formsStaggerPerkBuffer) - 1);
            formsStaggerPerkBuffer[sizeof(formsStaggerPerkBuffer) - 1] = '\0';
            strncpy_s(explosionPluginBuffer, settings->sExplosionPluginName.c_str(), sizeof(explosionPluginBuffer) - 1);
            explosionPluginBuffer[sizeof(explosionPluginBuffer) - 1] = '\0';
            strncpy_s(explosionFormIDBuffer, settings->sExplosionFormID.c_str(), sizeof(explosionFormIDBuffer) - 1);
            explosionFormIDBuffer[sizeof(explosionFormIDBuffer) - 1] = '\0';
            initialized = true;
            hasUnsavedChanges = false;
        }
        
        void MarkChanged()
        {
            hasUnsavedChanges = true;
        }
    }

    void Register()
    {
        if (!SKSEMenuFramework::IsInstalled()) {
            logger::warn("SKSE Menu Framework not installed, in-game menu disabled");
            return;
        }
        
        SKSEMenuFramework::SetSection("Timed Block Dodge and Counter");
        SKSEMenuFramework::AddSectionItem("Settings", RenderSettings);
        
        logger::info("SKSE Menu Framework menu registered");
    }

    void __stdcall RenderSettings()
    {
        auto* settings = Settings::GetSingleton();
        
        // Initialize on first render
        if (!State::initialized) {
            State::Initialize();
        }
        
        // Header with unsaved indicator
        ImGui::Text("Timed Block Dodge and Counter - Settings");
        if (State::hasUnsavedChanges) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "(Unsaved to INI)");
        }
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Core Timed Block", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Parry Window Duration (ms)", &settings->fParryWindowDurationMs, 50.0f, 1000.0f, "%.0f ms")) {
                State::MarkChanged();
                auto* addon = TimedBlockAddon::GetSingleton();
                if (addon->HasParryFormsReady()) {
                    addon->UpdateParryWindowDuration();
                    RE::DebugNotification(fmt::format("Parry window: {:.0f}ms", settings->fParryWindowDurationMs).c_str());
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Duration of the timed block window after pressing block.\nLower = harder timing, Higher = easier timing.\nDefault: 150ms. Changes apply immediately.");
            }

            ImGui::Spacing();

            if (ImGui::Checkbox("Perk-locked damage prevention", &settings->bPerkLockedBlock)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When enabled, blocked damage is only fully negated if the player has the configured perk.");
            }

            if (ImGui::Checkbox("Perk-locked stagger", &settings->bPerkLockedStagger)) {
                State::MarkChanged();
            }

            if (ImGui::Checkbox("Shield only (core timed block)", &settings->bOnlyWithShield)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Requires a shield in the left hand for parry-window detection and damage negation.");
            }

            if (ImGui::SliderFloat("Nearby stagger distance", &settings->fStaggerDistance, 0.0f, 512.0f, "%.0f")) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Actors within this radius (excluding the attacker) can be staggered on a successful timed block.");
            }

            if (ImGui::InputText("Dual-wield block key", State::blockKeyBuffer, sizeof(State::blockKeyBuffer))) {
                settings->sBlockKey = State::blockKeyBuffer;
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Extra block key binding (e.g. V). Use SKSE key name or decimal unified key code.");
            }

            if (ImGui::Checkbox("Explosion VFX on timed block", &settings->bEnableExplosion)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Place an explosion effect on successful timed block.\nConfigure the source mod and FormID in the [Forms] section below.");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Form lookups ([Forms] in INI)");

            if (ImGui::InputText("Perk plugin name", State::formsPerkPluginBuffer, sizeof(State::formsPerkPluginBuffer))) {
                settings->sFormsPerkPluginName = State::formsPerkPluginBuffer;
                State::MarkChanged();
            }
            if (ImGui::InputText("Damage perk FormID (hex)", State::formsDamagePerkBuffer, sizeof(State::formsDamagePerkBuffer))) {
                settings->sFormsDamagePreventPerkID = State::formsDamagePerkBuffer;
                State::MarkChanged();
            }
            if (ImGui::InputText("Stagger perk FormID (hex)", State::formsStaggerPerkBuffer, sizeof(State::formsStaggerPerkBuffer))) {
                settings->sFormsStaggerPerkID = State::formsStaggerPerkBuffer;
                State::MarkChanged();
            }
            if (ImGui::Button("Reload perk forms now")) {
                TimedBlockAddon::GetSingleton()->LoadPerkForms();
            }

            ImGui::Spacing();
            if (ImGui::InputText("Explosion plugin name", State::explosionPluginBuffer, sizeof(State::explosionPluginBuffer))) {
                settings->sExplosionPluginName = State::explosionPluginBuffer;
                State::MarkChanged();
            }
            if (ImGui::InputText("Explosion FormID (hex)", State::explosionFormIDBuffer, sizeof(State::explosionFormIDBuffer))) {
                settings->sExplosionFormID = State::explosionFormIDBuffer;
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Hex FormID of a BGSExplosion in the plugin above.\nReload a save or start a new game to apply changes.");
            }
        }
        
        // ===== HITSTOP SECTION =====
        if (ImGui::CollapsingHeader("Hitstop (Attacker Animation Freeze)", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Hitstop", &settings->bEnableHitstop)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Freezes the attacker's animation on timed block.\nDoes NOT freeze the entire game world!");
            }
            
            if (settings->bEnableHitstop) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Animation Speed", &settings->fHitstopSpeed, 0.0f, 0.3f, "%.2f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How slow the attacker's animation plays during hitstop.\n0.0 = Complete freeze\n0.1 = Very slow motion");
                }
                
                if (ImGui::SliderFloat("Duration (seconds)", &settings->fHitstopDuration, 0.05f, 0.5f, "%.2f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long the hitstop effect lasts.");
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== STAGGER SECTION =====
        if (ImGui::CollapsingHeader("Stagger", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Stagger", &settings->bEnableStagger)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Forces the attacker into a stagger animation on timed block.\nDifferent stagger values for normal vs power attacks.");
            }
            
            if (settings->bEnableStagger) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Normal Attack Stagger", &settings->fStaggerMagnitude, 0.0f, 1.5f, "%.2f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Stagger strength for NORMAL attacks:\n0.0 = Small stagger\n0.3 = Medium stagger\n0.7 = Large stagger (default)\n1.0+ = Largest stagger");
                }
                
                if (ImGui::SliderFloat("Power Attack Stagger", &settings->fPowerAttackStaggerMagnitude, 0.0f, 2.0f, "%.2f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Stagger strength for POWER attacks:\n0.0 = Small stagger\n0.5 = Medium stagger\n1.0 = Large stagger (default)\n1.5+ = Largest stagger\n\nHigher values reward parrying power attacks!");
                }
                
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Skill-Based Stagger Chance");
                
                if (ImGui::Checkbox("Use Skill-Based Chance", &settings->bUseStaggerChance)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, stagger success is based on a percentage chance\nthat scales with your Block and/or Weapon skill.\n\nDisabled = 100%% guaranteed stagger (default)");
                }
                
                if (settings->bUseStaggerChance) {
                    ImGui::Indent();
                    
                    if (ImGui::SliderFloat("Base Chance", &settings->fBaseStaggerChance, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Stagger chance at 0 skill.\nDefault: 50%%");
                    }
                    
                    if (ImGui::SliderFloat("Max Chance", &settings->fMaxStaggerChance, settings->fBaseStaggerChance, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Stagger chance at 100 skill.\nDefault: 100%%");
                    }
                    
                    ImGui::Spacing();
                    ImGui::Text("Skills to Factor:");
                    
                    if (ImGui::Checkbox("Block Skill", &settings->bStaggerUseBlockSkill)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Factor in your Block skill level when calculating stagger chance.");
                    }
                    
                    ImGui::SameLine();
                    
                    if (ImGui::Checkbox("Weapon Skill", &settings->bStaggerUseWeaponSkill)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Factor in your weapon skill (One-Handed, Two-Handed, or Archery)\nbased on your currently equipped weapon.");
                    }
                    
                    // Show calculated chance at player's current skill
                    ImGui::Spacing();
                    auto* player = RE::PlayerCharacter::GetSingleton();
                    if (player) {
                        auto* avOwner = player->AsActorValueOwner();
                        if (avOwner) {
                            float blockSkill = avOwner->GetActorValue(RE::ActorValue::kBlock);
                            
                            // Calculate current chance
                            float totalSkill = 0.0f;
                            int skillCount = 0;
                            if (settings->bStaggerUseBlockSkill) {
                                totalSkill += std::clamp(blockSkill, 0.0f, 100.0f);
                                skillCount++;
                            }
                            if (settings->bStaggerUseWeaponSkill) {
                                // Get weapon skill based on equipped weapon
                                float weaponSkill = 0.0f;
                                auto* rightWeapon = player->GetEquippedObject(false);
                                if (rightWeapon && rightWeapon->IsWeapon()) {
                                    auto* weap = rightWeapon->As<RE::TESObjectWEAP>();
                                    if (weap) {
                                        auto wType = weap->GetWeaponType();
                                        if (wType == RE::WEAPON_TYPE::kTwoHandSword || wType == RE::WEAPON_TYPE::kTwoHandAxe) {
                                            weaponSkill = avOwner->GetActorValue(RE::ActorValue::kTwoHanded);
                                        } else if (wType == RE::WEAPON_TYPE::kBow || wType == RE::WEAPON_TYPE::kCrossbow) {
                                            weaponSkill = avOwner->GetActorValue(RE::ActorValue::kArchery);
                                        } else {
                                            weaponSkill = avOwner->GetActorValue(RE::ActorValue::kOneHanded);
                                        }
                                    }
                                } else {
                                    weaponSkill = avOwner->GetActorValue(RE::ActorValue::kOneHanded);
                                }
                                totalSkill += std::clamp(weaponSkill, 0.0f, 100.0f);
                                skillCount++;
                            }
                            
                            float currentChance = settings->fBaseStaggerChance;
                            if (skillCount > 0) {
                                float avgSkill = totalSkill / static_cast<float>(skillCount);
                                float skillRatio = avgSkill / 100.0f;
                                currentChance = settings->fBaseStaggerChance + 
                                    (settings->fMaxStaggerChance - settings->fBaseStaggerChance) * skillRatio;
                            }
                            
                            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), 
                                "Current stagger chance: %.0f%%", currentChance);
                        }
                    }
                    
                    ImGui::Unindent();
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== CAMERA SHAKE SECTION =====
        if (ImGui::CollapsingHeader("Camera Shake", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Camera Shake", &settings->bEnableCameraShake)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Adds camera shake for impact feel on timed block.");
            }
            
            if (settings->bEnableCameraShake) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Shake Strength", &settings->fCameraShakeStrength, 0.1f, 2.0f, "%.2f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Intensity of the camera shake.");
                }
                
                if (ImGui::SliderFloat("Shake Duration (seconds)", &settings->fCameraShakeDuration, 0.05f, 0.5f, "%.2f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long the camera shake lasts.");
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== STAMINA RESTORATION SECTION =====
        if (ImGui::CollapsingHeader("Stamina Restoration", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Stamina Restore", &settings->bEnableStaminaRestore)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Restores a percentage of your max stamina on successful timed block.\nDefault: Enabled with 100%% restore (full refill)");
            }
            
            if (settings->bEnableStaminaRestore) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Restore Amount", &settings->fStaminaRestorePercent, 0.0f, 100.0f, "%.0f%%")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Percentage of max stamina to restore.\n100%% = Full refill\n50%% = Half refill\n\nReward for timing your blocks!");
                }
                
                // Show calculated restore amount for player
                auto* player = RE::PlayerCharacter::GetSingleton();
                if (player) {
                    auto* avOwner = player->AsActorValueOwner();
                    if (avOwner) {
                        float maxStamina = avOwner->GetPermanentActorValue(RE::ActorValue::kStamina);
                        float restoreAmount = maxStamina * (settings->fStaminaRestorePercent / 100.0f);
                        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), 
                            "Current: %.0f stamina restored (max: %.0f)", restoreAmount, maxStamina);
                    }
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== SOUND SECTION =====
        if (ImGui::CollapsingHeader("Sound")) {
            if (ImGui::Checkbox("Enable Sound", &settings->bEnableSound)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Plays a sound effect on timed block.");
            }
            
            if (settings->bEnableSound) {
                ImGui::Indent();
                
                if (ImGui::Checkbox("Use Custom WAV File", &settings->bUseCustomWav)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, plays a custom WAV file instead of a game sound.\n\nFile location: Data/SKSE/Plugins/TimedBlockDodgeCounter/timedblock.wav");
                }
                
                if (!settings->bUseCustomWav) {
                    if (ImGui::InputText("Sound Descriptor", State::soundPathBuffer, sizeof(State::soundPathBuffer))) {
                        settings->sSoundPath = State::soundPathBuffer;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Sound descriptor EditorID to play on timed block.\nExamples: UIMenuOK, NPCHumanCombatShieldBlock, MAGImpactStagger");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), 
                        "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/timedblock.wav");
                    
                    // Volume slider for custom WAV
                    float volumePercent = settings->fCustomWavVolume * 100.0f;
                    if (ImGui::SliderFloat("WAV Volume", &volumePercent, 0.0f, 100.0f, "%.0f%%")) {
                        settings->fCustomWavVolume = volumePercent / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Volume for custom WAV file playback.\n\nNote: This adjusts the Windows wave output volume,\nwhich may affect other sounds briefly.");
                    }
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Counter Strike Sound");
            
            if (ImGui::Checkbox("Enable Counter Strike Sound", &settings->bEnableCounterStrikeSound)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Play a sound when a counter attack hit connects.\n\nSound file: Data/SKSE/Plugins/TimedBlockDodgeCounter/counterstrike.wav");
            }
            
            if (settings->bEnableCounterStrikeSound) {
                ImGui::Indent();
                
                float volumePercent = settings->fCounterStrikeSoundVolume * 100.0f;
                if (ImGui::SliderFloat("Counter Strike Volume", &volumePercent, 0.0f, 100.0f, "%.0f%%")) {
                    settings->fCounterStrikeSoundVolume = volumePercent / 100.0f;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Volume of the counter strike sound effect.");
                }
                
                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), 
                    "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/counterstrike.wav");
                
                ImGui::Unindent();
            }
        }
        
        // ===== SLOWMO SECTION =====
        if (ImGui::CollapsingHeader("Slowmo Effect")) {
            if (ImGui::Checkbox("Enable Slowmo", &settings->bEnableSlowmo)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Slows down the entire game world on successful timed block.\nCreates a dramatic 'Matrix' style effect.");
            }
            
            if (settings->bEnableSlowmo) {
                ImGui::Indent();
                
                // Display speed as percentage (multiply by 100 for display)
                float speedPercent = settings->fSlowmoSpeed * 100.0f;
                if (ImGui::SliderFloat("Slowmo Speed", &speedPercent, 5.0f, 50.0f, "%.0f%%")) {
                    settings->fSlowmoSpeed = speedPercent / 100.0f;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How slow the world moves during slowmo.\n25%% = quarter speed (default)\n5%% = very slow\n50%% = half speed");
                }
                
                if (ImGui::SliderFloat("Slowmo Duration (sec)", &settings->fSlowmoDuration, 0.1f, 2.0f, "%.2f sec")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long the slowmo effect lasts (in real-time seconds).");
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== COUNTER ATTACK SECTION =====
        if (ImGui::CollapsingHeader("Counter Attack")) {
            if (ImGui::Checkbox("Enable Counter Attack", &settings->bEnableCounterAttack)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("After a successful timed block, pressing attack will\ncancel the block reaction animation and immediately\nstart an attack animation.\n\nAllows for quick counter attacks like in Elden Ring.");
            }
            
            if (settings->bEnableCounterAttack) {
                ImGui::Indent();
                
                float windowMs = settings->fCounterAttackWindow * 1000.0f;
                if (ImGui::SliderFloat("Counter Window (ms)", &windowMs, 100.0f, 1000.0f, "%.0f ms")) {
                    settings->fCounterAttackWindow = windowMs / 1000.0f;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long after a timed block you have to press attack\nto trigger a counter attack.\n\n500ms (0.5 sec) is the default.");
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Spacing();
            
            if (ImGui::Checkbox("Prevent Player Stagger", &settings->bPreventPlayerStagger)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Prevents the player from being staggered when performing\na successful timed block.\n\nThis stops heavy/power attacks from causing a stagger\nreaction even on a perfect timed block.\n\nEnabled by default.");
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Counter Attack Damage Bonus");
            
            if (ImGui::Checkbox("Enable Damage Bonus", &settings->bEnableCounterDamageBonus)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Increases the damage of the first hit after a counter attack.\nThe bonus is removed after landing the hit.\n\nGreat for rewarding aggressive play!");
            }
            
            if (settings->bEnableCounterDamageBonus) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Damage Bonus", &settings->fCounterDamageBonusPercent, 10.0f, 200.0f, "+%.0f%%")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How much extra damage the counter attack deals.\n\n+50%% = 1.5x damage (default)\n+100%% = 2x damage\n+200%% = 3x damage");
                }
                
                if (ImGui::SliderFloat("Damage Timeout", &settings->fCounterDamageBonusTimeout, 0.5f, 3.0f, "%.1f sec")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long the damage bonus lasts after initiating the counter attack.\nIf you don't land a hit within this time, the bonus expires.\n\nDefault: 1.0 seconds");
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Counter Attack Lunge");
            
            if (ImGui::Checkbox("Enable Lunge", &settings->bEnableCounterLunge)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("When you counter attack, lunge toward the attacker.\nCreates a more aggressive counter attack feeling.\n\nRequires 'Enable Counter Attack' to be on.");
            }
            
            if (settings->bEnableCounterLunge) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Lunge Distance", &settings->fCounterLungeDistance, 50.0f, 500.0f, "%.0f units")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Maximum distance to lunge toward the attacker.\n\n100-150 = short lunge\n200-300 = medium lunge\n400-500 = long lunge");
                }
                
                if (ImGui::SliderFloat("Lunge Speed", &settings->fCounterLungeSpeed, 0.1f, 50.0f, "%.1f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Speed of the lunge movement.\nHigher = faster dash, lower = smoother glide.\n\n400 = slow\n800 = default\n1500 = fast dash");
                }
                
                {
                    static const char* kCurveItems[] = {
                        "Bell (Ease In-Out)", "Linear", "Ease In", "Ease Out", "Cubic In", "Cubic Out"
                    };
                    if (ImGui::Combo("Lunge Curve##tb", &settings->iCounterLungeCurve, kCurveItems, 6)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Velocity profile for the lunge:\n"
                            "  Bell (Ease In-Out) - smooth start AND stop (default)\n"
                            "  Linear             - constant speed throughout\n"
                            "  Ease In            - slow start, fast finish\n"
                            "  Ease Out           - fast start, slow stop\n"
                            "  Cubic In           - strong acceleration from rest\n"
                            "  Cubic Out          - strong deceleration into target");
                    }
                }
                
                if (ImGui::SliderFloat("Stop Distance##tb", &settings->fCounterLungeMeleeStopDistance, 32.0f, 400.0f, "%.0f units")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How far from the target the lunge stops (timed block counter).\nLower = end closer (daggers / aggressive)\nHigher = keep more spacing (greatswords / safer)\n\nDefault: 128 units");
                }
                
                ImGui::Unindent();
            }
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Counter Attack Slow Time");
            
            if (ImGui::Checkbox("Enable Counter Slow Time", &settings->bEnableCounterSlowTime)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Slow down time when you start a counter attack.\nSlowdown lasts until the specified animation event fires.\n\nCreates a 'Sekiro' style dramatic counter effect.");
            }
            
            if (settings->bEnableCounterSlowTime) {
                ImGui::Indent();
                
                float scalePercent = settings->fCounterSlowTimeScale * 100.0f;
                if (ImGui::SliderFloat("Slow Time Amount", &scalePercent, 5.0f, 75.0f, "%.0f%%")) {
                    settings->fCounterSlowTimeScale = scalePercent / 100.0f;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How slow time moves during counter slow time.\n25%% = quarter speed (default)\n10%% = very slow\n50%% = half speed");
                }
                
                if (ImGui::SliderFloat("Max Duration (sec)", &settings->fCounterSlowTimeMaxDuration, 0.5f, 5.0f, "%.1f sec")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Maximum duration of slow time if the end event is not detected.\nFallback to prevent infinite slow time.\n\n2 seconds is usually enough for most attack animations.");
                }
                
                ImGui::Spacing();
                ImGui::Text("Start Trigger:");
                
                if (ImGui::Checkbox("Start After Lunge", &settings->bCounterSlowStartAfterLunge)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, slow time starts AFTER the lunge ends.\nWhen disabled, slow time starts on the Start Event.\n\nUseful for dramatic effect when the player\narrives at the target and begins attacking.");
                }
                
                static char startEventBuffer[64];
                static char endEventBuffer[64];
                static bool bufferInitialized = false;
                
                if (!bufferInitialized) {
                    strncpy_s(startEventBuffer, settings->sCounterSlowStartEvent.c_str(), sizeof(startEventBuffer) - 1);
                    strncpy_s(endEventBuffer, settings->sCounterSlowEndEvent.c_str(), sizeof(endEventBuffer) - 1);
                    bufferInitialized = true;
                }
                
                if (!settings->bCounterSlowStartAfterLunge) {
                    if (ImGui::InputText("Start Event", startEventBuffer, sizeof(startEventBuffer))) {
                        settings->sCounterSlowStartEvent = startEventBuffer;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Animation event that STARTS slow time.\nCommon events:\n  attackStart - weapon attack begins\n  AttackWinStart - attack window opens\n\nUses partial matching (case-insensitive).");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "Slow time starts when lunge ends");
                }
                
                ImGui::Spacing();
                ImGui::Text("End Trigger:");
                
                if (ImGui::InputText("End Event", endEventBuffer, sizeof(endEventBuffer))) {
                    settings->sCounterSlowEndEvent = endEventBuffer;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Animation event that ENDS slow time.\nCommon events:\n  weaponSwing - weapon swing moment\n  HitFrame - actual hit frame\n  AttackWinEnd - attack window closes\n\nUses partial matching (case-insensitive).");
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== COOLDOWN SECTION =====
        if (ImGui::CollapsingHeader("Timed Block Cooldown", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Cooldown", &settings->bEnableCooldown)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Prevents spamming the block button for timed blocks.\nIf you block but fail to trigger a timed block within the window,\nyou must wait for the cooldown before attempting another timed block.\n\nSuccessful timed blocks CLEAR the cooldown (allows consecutive blocks).");
            }
            
            if (settings->bEnableCooldown) {
                ImGui::Indent();
                
                if (ImGui::SliderFloat("Cooldown Duration (ms)", &settings->fCooldownDurationMs, 0.0f, 1000.0f, "%.0f ms")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long after a failed timed block attempt before you can try again.\n250ms is recommended for balanced gameplay.\n\nBlocking during cooldown RESTARTS the timer!");
                }
                
                ImGui::Spacing();
                
                if (ImGui::Checkbox("Ignore Cooldown Outside Combat Range", &settings->bIgnoreCooldownOutsideRange)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, cooldown is ignored if there are no NPCs\nactively fighting YOU within the specified distance.\n\nUseful to allow timed blocks against distant archers\nwhile still preventing spam against nearby melee enemies.");
                }
                
                if (settings->bIgnoreCooldownOutsideRange) {
                    if (ImGui::SliderFloat("Combat Detection Range", &settings->fCooldownIgnoreDistance, 128.0f, 2048.0f, "%.0f units")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Cooldown is ignored if no hostile NPCs in combat\nare within this distance.\n\n512 = close range (melee)\n1024 = medium range\n2048 = long range");
                    }
                }
                
                ImGui::Unindent();
            }
        }
        
        // ===== TIMED DODGE SECTION =====
        if (ImGui::CollapsingHeader("Timed Dodge", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Timed Dodge", &settings->bEnableTimedDodge)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Dodge at the precise moment an enemy attacks to trigger\nslow-motion, i-frames, and radial blur.\n\nWorks with DMCO, TK Dodge RE, and Ultimate Dodge.");
            }
            
            if (settings->bEnableTimedDodge) {
                ImGui::Indent();
                
                // Slow-motion settings
                ImGui::Spacing();
                ImGui::Text("Slow Motion");
                ImGui::Separator();
                
                if (ImGui::SliderFloat("Slomo Duration (sec)##dodge", &settings->fTimedDodgeSlomoDuration, 1.0f, 10.0f, "%.1f sec")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long the slow-motion effect lasts (real-time seconds).\nDefault: 4 seconds");
                }
                
                float dodgeSpeedPercent = settings->fTimedDodgeSlomoSpeed * 100.0f;
                if (ImGui::SliderFloat("Game Speed##dodge", &dodgeSpeedPercent, 1.0f, 50.0f, "%.0f%%")) {
                    settings->fTimedDodgeSlomoSpeed = dodgeSpeedPercent / 100.0f;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How slow the game world moves during the effect.\n5%% = very slow (default)\n25%% = quarter speed\n50%% = half speed");
                }
                
                if (ImGui::SliderFloat("Cooldown (sec)##dodge", &settings->fTimedDodgeCooldown, 0.5f, 15.0f, "%.1f sec")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Cooldown before another timed dodge can trigger.\nPrevents spamming. Default: 3 seconds");
                }

                if (ImGui::SliderFloat("Damage Cooldown (sec)##dodge", &settings->fTimedDodgeDamageCooldown, 0.0f, 30.0f, "%.1f sec")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("After taking damage, timed dodge cannot trigger for this duration.\nSet to 0 to disable. Default: 5 seconds");
                }
                
                if (ImGui::SliderFloat("Detection Range##dodge", &settings->fTimedDodgeDetectionRange, 100.0f, 600.0f, "%.0f units")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How close an attacking enemy must be for the timed dodge\nto trigger. Lower = requires closer timing.\n\n200 = very close (dagger range)\n300 = default (sword range)\n500 = generous (greatsword range)");
                }

                if (ImGui::SliderFloat("Forgiveness (ms)##dodge", &settings->fTimedDodgeForgivenessMs, 0.0f, 500.0f, "%.0f ms")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Grace period for dodging slightly too early.\nIf you dodge before the enemy commits to a swing,\nthe game will check for an attack over this window.\n\n0 = no forgiveness (frame-perfect)\n200 = default (generous)\n500 = very forgiving\n\nOnly helps early dodges. Late dodges still get hit.");
                }
                
                // I-Frames
                ImGui::Spacing();
                ImGui::Text("I-Frames (Invulnerability)");
                ImGui::Separator();
                
                if (ImGui::Checkbox("Enable I-Frames##dodge", &settings->bTimedDodgeIframes)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Player cannot be damaged during the slow-motion effect.\nDuration matches the slow-motion duration.");
                }
                
                // Counter Attack
                ImGui::Spacing();
                ImGui::Text("Counter Attack During Timed Dodge");
                ImGui::Separator();
                
                if (ImGui::Checkbox("Enable Counter Attack##dodge", &settings->bTimedDodgeCounterAttack)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Attack during slow-motion to cancel the effect and perform\na counter attack. The input window is set below (independent\nof the timed block counter window).");
                }

                if (settings->bTimedDodgeCounterAttack) {
                    ImGui::Indent();

                    if (ImGui::SliderFloat("Counter Window (ms)##dodge", &settings->fTimedDodgeCounterWindowMs, 100.0f, 5000.0f, "%.0f ms")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long after a timed dodge you have to press attack\nto trigger a counter attack.\n\nIndependent of slow-motion duration.\n\nDefault: 2000 ms");
                    }

                    if (ImGui::SliderFloat("Damage Bonus##dodge", &settings->fTimedDodgeCounterDamagePercent, 10.0f, 200.0f, "+%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How much extra damage the counter attack deals during timed dodge.\nOverrides the timed block counter damage when they differ.\n\n+50%% = 1.5x damage (default)\n+100%% = 2x damage\n+200%% = 3x damage");
                    }

                    if (ImGui::SliderFloat("Damage Timeout##dodge", &settings->fTimedDodgeCounterDamageTimeout, 0.5f, 5.0f, "%.1f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long the damage bonus lasts after initiating the counter attack.\nNeeds to be long enough for the dodge exit animation to finish\nand your attack to connect.\n\nDefault: 3.0 seconds");
                    }

                    if (ImGui::Checkbox("Counter Lunge##dodge", &settings->bTimedDodgeCounterLunge)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Lunge toward the attacker when performing a counter attack\nduring a timed dodge. Max travel and melee stop distance match\nthe Counter Attack section; lunge speed is set below.\n\nSeparate from the timed block counter lunge toggle.");
                    }

                    if (settings->bTimedDodgeCounterLunge) {
                        ImGui::Indent();

                        if (ImGui::SliderFloat("Lunge Speed##dodgeCounter", &settings->fTimedDodgeCounterLungeSpeed, 0.1f, 50.0f, "%.1f")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Speed of the timed dodge counter lunge.\nHigher = faster dash, lower = smoother glide.\n\n400 = slow\n800 = default\n1500 = fast dash");
                        }

                        {
                            static const char* kCurveItems[] = {
                                "Bell (Ease In-Out)", "Linear", "Ease In", "Ease Out", "Cubic In", "Cubic Out"
                            };
                            if (ImGui::Combo("Lunge Curve##dodge", &settings->iTimedDodgeCounterLungeCurve, kCurveItems, 6)) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip(
                                    "Velocity profile for the timed dodge counter lunge:\n"
                                    "  Bell (Ease In-Out) - smooth start AND stop (default)\n"
                                    "  Linear             - constant speed throughout\n"
                                    "  Ease In            - slow start, fast finish\n"
                                    "  Ease Out           - fast start, slow stop\n"
                                    "  Cubic In           - strong acceleration from rest\n"
                                    "  Cubic Out          - strong deceleration into target");
                            }
                        }

                        if (ImGui::SliderFloat("Stop Distance##dodgeCounter", &settings->fTimedDodgeCounterLungeMeleeStopDistance, 32.0f, 400.0f, "%.0f units")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("How far from the target the lunge stops (timed dodge counter).\nLower = end closer (daggers / aggressive)\nHigher = keep more spacing (greatswords / safer)\n\nDefault: 128 units");
                        }

                        ImGui::Unindent();
                    }

                    ImGui::Spacing();

                    if (ImGui::Checkbox("Spell Counter Attack##dodge", &settings->bTimedDodgeCounterSpellHit)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Allow casting a counter-attack spell after a timed dodge.\nThe dodge animation is cancelled and the spell receives bonus damage on hit.\n\nDefault: On");
                    }

                    if (settings->bTimedDodgeCounterSpellHit) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("Spell Damage Bonus##dodge", &settings->fTimedDodgeCounterSpellDamagePercent, 10.0f, 500.0f, "+%.0f%%")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Extra damage added to a counter-attack spell fired after a timed dodge.\n+50%% means a 20-damage spell deals 30.\n\nDefault: +50%%");
                        }
                        ImGui::Unindent();
                    }

                    ImGui::Spacing();

                    if (ImGui::Checkbox("Ranged Counter Attack##dodge", &settings->bTimedDodgeCounterRanged)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Allow firing a counter-attack arrow/bolt after a timed dodge.\nCancels the dodge animation and temporarily speeds up draw time.\n\nDefault: On");
                    }

                    if (settings->bTimedDodgeCounterRanged) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("Ranged Damage Bonus##dodge", &settings->fTimedDodgeCounterRangedDamagePercent, 10.0f, 500.0f, "+%.0f%%")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Extra damage added to a counter-attack arrow/bolt after a timed dodge.\n+50%% means a 15-damage arrow deals 22.5.\n\nDefault: +50%%");
                        }

                        if (ImGui::SliderFloat("Ranged Window (ms)##dodge", &settings->fTimedDodgeCounterRangedWindowMs, 500.0f, 10000.0f, "%.0f ms")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("How long you have to draw and fire the bow/crossbow after the dodge.\nThe bonus is consumed on the first arrow/bolt hit.\n\nDefault: 2500 ms");
                        }

                        if (ImGui::SliderFloat("Draw Speed Multiplier##dodge", &settings->fTimedDodgeCounterDrawSpeedMult, 1.0f, 10.0f, "%.1fx")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Temporary draw speed multiplier for the counter shot.\n1.0x = normal speed, 2.0x = twice as fast.\nApplied once and removed after the arrow is fired.\n\nDefault: 2.0x");
                        }

                        if (ImGui::Checkbox("Counter Shot Sound##dodge", &settings->bTimedDodgeCounterRangedSound)) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Play a sound effect when the counter arrow/bolt hits an enemy.\n\nDefault: On");
                        }
                        ImGui::Unindent();
                    }

                    ImGui::Unindent();
                }
                
                // Apply Block Effects
                ImGui::Spacing();
                ImGui::Text("Timed Block Effects on Attacker");
                ImGui::Separator();
                
                if (ImGui::Checkbox("Apply Block Effects##dodge", &settings->bTimedDodgeApplyBlockEffects)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Apply the timed block visual effects to the attacker:\nhitstop, camera shake, stamina restore, etc.\n\nUses the settings from the timed block sections above.\nStagger and sound are controlled separately below.");
                }

                // Stagger
                ImGui::Spacing();
                ImGui::Text("Stagger Attacker");
                ImGui::Separator();

                if (ImGui::Checkbox("Stagger on Timed Dodge##dodge", &settings->bTimedDodgeStagger)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Force the attacker into a stagger animation on timed dodge.\nDisabled by default (the attacker slow is usually enough).");
                }

                // Attacker Animation Slow
                ImGui::Spacing();
                ImGui::Text("Attacker Animation Slow");
                ImGui::Separator();

                if (ImGui::Checkbox("Slow Attacker##dodge", &settings->bTimedDodgeAttackerSlow)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Slow the attacker's animation speed on timed dodge.\nThis only affects the attacker, not the whole game world.\nStacks with the global slow-motion effect.");
                }

                if (settings->bTimedDodgeAttackerSlow) {
                    ImGui::Indent();

                    float atkSlowPercent = settings->fTimedDodgeAttackerSlowSpeed * 100.0f;
                    if (ImGui::SliderFloat("Attacker Anim Speed##dodge", &atkSlowPercent, 1.0f, 50.0f, "%.0f%%")) {
                        settings->fTimedDodgeAttackerSlowSpeed = atkSlowPercent / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How slow the attacker's animation plays.\n5%% = nearly frozen (default)\n25%% = quarter speed\n50%% = half speed");
                    }

                    if (ImGui::SliderFloat("Slow Duration (sec)##atkSlow", &settings->fTimedDodgeAttackerSlowDuration, 0.5f, 5.0f, "%.1f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long the attacker's animation is slowed.\nDefault: 1.5 seconds");
                    }

                    ImGui::Unindent();
                }

                // Sound
                ImGui::Spacing();
                ImGui::Text("Sound");
                ImGui::Separator();

                if (ImGui::Checkbox("Enable Timed Dodge Sound##dodge", &settings->bTimedDodgeSound)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Play a sound effect on successful timed dodge.\n\nWAV file: Data/SKSE/Plugins/TimedBlockDodgeCounter/timeddodge.wav");
                }

                if (settings->bTimedDodgeSound) {
                    ImGui::Indent();

                    float dodgeVolPercent = settings->fTimedDodgeSoundVolume * 100.0f;
                    if (ImGui::SliderFloat("Dodge Sound Volume##dodge", &dodgeVolPercent, 0.0f, 100.0f, "%.0f%%")) {
                        settings->fTimedDodgeSoundVolume = dodgeVolPercent / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Volume of the timed dodge sound effect.");
                    }

                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                        "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/timeddodge.wav");

                    ImGui::Unindent();
                }
                
                // Radial Blur
                ImGui::Spacing();
                ImGui::Text("Radial Blur");
                ImGui::Separator();
                
                if (ImGui::Checkbox("Enable Radial Blur##dodge", &settings->bTimedDodgeRadialBlur)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Apply a radial blur effect during the slow-motion.\nFades in smoothly and fades out when slomo ends.");
                }
                
                if (settings->bTimedDodgeRadialBlur) {
                    ImGui::Indent();
                    
                    if (ImGui::SliderFloat("Blur Strength##dodge", &settings->fTimedDodgeBlurStrength, 0.05f, 1.0f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Intensity of the radial blur effect.\n0.3 = subtle (default)\n0.5 = moderate\n1.0 = very strong");
                    }
                    
                    if (ImGui::SliderFloat("Blend Speed##dodge", &settings->fTimedDodgeBlurBlendSpeed, 1.0f, 20.0f, "%.1f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How fast the blur fades in and out.\nHigher = faster fade. Default: 5.0");
                    }
                    
                    if (ImGui::SliderFloat("Ramp Up##dodge", &settings->fTimedDodgeBlurRampUp, 0.0f, 0.5f, "%.2f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("IMOD ramp up time in seconds.\nDefault: 0.1");
                    }
                    
                    if (ImGui::SliderFloat("Ramp Down##dodge", &settings->fTimedDodgeBlurRampDown, 0.0f, 1.0f, "%.2f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("IMOD ramp down time in seconds.\nDefault: 0.2");
                    }
                    
                    if (ImGui::SliderFloat("Center Radius##dodge", &settings->fTimedDodgeBlurRadius, 0.0f, 1.0f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Blur start radius (center clarity).\n0.0 = blur from center\n0.4 = moderate clarity (default)\n1.0 = blur only at edges");
                    }
                    
                    ImGui::Unindent();
                }
                
                ImGui::Unindent();
            }
        }

        // ===== WARD TIMED BLOCK =====
        if (ImGui::CollapsingHeader("Ward Timed Block", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Enable Ward Timed Block", &settings->bEnableWardTimedBlock)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Melee-only: while a ward spell is active, a short window opens after the ward applies.\n"
                    "If a melee attack hits during that window, damage is cancelled and timed-block effects\n"
                    "(hitstop, stagger on attacker, etc.) run — without forcing block animations on the player.");
            }

            if (settings->bEnableWardTimedBlock) {
                ImGui::Indent();

                if (ImGui::SliderFloat("Timing Window (ms)", &settings->fWardTimedBlockWindowMs, 50.0f, 2000.0f, "%.0f ms")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long after the ward applies you can parry a melee hit (default more forgiving than shield parry).");
                }

                if (ImGui::Checkbox("Cancel Melee Damage", &settings->bWardTimedBlockDamageCancel)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Restore health to cancel the melee hit that landed during the ward window.");
                }

                ImGui::Spacing();
                ImGui::Text("Stagger on attacker");
                ImGui::Separator();
                if (ImGui::Checkbox("Enable Ward Stagger", &settings->bWardTimedBlockStagger)) {
                    State::MarkChanged();
                }
                if (settings->bWardTimedBlockStagger) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("1H Ward Stagger", &settings->fWardSmallStaggerMagnitude, 0.0f, 2.0f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Stagger magnitude when only one hand is casting a ward.");
                    }
                    if (ImGui::SliderFloat("2H Dual-Cast Ward Stagger", &settings->fWardLargeStaggerMagnitude, 0.0f, 2.0f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Stagger magnitude when both hands are casting ward (dual-cast).");
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                ImGui::Text("Sound");
                ImGui::Separator();
                if (ImGui::Checkbox("Ward Timed Block Sound", &settings->bWardTimedBlockSound)) {
                    State::MarkChanged();
                }
                if (settings->bWardTimedBlockSound) {
                    ImGui::Indent();
                    float wv = settings->fWardTimedBlockSoundVolume * 100.0f;
                    if (ImGui::SliderFloat("Volume##wardtb", &wv, 0.0f, 100.0f, "%.0f%%")) {
                        settings->fWardTimedBlockSoundVolume = wv / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::InputText("WAV filename##wardtb", State::wardSoundFileBuffer, sizeof(State::wardSoundFileBuffer))) {
                        settings->sWardTimedBlockSoundFile = State::wardSoundFileBuffer;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("File placed in Data/SKSE/Plugins/TimedBlockDodgeCounter/");
                    }
                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                        "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/%s", settings->sWardTimedBlockSoundFile.c_str());
                    ImGui::Unindent();
                }

                ImGui::Spacing();
                ImGui::Text("Counter attack");
                ImGui::Separator();
                if (ImGui::Checkbox("Enable Ward Counter Window", &settings->bWardTimedBlockCounterAttack)) {
                    State::MarkChanged();
                }
                if (settings->bWardTimedBlockCounterAttack) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Counter Window (ms)##ward", &settings->fWardCounterWindowMs, 50.0f, 10000.0f, "%.0f ms")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long after the ward timed block the player has to land a counter attack or spell hit.");
                    }
                    if (ImGui::SliderFloat("Counter damage bonus %%##ward", &settings->fWardCounterDamagePercent, 0.0f, 500.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Extra damage applied on top of the counter attack (melee or spell) after a successful ward timed block.\n0%% = no bonus. 50%% = +50%% of the hit's base damage.");
                    }
                    if (ImGui::Checkbox("Spell / magic projectile can satisfy counter", &settings->bWardCounterSpellHit)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "First qualifying spell hit (concentration or spell projectile) can apply the counter damage bonus.\n"
                            "Physical arrows/bolts are ignored.");
                    }
                    if (settings->bWardCounterSpellHit) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("Spell in-flight timeout (ms)##ward", &settings->fWardCounterSpellInFlightMs, 100.0f, 30000.0f, "%.0f ms")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "After a counter spell is CAST, how long the bonus stays alive while the projectile travels\n"
                                "or a concentration spell starts hitting. If nothing lands in this time the bonus expires.\n"
                                "Default: 5000ms (5 seconds).");
                        }
                        if (ImGui::Checkbox("Spell counter sound", &settings->bWardCounterSpellSound)) {
                            State::MarkChanged();
                        }
                        if (settings->bWardCounterSpellSound) {
                            float sv = settings->fWardCounterSpellSoundVolume * 100.0f;
                            if (ImGui::SliderFloat("Spell counter volume##ward", &sv, 0.0f, 100.0f, "%.0f%%")) {
                                settings->fWardCounterSpellSoundVolume = sv / 100.0f;
                                State::MarkChanged();
                            }
                            if (ImGui::InputText("Spell counter WAV##ward", State::wardCounterSpellFileBuffer, sizeof(State::wardCounterSpellFileBuffer))) {
                                settings->sWardCounterSpellSoundFile = State::wardCounterSpellFileBuffer;
                                State::MarkChanged();
                            }
                            ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                                "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/%s", settings->sWardCounterSpellSoundFile.c_str());
                        }
                        ImGui::Unindent();
                    }
                    ImGui::Unindent();
                }

                if (ImGui::SliderFloat("Ward timed block cooldown (sec)", &settings->fWardTimedBlockCooldown, 0.0f, 10.0f, "%.1f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Minimum time before a new ward window can start after a successful parry.");
                }

                ImGui::Spacing();

                if (ImGui::SliderFloat("Melee detection range (units)##wardrange", &settings->fWardMeleeDetectionRange, 50.0f, 1000.0f, "%.0f")) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Maximum distance (game units) between the player and attacker for a ward timed block to trigger.\n1H weapon reach ~130u, 2H ~200u. Default 300 gives generous slack.");
                }

                ImGui::Spacing();

                if (ImGui::Checkbox("Omnidirectional ward##wardOmni", &settings->bWardOmnidirectional)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("By default the ward only parries attacks from the front 180\xc2\xb0 (matching vanilla ward coverage).\nEnable this to parry attacks from all directions.");
                }

                ImGui::Spacing();

                if (ImGui::Checkbox("Require 2H ward for 2H weapon attacks##ward2h", &settings->bWardRequire2HForTwoHanders)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, only a dual-cast (both hands) ward can parry two-handed weapon attacks.\nA single-hand ward can only parry one-handed attacks.");
                }

                ImGui::Spacing();

                if (ImGui::Checkbox("Restore magicka on ward timed block##wardmp", &settings->bWardTimedBlockMagickaRestore)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Restore a portion of the player's magicka on a successful ward timed block.");
                }
                if (settings->bWardTimedBlockMagickaRestore) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Magicka restore %%##wardmpslider", &settings->fWardMagickaRestorePercent, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Percentage of maximum magicka restored on a successful ward timed block.");
                    }
                    ImGui::Unindent();
                }

                ImGui::Unindent();
            }
        }
        
        // ===== GENERAL SECTION =====
        if (ImGui::CollapsingHeader("General", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Window exclusion (ms)", &settings->fWindowExclusionMs, 0.0f, 5000.0f, "%.0f ms")) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Minimum time between timed block, ward timed block, and timed dodge windows.\n"
                    "Prevents chaining one into another (e.g. ward parry then immediately dodge).\n"
                    "0 = no restriction.");
            }
        }

        // ===== DEBUG SECTION =====
        if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Checkbox("Debug Logging", &settings->bDebugLogging)) {
                State::MarkChanged();
                if (settings->bDebugLogging) {
                    spdlog::set_level(spdlog::level::debug);
                    spdlog::flush_on(spdlog::level::debug);
                    logger::info("=== DEBUG LOGGING ENABLED ===");
                    RE::DebugNotification("[TB Debug] Debug logging ENABLED");
                } else {
                    spdlog::set_level(spdlog::level::info);
                    spdlog::flush_on(spdlog::level::info);
                    logger::info("Debug logging DISABLED");
                    RE::DebugNotification("Debug logging disabled");
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Enable detailed log file output.\nLog: Documents/My Games/Skyrim Special Edition/SKSE/TimedBlockDodgeCounter.log\n\nOn-screen notifications are controlled separately below.");
            }

            if (settings->bDebugLogging) {
                ImGui::Spacing();
                ImGui::Text("On-Screen Notifications:");
                ImGui::Indent();

                if (ImGui::Checkbox("Timed Block##dbgScreen", &settings->bDebugScreenTimedBlock)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Show timed block debug notifications on screen.\nCovers: parry success/failure, cooldown, stagger, stamina.");
                }

                if (ImGui::Checkbox("Counter Attack##dbgScreen", &settings->bDebugScreenCounterAttack)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Show counter attack debug notifications on screen.\nCovers: counter window, damage bonus, slow time, lunge,\nspell counter, ranged counter.");
                }

                if (ImGui::Checkbox("Ward Timed Block##dbgScreen", &settings->bDebugScreenWard)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Show ward timed block debug notifications on screen.\nCovers: ward detection, parry window, hit detection, cooldown.");
                }

                if (ImGui::Checkbox("Timed Dodge##dbgScreen", &settings->bDebugScreenDodge)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Show timed dodge debug notifications on screen.\nCovers: dodge activation, slomo, i-frames.");
                }

                ImGui::Unindent();

                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Debug Status:");
                ImGui::Text("  Cooldown Enabled: %s", settings->bEnableCooldown ? "YES" : "NO");
                ImGui::Text("  Ignore Outside Range: %s", settings->bIgnoreCooldownOutsideRange ? "YES" : "NO");
                ImGui::Text("  Detection Range: %.0f units", settings->fCooldownIgnoreDistance);
            }
        }
        
        ImGui::Separator();
        
        // ===== ACTION BUTTONS =====
        if (ImGui::Button("Save to INI")) {
            settings->SaveSettings();
            State::hasUnsavedChanges = false;
            RE::DebugNotification("Simple Timed Block Addons: Settings saved!");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Save current settings to the INI file.\nChanges will persist across game sessions.");
        }
        
        ImGui::SameLine();
        
        if (ImGui::Button("Revert to INI")) {
            settings->LoadSettings();
            // Refresh sound path buffer
            strncpy_s(State::soundPathBuffer, settings->sSoundPath.c_str(), sizeof(State::soundPathBuffer) - 1);
            State::soundPathBuffer[sizeof(State::soundPathBuffer) - 1] = '\0';
            strncpy_s(State::wardSoundFileBuffer, settings->sWardTimedBlockSoundFile.c_str(), sizeof(State::wardSoundFileBuffer) - 1);
            State::wardSoundFileBuffer[sizeof(State::wardSoundFileBuffer) - 1] = '\0';
            strncpy_s(State::wardCounterSpellFileBuffer, settings->sWardCounterSpellSoundFile.c_str(), sizeof(State::wardCounterSpellFileBuffer) - 1);
            State::wardCounterSpellFileBuffer[sizeof(State::wardCounterSpellFileBuffer) - 1] = '\0';
            State::hasUnsavedChanges = false;
            RE::DebugNotification("Simple Timed Block Addons: Settings reverted to INI!");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Discard session changes and reload settings from INI file.");
        }
        
        // Show note about session changes
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), 
            "Changes apply IMMEDIATELY to your current session.");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), 
            "Use 'Save to INI' to keep changes after restarting the game.");
    }
}
