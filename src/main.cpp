#include "Settings.h"
#include "TimedBlockAddon.h"
#include "Papyrus.h"
#include "Menu.h"

void InitializeLog() {
    auto path = logger::log_directory();
    if (!path) {
        SKSE::stl::report_and_fail("Failed to find standard logging directory"sv);
    }

    *path /= "TimedBlockDodgeCounter.log"sv;
    
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);

    auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));

    log->set_level(spdlog::level::debug);
    log->flush_on(spdlog::level::debug);

    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v"s);

    spdlog::flush_every(std::chrono::seconds(3));
    
    logger::info("=== Timed Block Dodge and Counter Log ===");
    logger::info("Log file path: {}", path->string());
    spdlog::default_logger()->flush();
}

void MessageHandler(SKSE::MessagingInterface::Message* message) noexcept {
    switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded: {
            logger::info("Data loaded, initializing...");
            
            Settings::GetSingleton()->LoadSettings();

            TimedBlockAddon::GetSingleton()->LoadPerkForms();

            TimedBlockAddon::GetSingleton()->Initialize();
            
            TimedBlockAddon::Register();

            WardEffectHandler::Register();

            PrecisionCache::Init();
            WardTimedBlockState::RegisterPrecision();
            
            CounterDamageHitHandler::Register();
            
            TimedDodgeState::InitializeBlurIMOD();
            TimedDodgeState::InitCustomDodge();
            
            Menu::Register();
            
            logger::info("Timed Block Dodge and Counter: core systems initialized.");
            break;
        }
        case SKSE::MessagingInterface::kInputLoaded: {
            CounterAttackInputHandler::Register();
            BlockKeyInputHandler::Register();
            break;
        }
        case SKSE::MessagingInterface::kPostLoadGame: {
            logger::info("Post load game - reloading settings");
            Settings::GetSingleton()->LoadSettings();

            if (!TimedBlockAddon::GetSingleton()->CreateParryWindowForms()) {
                logger::error("Failed to create parry window MGEF/spell");
            } else {
                TimedBlockAddon::GetSingleton()->UpdateParryWindowDuration();
            }

            if (!CounterAttackState::CreateCounterDamageForms()) {
                logger::error("Failed to create counter-attack damage MGEF/spell");
            }
            if (!CounterAttackState::CreateDrawSpeedForms()) {
                logger::error("Failed to create draw-speed boost MGEF/spell");
            }
            
            CounterAnimEventHandler::Register();
            break;
        }
        case SKSE::MessagingInterface::kNewGame: {
            logger::info("New game - reloading settings");
            Settings::GetSingleton()->LoadSettings();

            if (!TimedBlockAddon::GetSingleton()->CreateParryWindowForms()) {
                logger::error("Failed to create parry window MGEF/spell");
            } else {
                TimedBlockAddon::GetSingleton()->UpdateParryWindowDuration();
            }

            if (!CounterAttackState::CreateCounterDamageForms()) {
                logger::error("Failed to create counter-attack damage MGEF/spell");
            }
            if (!CounterAttackState::CreateDrawSpeedForms()) {
                logger::error("Failed to create draw-speed boost MGEF/spell");
            }
            
            CounterAnimEventHandler::Register();
            break;
        }
        default:
            break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
    InitializeLog();
    
    SKSE::Init(skse);
    
    logger::info("Timed Block Dodge and Counter (SKSE)");
    logger::info("=====================================");
    
    SKSE::AllocTrampoline(128);
    
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener(MessageHandler)) {
        logger::error("Failed to register messaging listener!");
        return false;
    }
    
    auto papyrus = SKSE::GetPapyrusInterface();
    if (!papyrus->Register(Papyrus::RegisterFunctions)) {
        logger::error("Failed to register Papyrus functions!");
        return false;
    }
    
    logger::info("Plugin loaded successfully.");
    
    return true;
}
