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

        if (ImGui::BeginTabBar("SettingsPages", ImGuiTabBarFlags_None)) {

        // ===== TIMED BLOCK =====
        if (ImGui::BeginTabItem("Timed Block")) {

            // --- Timing ---
            if (ImGui::TreeNodeEx("Timing##tb", ImGuiTreeNodeFlags_DefaultOpen)) {

                if (ImGui::SliderFloat("Parry Window Duration (ms)##tb", &settings->fParryWindowDurationMs, 50.0f, 1000.0f, "%.0f ms")) {
                    State::MarkChanged();
                    auto* addon = TimedBlockAddon::GetSingleton();
                    if (addon->HasParryFormsReady()) {
                        addon->UpdateParryWindowDuration();
                        RE::DebugNotification(fmt::format("Parry window: {:.0f}ms", settings->fParryWindowDurationMs).c_str());
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("How long the parry window stays open after pressing block.\nSmaller values demand tighter reflexes; larger values are forgiving.\n\n150ms = challenging (default)\n300ms = moderate\n500ms+ = very easy");
                }

                if (ImGui::InputText("Block Key##tb", State::blockKeyBuffer, sizeof(State::blockKeyBuffer))) {
                    settings->sBlockKey = State::blockKeyBuffer;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Alternate block key for dual-wield or other setups.\nUse an SKSE key name (e.g. V, LShift) or decimal unified key code.\n\nOnly needed if you can't use the normal block key.");
                }

                if (ImGui::Checkbox("Shield Only##tb", &settings->bOnlyWithShield)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, timed block only works if you have a shield equipped.\nDisable to allow weapon-blocking and unarmed timed blocks.");
                }

                ImGui::TreePop();
            }

            // --- On Successful Parry ---
            ImGui::Spacing();
            if (ImGui::TreeNodeEx("On Successful Parry##tb", ImGuiTreeNodeFlags_DefaultOpen)) {

                if (ImGui::TreeNodeEx("Damage Reduction##tb")) {
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                        "% of hit damage prevented per equipment type (100 = full negate)");
                    ImGui::Spacing();

                    if (ImGui::SliderFloat("Shield##dmgRed", &settings->fDmgReductionShield, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("Unarmed##dmgRed", &settings->fDmgReductionUnarmed, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("Sword##dmgRed", &settings->fDmgReductionSword, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("Dagger##dmgRed", &settings->fDmgReductionDagger, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("War Axe##dmgRed", &settings->fDmgReductionAxe, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("Mace##dmgRed", &settings->fDmgReductionMace, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("Greatsword##dmgRed", &settings->fDmgReductionGreatsword, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::SliderFloat("Battleaxe / Warhammer##dmgRed", &settings->fDmgReductionBattleaxe, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Covers Battleaxe and Warhammer (both use kTwoHandAxe internally).");
                    }

                    ImGui::TreePop();
                }

                ImGui::Spacing();

                // Hitstop
                if (ImGui::Checkbox("Hitstop##tb", &settings->bEnableHitstop)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Briefly freezes the ATTACKER's animation on a successful parry.\nAdds satisfying impact feel without freezing the whole game.");
                }
                if (settings->bEnableHitstop) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Animation Speed##hitstop", &settings->fHitstopSpeed, 0.0f, 0.3f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Attacker's animation speed during hitstop.\n0.0 = complete freeze\n0.05 = near-frozen (default)\n0.2 = subtle slowdown");
                    }
                    if (ImGui::SliderFloat("Duration (sec)##hitstop", &settings->fHitstopDuration, 0.05f, 0.5f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long the attacker stays frozen.\n0.1 = quick flash\n0.2 = noticeable (default)\n0.4+ = dramatic pause");
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();

                // Stagger
                if (ImGui::Checkbox("Stagger Attacker##tb", &settings->bEnableStagger)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Forces the attacker into a stagger animation on a successful parry.\nHigher magnitudes produce more dramatic staggers.\nYou can set different strengths for normal vs power attacks.");
                }
                if (settings->bEnableStagger) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Normal Attack Stagger##tb", &settings->fStaggerMagnitude, 0.0f, 1.5f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Stagger strength for normal attacks:\n0.25 = small stumble\n0.5 = medium stagger\n0.75 = large stagger (default)\n1.0+ = ragdoll-level");
                    }
                    if (ImGui::SliderFloat("Power Attack Stagger##tb", &settings->fPowerAttackStaggerMagnitude, 0.0f, 2.0f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Stagger strength for power attacks:\n0.5 = medium\n1.0 = large (default)\n1.5+ = massive\n\nReward parrying power attacks with a bigger opening!");
                    }

                    if (ImGui::SliderFloat("Nearby Stagger Distance##tb", &settings->fStaggerDistance, 0.0f, 512.0f, "%.0f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Actors within this radius (excluding the attacker) can also\nbe staggered on a successful parry.\n\n0 = only stagger the attacker\n256 = medium AoE\n512 = large AoE");
                    }

                    ImGui::Spacing();

                    // Skill-Based Stagger Chance sub-tree
                    if (ImGui::TreeNodeEx("Skill-Based Stagger Chance##tb")) {
                        if (ImGui::Checkbox("Use Skill-Based Chance##tb", &settings->bUseStaggerChance)) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("When enabled, stagger is not guaranteed — it scales\nwith your Block and/or weapon skill level.\n\nDisabled = 100%% guaranteed stagger on every parry.");
                        }

                        if (settings->bUseStaggerChance) {
                            ImGui::Indent();
                            if (ImGui::SliderFloat("Base Chance##tb", &settings->fBaseStaggerChance, 0.0f, 100.0f, "%.0f%%")) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Stagger chance at skill level 0.\nDefault: 50%%");
                            }
                            if (ImGui::SliderFloat("Max Chance##tb", &settings->fMaxStaggerChance, settings->fBaseStaggerChance, 100.0f, "%.0f%%")) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Stagger chance at skill level 100.\nDefault: 100%%");
                            }

                            ImGui::Spacing();
                            ImGui::Text("Skills to Factor:");
                            if (ImGui::Checkbox("Block Skill##stgr", &settings->bStaggerUseBlockSkill)) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Factor your Block skill into the stagger chance calculation.");
                            }
                            ImGui::SameLine();
                            if (ImGui::Checkbox("Weapon Skill##stgr", &settings->bStaggerUseWeaponSkill)) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Factor your weapon skill (One-Handed, Two-Handed, or Archery)\nbased on your currently equipped weapon.");
                            }

                            // Calculated chance display
                            ImGui::Spacing();
                            auto* player = RE::PlayerCharacter::GetSingleton();
                            if (player) {
                                auto* avOwner = player->AsActorValueOwner();
                                if (avOwner) {
                                    float blockSkill = avOwner->GetActorValue(RE::ActorValue::kBlock);
                                    float totalSkill = 0.0f;
                                    int skillCount = 0;
                                    if (settings->bStaggerUseBlockSkill) {
                                        totalSkill += std::clamp(blockSkill, 0.0f, 100.0f);
                                        skillCount++;
                                    }
                                    if (settings->bStaggerUseWeaponSkill) {
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
                        ImGui::TreePop();
                    }

                    ImGui::Unindent();
                }

                ImGui::Spacing();

                // Camera Shake
                if (ImGui::Checkbox("Camera Shake##tb", &settings->bEnableCameraShake)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Adds a brief camera shake on successful parry for impact feel.\nPurely cosmetic — does not affect gameplay.");
                }
                if (settings->bEnableCameraShake) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Shake Strength##tb", &settings->fCameraShakeStrength, 0.1f, 2.0f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Intensity of the shake.\n0.3 = subtle\n0.7 = moderate (default)\n1.5+ = heavy impact");
                    }
                    if (ImGui::SliderFloat("Shake Duration (sec)##tb", &settings->fCameraShakeDuration, 0.05f, 0.5f, "%.2f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long the shake lasts.\n0.1 = quick jolt (default)\n0.3+ = extended rumble");
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();

                // Stamina Restore
                if (ImGui::Checkbox("Stamina Restore##tb", &settings->bEnableStaminaRestore)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Restores a percentage of your max stamina on successful parry.\nRewards good timing with resource recovery.\n\nDefault: 100%% (full refill)");
                }
                if (settings->bEnableStaminaRestore) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Restore Amount##tb", &settings->fStaminaRestorePercent, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Percentage of max stamina restored.\n25%% = small reward\n50%% = half refill\n100%% = full refill (default)");
                    }
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

                ImGui::Spacing();

                // Slowmo
                if (ImGui::Checkbox("Slow Motion##tb", &settings->bEnableSlowmo)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Slows down the ENTIRE game world on successful parry.\nCreates a dramatic bullet-time effect for a brief moment.\n\nDoes NOT stack with counter attack slow time.");
                }
                if (settings->bEnableSlowmo) {
                    ImGui::Indent();
                    float speedPercent = settings->fSlowmoSpeed * 100.0f;
                    if (ImGui::SliderFloat("World Speed##slowmo", &speedPercent, 5.0f, 50.0f, "%.0f%%")) {
                        settings->fSlowmoSpeed = speedPercent / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How slow the world moves.\n10%% = very slow\n25%% = quarter speed (default)\n50%% = half speed");
                    }
                    if (ImGui::SliderFloat("Duration (sec)##slowmo", &settings->fSlowmoDuration, 0.1f, 2.0f, "%.2f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Real-time duration of the slowmo effect.\n0.3 = brief flash\n0.5 = default\n1.0+ = extended dramatic pause");
                    }
                    ImGui::Unindent();
                }

                ImGui::TreePop();
            }

            // --- Counter Attack ---
            ImGui::Spacing();
            if (ImGui::TreeNodeEx("Counter Attack##tb")) {

                if (ImGui::Checkbox("Prevent Player Stagger##tb", &settings->bPreventPlayerStagger)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Prevents YOU from staggering on a successful parry.\nWithout this, heavy/power attacks can still stagger you\neven if you timed the block perfectly.\n\nDefault: Enabled");
                }

                ImGui::Spacing();

                if (ImGui::Checkbox("Enable Counter Attack##tb", &settings->bEnableCounterAttack)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("After a successful parry, pressing attack cancels your block\nreaction and immediately starts an attack animation.\n\nAllows Elden Ring / Sekiro style counter-attacks.");
                }
                if (settings->bEnableCounterAttack) {
                    ImGui::Indent();
                    float windowMs = settings->fCounterAttackWindow * 1000.0f;
                    if (ImGui::SliderFloat("Counter Window (ms)##tb", &windowMs, 100.0f, 1000.0f, "%.0f ms")) {
                        settings->fCounterAttackWindow = windowMs / 1000.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long after a parry you can press attack to counter.\n300ms = tight window\n500ms = default\n800ms = very generous");
                    }
                    if (ImGui::Checkbox("Alt Power Attack Input##tb", &settings->bAltPowerAttackFallback)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Enable if you use an alternate power attack mod that\nbinds power attack to a non-standard mouse button\n(middle mouse, mouse 4/5, etc.).\n\nTreats unbound extra mouse buttons as attack input\nduring the counter window.\n\nDefault: Off");
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();

                // Damage Bonus
                if (ImGui::Checkbox("Counter Damage Bonus##tb", &settings->bEnableCounterDamageBonus)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Your first hit after a counter attack deals bonus damage.\nThe bonus is consumed on the first successful strike.\n\nRewards aggressive play after a parry!");
                }
                if (settings->bEnableCounterDamageBonus) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Damage Bonus##tb", &settings->fCounterDamageBonusPercent, 10.0f, 200.0f, "+%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Extra damage on the counter hit.\n+50%% = 1.5x damage (default)\n+100%% = double damage\n+200%% = triple damage");
                    }
                    if (ImGui::SliderFloat("Bonus Timeout##tb", &settings->fCounterDamageBonusTimeout, 0.5f, 3.0f, "%.1f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long the damage bonus lasts if you don't land a hit.\nExpires after this time to prevent hoarding the bonus.\n\nDefault: 1.0 sec");
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();

                // Lunge
                if (ImGui::Checkbox("Counter Lunge##tb", &settings->bEnableCounterLunge)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Lunge toward the attacker when you counter attack.\nCloses the gap for a more aggressive counter feel.\n\nRequires 'Enable Counter Attack' above.");
                }
                if (settings->bEnableCounterLunge) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Lunge Distance##tb", &settings->fCounterLungeDistance, 50.0f, 500.0f, "%.0f units")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Maximum travel distance toward the target.\n100-150 = short (daggers)\n200-300 = medium (swords, default)\n400-500 = long (closing large gaps)");
                    }
                    if (ImGui::SliderFloat("Lunge Speed##tb", &settings->fCounterLungeSpeed, 0.01f, 1.0f, "%.3f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Speed of the lunge movement.\nHigher = snappier dash, lower = smoother glide.\n\nDefault: 0.050");
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
                        ImGui::SetTooltip("How far from the target the lunge stops.\nLower = end right in their face (daggers)\nHigher = keep spacing (greatswords)\n\nDefault: 128 units");
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();

                // Counter Slow Time
                if (ImGui::Checkbox("Counter Slow Time##tb", &settings->bEnableCounterSlowTime)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Slows down time when you begin a counter attack.\nEnds when a specific animation event fires (e.g. weapon swing).\n\nCreates a Sekiro-style dramatic counter moment.");
                }
                if (settings->bEnableCounterSlowTime) {
                    ImGui::Indent();
                    float scalePercent = settings->fCounterSlowTimeScale * 100.0f;
                    if (ImGui::SliderFloat("Time Scale##counterSlow", &scalePercent, 5.0f, 75.0f, "%.0f%%")) {
                        settings->fCounterSlowTimeScale = scalePercent / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How slow time moves during the counter.\n10%% = very dramatic\n25%% = default\n50%% = subtle slowdown");
                    }
                    if (ImGui::SliderFloat("Max Duration (sec)##counterSlow", &settings->fCounterSlowTimeMaxDuration, 0.5f, 5.0f, "%.1f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Safety cap — slow time ends after this long even if\nthe end animation event is never detected.\n\nDefault: 2.0 sec");
                    }

                    ImGui::Spacing();
                    if (ImGui::Checkbox("Start After Lunge##counterSlow", &settings->bCounterSlowStartAfterLunge)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("When ON: slow time starts AFTER the lunge finishes.\nWhen OFF: slow time starts on the Start Event below.\n\nBest with lunge enabled — the slowdown kicks in\nright as your weapon connects.");
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
                        if (ImGui::InputText("Start Event##counterSlow", startEventBuffer, sizeof(startEventBuffer))) {
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
                    if (ImGui::InputText("End Event##counterSlow", endEventBuffer, sizeof(endEventBuffer))) {
                        settings->sCounterSlowEndEvent = endEventBuffer;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Animation event that ENDS slow time.\nCommon events:\n  weaponSwing - weapon swing moment\n  HitFrame - actual hit frame\n  AttackWinEnd - attack window closes\n\nUses partial matching (case-insensitive).");
                    }

                    ImGui::Unindent();
                }

                ImGui::TreePop();
            }

            // --- Cooldown ---
            ImGui::Spacing();
            if (ImGui::TreeNodeEx("Cooldown##tb", ImGuiTreeNodeFlags_DefaultOpen)) {

                if (ImGui::Checkbox("Enable Cooldown##tb", &settings->bEnableCooldown)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Prevents spamming block to fish for parries.\nIf you block but MISS the parry window, you must wait\nbefore trying again.\n\nSuccessful parries CLEAR the cooldown instantly.");
                }
                if (settings->bEnableCooldown) {
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Cooldown Duration (ms)##tb", &settings->fCooldownDurationMs, 0.0f, 1000.0f, "%.0f ms")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Wait time after a failed parry attempt.\n150ms = short (barely noticeable)\n250ms = balanced (default)\n500ms+ = punishing");
                    }

                    ImGui::Spacing();
                    if (ImGui::Checkbox("Ignore Outside Combat Range##tb", &settings->bIgnoreCooldownOutsideRange)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Cooldown is skipped if no hostile NPCs are fighting you nearby.\nLets you freely attempt parries against distant archers\nwhile still punishing spam in melee range.");
                    }
                    if (settings->bIgnoreCooldownOutsideRange) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("Detection Range##tbCooldown", &settings->fCooldownIgnoreDistance, 128.0f, 2048.0f, "%.0f units")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("If no hostile NPC in combat is within this distance,\ncooldown is ignored.\n\n512 = melee only\n1024 = medium range\n2048 = long range");
                        }
                        ImGui::Unindent();
                    }
                    ImGui::Unindent();
                }

                ImGui::TreePop();
            }

            // --- Sound ---
            ImGui::Spacing();
            if (ImGui::TreeNodeEx("Sound##tb")) {

                if (ImGui::Checkbox("Timed Block Sound##tb", &settings->bEnableSound)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Play a sound effect on successful timed block.\nCan use a game sound descriptor or a custom WAV file.");
                }
                if (settings->bEnableSound) {
                    ImGui::Indent();
                    if (ImGui::Checkbox("Use Custom WAV##tb", &settings->bUseCustomWav)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Toggle between a game sound descriptor (EditorID)\nand a custom WAV file.\n\nWAV location: Data/SKSE/Plugins/TimedBlockDodgeCounter/timedblock.wav");
                    }
                    if (!settings->bUseCustomWav) {
                        if (ImGui::InputText("Sound Descriptor##tb", State::soundPathBuffer, sizeof(State::soundPathBuffer))) {
                            settings->sSoundPath = State::soundPathBuffer;
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("EditorID of a sound descriptor form.\nExamples: UIMenuOK, NPCHumanCombatShieldBlock, MAGImpactStagger");
                        }
                    } else {
                        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), 
                            "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/timedblock.wav");
                        float volumePercent = settings->fCustomWavVolume * 100.0f;
                        if (ImGui::SliderFloat("WAV Volume##tb", &volumePercent, 0.0f, 100.0f, "%.0f%%")) {
                            settings->fCustomWavVolume = volumePercent / 100.0f;
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Playback volume for the custom WAV.");
                        }
                    }
                    ImGui::Unindent();
                }

                ImGui::Spacing();

                if (ImGui::Checkbox("Counter Strike Sound##tb", &settings->bEnableCounterStrikeSound)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Play a sound when your counter attack connects with an enemy.\n\nWAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/counterstrike.wav");
                }
                if (settings->bEnableCounterStrikeSound) {
                    ImGui::Indent();
                    float volumePercent = settings->fCounterStrikeSoundVolume * 100.0f;
                    if (ImGui::SliderFloat("Counter Strike Volume##tb", &volumePercent, 0.0f, 100.0f, "%.0f%%")) {
                        settings->fCounterStrikeSoundVolume = volumePercent / 100.0f;
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Volume of the counter strike impact sound.");
                    }
                    ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f), 
                        "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/counterstrike.wav");
                    ImGui::Unindent();
                }

                ImGui::TreePop();
            }

            // --- Requirements & Forms ---
            ImGui::Spacing();
            if (ImGui::TreeNodeEx("Requirements & Forms##tb")) {

                ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.5f, 1.0f), "Advanced: perk requirements and form lookups");
                ImGui::Spacing();

                if (ImGui::Checkbox("Perk-Locked Damage Prevention##tb", &settings->bPerkLockedBlock)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, the full damage negation from a parry\nonly works if you have the configured perk.\nWithout the perk, you still get stagger/effects but take damage.\n\nUseful for progression-gated timed blocking.");
                }

                if (ImGui::Checkbox("Perk-Locked Stagger##tb", &settings->bPerkLockedStagger)) {
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("When enabled, attacker stagger only triggers if you\nhave the configured stagger perk.\nOther effects (hitstop, sound, etc.) still fire.");
                }

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Perk Forms");

                if (ImGui::InputText("Perk Plugin Name##tb", State::formsPerkPluginBuffer, sizeof(State::formsPerkPluginBuffer))) {
                    settings->sFormsPerkPluginName = State::formsPerkPluginBuffer;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("ESP/ESM/ESL filename containing the perk forms.\nExample: Skyrim.esm, MyMod.esp");
                }
                if (ImGui::InputText("Damage Perk FormID (hex)##tb", State::formsDamagePerkBuffer, sizeof(State::formsDamagePerkBuffer))) {
                    settings->sFormsDamagePreventPerkID = State::formsDamagePerkBuffer;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Hexadecimal FormID of the perk required for damage prevention.\nExample: 000C44BB");
                }
                if (ImGui::InputText("Stagger Perk FormID (hex)##tb", State::formsStaggerPerkBuffer, sizeof(State::formsStaggerPerkBuffer))) {
                    settings->sFormsStaggerPerkID = State::formsStaggerPerkBuffer;
                    State::MarkChanged();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Hexadecimal FormID of the perk required for stagger.\nExample: 000C44BC");
                }
                if (ImGui::Button("Reload Perk Forms##tb")) {
                    TimedBlockAddon::GetSingleton()->LoadPerkForms();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Hot-reload perk forms from the plugin without restarting.");
                }

                ImGui::TreePop();
            }
        ImGui::EndTabItem();
        }

        // ===== TIMED DODGE SECTION =====
        if (ImGui::BeginTabItem("Timed Dodge")) {
            if (ImGui::Checkbox("Enable Timed Dodge", &settings->bEnableTimedDodge)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Dodge at the precise moment an enemy attacks to trigger\nslow-motion, i-frames, and radial blur.\n\nWorks with DMCO, TK Dodge RE, and Ultimate Dodge.");
            }
            
            if (settings->bEnableTimedDodge) {
                ImGui::Indent();
                
                // --- Timing & Detection ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Timing & Detection", ImGuiTreeNodeFlags_DefaultOpen)) {

                    if (ImGui::SliderFloat("Detection Range##dodge", &settings->fTimedDodgeDetectionRange, 100.0f, 600.0f, "%.0f units")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Maximum distance (game units) an attacking enemy can be\n"
                            "for your dodge to count as 'timed'.\n\n"
                            "With Precision strict mode enabled, this acts as a fast\n"
                            "pre-filter — actual weapon reach is checked separately.\n\n"
                            "200 = very close (dagger range)\n"
                            "300 = default (sword range)\n"
                            "500 = generous (greatsword range)");
                    }

                    if (ImGui::SliderFloat("Forgiveness (ms)##dodge", &settings->fTimedDodgeForgivenessMs, 0.0f, 500.0f, "%.0f ms")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Grace period for EARLY dodges only.\n"
                            "If you dodge before the enemy commits to a swing,\n"
                            "the game re-checks for an attack each frame over this window.\n\n"
                            "0 = no forgiveness (frame-perfect timing required)\n"
                            "200 = default (generous)\n"
                            "500 = very forgiving\n\n"
                            "Does NOT help late dodges — if the swing already hit, you're hit.");
                    }

                    if (ImGui::SliderFloat("Hit Window (ms)##dodge", &settings->fTimedDodgeHitWindowMs, 0.0f, 2000.0f, "%.0f ms")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Window after dodge start where GETTING HIT triggers timed dodge.\n"
                            "Complements Forgiveness (which handles early dodges).\n\n"
                            "If you dodge and an enemy hit lands within this window,\n"
                            "the timed dodge still activates.\n\n"
                            "600 = default\n"
                            "0 = disabled (only Forgiveness / detection range matter)");
                    }

                    // Precision Integration
                    ImGui::Spacing();
                    if (WardTimedBlockState::g_precisionAvailable) {
                        if (ImGui::TreeNodeEx("Precision Integration", ImGuiTreeNodeFlags_DefaultOpen)) {

                            if (ImGui::Checkbox("Require Active Hitbox##precDodge", &settings->bPrecisionStrictDodge)) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip(
                                    "When enabled, timed dodge only triggers if the enemy has a live\n"
                                    "Precision weapon hitbox (the dangerous part of their swing).\n\n"
                                    "When disabled, any attack animation state within range qualifies\n"
                                    "(more lenient, like vanilla detection).\n\n"
                                    "Requires: Precision by Ersh installed and active.");
                            }

                            if (settings->bPrecisionStrictDodge) {
                                ImGui::Indent();

                                if (ImGui::Checkbox("Fallback to Animation State##precDodge", &settings->bPrecisionFallbackToAttackState)) {
                                    State::MarkChanged();
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip(
                                        "If the enemy is in an attack animation but has no active hitbox\n"
                                        "(e.g. bash, some unarmed attacks), still allow the timed dodge\n"
                                        "using the legacy animation-state check.\n\n"
                                        "Recommended: On (prevents edge cases where attacks slip through).");
                                }

                                if (ImGui::SliderFloat("Reach Tolerance##precDodge", &settings->fPrecisionReachTolerance, 0.0f, 256.0f, "%.0f units")) {
                                    State::MarkChanged();
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip(
                                        "Extra distance (game units) added to the enemy's weapon reach\n"
                                        "when checking if their swing could hit you.\n\n"
                                        "Higher = more forgiving (triggers further from weapon tip).\n"
                                        "Lower = more precise (must be very close to the swing arc).\n\n"
                                        "64 = default (about half a meter of forgiveness)\n"
                                        "0 = no tolerance (only triggers if weapon literally reaches you)\n"
                                        "128 = generous");
                                }

                                float lookaheadMs = settings->fPrecisionLookaheadSec * 1000.0f;
                                if (ImGui::SliderFloat("Movement Lookahead##precDodge", &lookaheadMs, 0.0f, 1000.0f, "%.0f ms")) {
                                    settings->fPrecisionLookaheadSec = lookaheadMs / 1000.0f;
                                    State::MarkChanged();
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip(
                                        "How far ahead (in milliseconds) to project the enemy's movement\n"
                                        "when estimating if their swing can reach you.\n\n"
                                        "Accounts for enemies lunging or charging during power attacks.\n"
                                        "An enemy running toward you at 300 units/sec with 200ms lookahead\n"
                                        "extends their effective reach by 60 units.\n\n"
                                        "200ms = default\n"
                                        "0 = ignore movement (only static weapon length matters)\n"
                                        "500ms = generous (catches fast-moving lunges)");
                                }

                                ImGui::Unindent();
                            }

                            ImGui::TreePop();
                        }
                    } else {
                        ImGui::TextDisabled("Precision Integration: not available (Precision.dll not loaded)");
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "Install Precision by Ersh for stricter hitbox-based dodge detection.\n"
                                "Without it, timed dodge uses animation-state detection (still works, just less precise).");
                        }
                    }

                    ImGui::TreePop();
                }

                // --- Slow Motion ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Slow Motion", ImGuiTreeNodeFlags_DefaultOpen)) {

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

                    ImGui::TreePop();
                }

                // --- Cooldowns ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Cooldowns", ImGuiTreeNodeFlags_DefaultOpen)) {

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
                        ImGui::SetTooltip(
                            "After taking damage from a weapon hit (melee or ranged),\n"
                            "timed dodge is locked out for this long.\n"
                            "Prevents dodge-spamming after getting hit.\n\n"
                            "Only real weapon hits count — poison ticks and DoT are ignored.\n"
                            "Set to 0 to disable.");
                    }

                    if (ImGui::SliderFloat("Hit Contact Cooldown (sec)##dodge", &settings->fTimedDodgeHitContactCooldown, 0.0f, 5.0f, "%.2f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Brief lockout after a melee weapon physically touches you.\n"
                            "Triggers on contact, not on HP loss — blocked hits and\n"
                            "i-framed hits still count.\n\n"
                            "With Precision: uses pre-hit contact detection.\n"
                            "Without Precision: uses TESHitEvent (melee only).\n\n"
                            "Set to 0 to disable.");
                    }

                    ImGui::TreePop();
                }

                // --- I-Frames ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("I-Frames##dodge")) {

                    if (ImGui::Checkbox("Enable I-Frames##dodge", &settings->bTimedDodgeIframes)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Player cannot be damaged during the slow-motion effect.\nDuration matches the slow-motion duration.");
                    }

                    if (ImGui::SliderFloat("Attacker Immunity (sec)##dodge", &settings->fTimedDodgeAttackerImmunity, 0.0f, 5.0f, "%.1f sec")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "After a timed dodge triggers, the player is immune to damage\n"
                            "from the specific attacker that triggered it for this duration.\n\n"
                            "Prevents the very attack that caused the timed dodge from\n"
                            "dealing damage (e.g. multi-hit attacks, lingering hitboxes).\n\n"
                            "1.0 = default (covers most single attacks)\n"
                            "2.0 = generous (covers combo follow-ups)\n"
                            "0 = disabled");
                    }

                    ImGui::TreePop();
                }

                // --- Counter Attack ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Counter Attack##dodge")) {

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

                            if (ImGui::SliderFloat("Lunge Distance##dodgeCounter", &settings->fTimedDodgeCounterLungeDistance, 50.0f, 500.0f, "%.0f units")) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Maximum travel distance for the lunge (game units).\nLonger = more aggressive gap-close.\n\nDefault: 150 units");
                            }

                            if (ImGui::SliderFloat("Lunge Speed##dodgeCounter", &settings->fTimedDodgeCounterLungeSpeed, 0.01f, 1.0f, "%.3f")) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("Speed of the timed dodge counter lunge.\nHigher = faster/snappier, lower = smoother glide.\n\nDefault: 0.050");
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

                    ImGui::TreePop();
                }

                // --- Effects on Attacker ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Effects on Attacker##dodge")) {

                    if (ImGui::Checkbox("Apply Block Effects##dodge", &settings->bTimedDodgeApplyBlockEffects)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Apply the timed block visual effects to the attacker:\nhitstop, camera shake, stamina restore, etc.\n\nUses the settings from the timed block sections above.\nStagger and sound are controlled separately below.");
                    }

                    if (ImGui::Checkbox("Stagger on Timed Dodge##dodge", &settings->bTimedDodgeStagger)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Force the attacker into a stagger animation on timed dodge.\nDisabled by default (the attacker slow is usually enough).");
                    }

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

                    ImGui::TreePop();
                }

                // --- Visuals & Sound ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Visuals & Sound##dodge")) {

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

                    ImGui::Spacing();

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

                    ImGui::TreePop();
                }
                
                ImGui::Unindent();
            }
        ImGui::EndTabItem();
        }

        // ===== WARD TIMED BLOCK =====
        if (ImGui::BeginTabItem("Ward")) {
            if (ImGui::Checkbox("Enable Ward Timed Block##ward", &settings->bEnableWardTimedBlock)) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Melee-only: while a ward spell is active, a short window opens.\n"
                    "If a melee attack hits during that window, damage is cancelled\n"
                    "and parry effects (stagger, hitstop, etc.) fire.\n\n"
                    "Works without forcing block animations on your character.");
            }

            if (settings->bEnableWardTimedBlock) {
                ImGui::Indent();

                // --- Timing & Detection ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Timing & Detection##ward", ImGuiTreeNodeFlags_DefaultOpen)) {

                    if (ImGui::SliderFloat("Timing Window (ms)##ward", &settings->fWardTimedBlockWindowMs, 50.0f, 2000.0f, "%.0f ms")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How long after raising the ward a melee hit counts as parried.\nMore forgiving than shield parry by default.\n\n200ms = tight\n500ms = default\n1000ms+ = very easy");
                    }

                    if (ImGui::SliderFloat("Detection Range (units)##ward", &settings->fWardMeleeDetectionRange, 50.0f, 1000.0f, "%.0f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Maximum distance between you and the attacker for the\nward parry to trigger.\n\n130 = 1H weapon reach\n200 = 2H weapon reach\n300 = generous slack (default)");
                    }

                    if (ImGui::Checkbox("Omnidirectional##ward", &settings->bWardOmnidirectional)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("By default, the ward only parries attacks from\nyour front 180\xc2\xb0 (matching vanilla ward coverage).\n\nEnable this to parry attacks from all directions.");
                    }

                    if (ImGui::Checkbox("Require 2H Ward for 2H Weapons##ward", &settings->bWardRequire2HForTwoHanders)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("When enabled, only a dual-cast (both hands) ward can\nparry two-handed weapon attacks.\nA single-hand ward only blocks one-handed strikes.");
                    }

                    if (ImGui::SliderFloat("Cooldown (sec)##ward", &settings->fWardTimedBlockCooldown, 0.0f, 10.0f, "%.1f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Minimum time before a new ward parry window opens\nafter a successful one.\n\nPrevents instant re-parrying in group fights.\n0 = no cooldown");
                    }

                    if (ImGui::SliderFloat("Attacker Immunity (sec)##ward", &settings->fWardAttackerImmunity, 0.0f, 5.0f, "%.1f")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("After a successful ward parry, ignore all damage\nfrom the triggering attacker for this duration.\n\nPrevents follow-up hits from the same attacker\nfrom dealing damage during the counter window.\n\n0 = disabled");
                    }

                    ImGui::TreePop();
                }

                // --- On Successful Parry ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("On Successful Parry##ward", ImGuiTreeNodeFlags_DefaultOpen)) {

                    if (ImGui::SliderFloat("Damage Reduction##ward", &settings->fWardDmgReduction, 0.0f, 100.0f, "%.0f%%")) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "How much of the melee hit's damage is prevented on a successful ward parry.\n\n"
                            "100%% = full cancel (default, take no damage)\n"
                            "50%% = take half damage\n"
                            "0%% = no protection (effects only)");
                    }

                    ImGui::Spacing();

                    if (ImGui::Checkbox("Stagger Attacker##ward", &settings->bWardTimedBlockStagger)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Staggers the melee attacker on a successful ward parry.\nDifferent magnitudes for 1H vs dual-cast wards.");
                    }
                    if (settings->bWardTimedBlockStagger) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("1H Ward Stagger##ward", &settings->fWardSmallStaggerMagnitude, 0.0f, 2.0f, "%.2f")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Stagger magnitude with a single-hand ward.\n0.25 = small stumble\n0.5 = medium\n0.75 = default");
                        }
                        if (ImGui::SliderFloat("2H Dual-Cast Stagger##ward", &settings->fWardLargeStaggerMagnitude, 0.0f, 2.0f, "%.2f")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Stagger magnitude with dual-cast ward.\nRewards committing both hands to defense.\n\n0.5 = medium\n1.0 = large (default)\n1.5+ = massive");
                        }
                        ImGui::Unindent();
                    }

                    ImGui::Spacing();

                    if (ImGui::Checkbox("Restore Magicka##ward", &settings->bWardTimedBlockMagickaRestore)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Restore a portion of max magicka on successful ward parry.\nHelps offset the magicka drain of maintaining wards.");
                    }
                    if (settings->bWardTimedBlockMagickaRestore) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("Magicka Restore %%##ward", &settings->fWardMagickaRestorePercent, 0.0f, 100.0f, "%.0f%%")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Percentage of maximum magicka restored.\n25%% = small\n50%% = half refill\n100%% = full refill");
                        }
                        ImGui::Unindent();
                    }

                    ImGui::TreePop();
                }

                // --- Counter Attack ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Counter Attack##ward")) {

                    if (ImGui::Checkbox("Enable Ward Counter##ward", &settings->bWardTimedBlockCounterAttack)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Opens a counter-attack window after a successful ward parry.\nLand a melee hit or spell within the window for bonus damage.");
                    }
                    if (settings->bWardTimedBlockCounterAttack) {
                        ImGui::Indent();
                        if (ImGui::SliderFloat("Counter Window (ms)##wardCounter", &settings->fWardCounterWindowMs, 50.0f, 10000.0f, "%.0f ms")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Time after the ward parry to land a counter attack.\n500ms = tight\n2000ms = generous\n5000ms = very forgiving");
                        }
                        if (ImGui::SliderFloat("Counter Damage Bonus %%##ward", &settings->fWardCounterDamagePercent, 0.0f, 500.0f, "%.0f%%")) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Extra damage on the counter hit (melee or spell).\n0%% = no bonus\n50%% = +50%% of the hit's base damage\n100%% = double damage");
                        }

                        ImGui::Spacing();
                        if (ImGui::Checkbox("Spell Counter##ward", &settings->bWardCounterSpellHit)) {
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("First qualifying spell hit (concentration or projectile)\ncan consume the counter damage bonus.\nPhysical arrows/bolts are ignored.");
                        }
                        if (settings->bWardCounterSpellHit) {
                            ImGui::Indent();
                            if (ImGui::SliderFloat("Spell In-Flight Timeout (ms)##ward", &settings->fWardCounterSpellInFlightMs, 100.0f, 30000.0f, "%.0f ms")) {
                                State::MarkChanged();
                            }
                            if (ImGui::IsItemHovered()) {
                                ImGui::SetTooltip("After casting a counter spell, how long the bonus\nwaits for the projectile to hit.\nIf nothing lands in time, the bonus expires.\n\nDefault: 5000ms (5 sec)");
                            }
                            if (ImGui::Checkbox("Spell Counter Sound##ward", &settings->bWardCounterSpellSound)) {
                                State::MarkChanged();
                            }
                            if (settings->bWardCounterSpellSound) {
                                ImGui::Indent();
                                float sv = settings->fWardCounterSpellSoundVolume * 100.0f;
                                if (ImGui::SliderFloat("Volume##wardSpellCounter", &sv, 0.0f, 100.0f, "%.0f%%")) {
                                    settings->fWardCounterSpellSoundVolume = sv / 100.0f;
                                    State::MarkChanged();
                                }
                                if (ImGui::InputText("WAV File##wardSpellCounter", State::wardCounterSpellFileBuffer, sizeof(State::wardCounterSpellFileBuffer))) {
                                    settings->sWardCounterSpellSoundFile = State::wardCounterSpellFileBuffer;
                                    State::MarkChanged();
                                }
                                ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                                    "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/%s", settings->sWardCounterSpellSoundFile.c_str());
                                ImGui::Unindent();
                            }
                            ImGui::Unindent();
                        }
                        ImGui::Unindent();
                    }

                    ImGui::TreePop();
                }

                // --- Sound ---
                ImGui::Spacing();
                if (ImGui::TreeNodeEx("Sound##ward")) {

                    if (ImGui::Checkbox("Ward Parry Sound##ward", &settings->bWardTimedBlockSound)) {
                        State::MarkChanged();
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Play a sound effect on successful ward parry.\n\nWAV location: Data/SKSE/Plugins/TimedBlockDodgeCounter/<filename>");
                    }
                    if (settings->bWardTimedBlockSound) {
                        ImGui::Indent();
                        float wv = settings->fWardTimedBlockSoundVolume * 100.0f;
                        if (ImGui::SliderFloat("Volume##wardSound", &wv, 0.0f, 100.0f, "%.0f%%")) {
                            settings->fWardTimedBlockSoundVolume = wv / 100.0f;
                            State::MarkChanged();
                        }
                        if (ImGui::InputText("WAV Filename##wardSound", State::wardSoundFileBuffer, sizeof(State::wardSoundFileBuffer))) {
                            settings->sWardTimedBlockSoundFile = State::wardSoundFileBuffer;
                            State::MarkChanged();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("Filename of the WAV in Data/SKSE/Plugins/TimedBlockDodgeCounter/");
                        }
                        ImGui::TextColored(ImVec4(0.7f, 0.9f, 0.7f, 1.0f),
                            "WAV: Data/SKSE/Plugins/TimedBlockDodgeCounter/%s", settings->sWardTimedBlockSoundFile.c_str());
                        ImGui::Unindent();
                    }

                    ImGui::TreePop();
                }

                ImGui::Unindent();
            }
        ImGui::EndTabItem();
        }

        // ===== GENERAL =====
        if (ImGui::BeginTabItem("General")) {
            if (ImGui::SliderFloat("Window Exclusion (ms)##general", &settings->fWindowExclusionMs, 0.0f, 5000.0f, "%.0f ms")) {
                State::MarkChanged();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Minimum time between timed block, ward parry, and timed dodge windows.\nPrevents chaining one mechanic into another instantly\n(e.g. ward parry then immediately dodge).\n\n0 = no restriction (all mechanics can overlap)");
            }
        ImGui::EndTabItem();
        }

        // ===== DEBUG =====
        if (ImGui::BeginTabItem("Debug")) {
            if (ImGui::Checkbox("Debug Logging##dbg", &settings->bDebugLogging)) {
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
                ImGui::SetTooltip("Write detailed debug info to the log file.\nLog: Documents/My Games/Skyrim Special Edition/SKSE/TimedBlockDodgeCounter.log\n\nOn-screen notifications are controlled separately below.");
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
        ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
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
