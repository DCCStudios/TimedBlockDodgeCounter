# Timed Block Dodge and Counter

SKSE plugin for Skyrim SE/AE that adds timed blocking, timed dodging, ward parrying, and counter-attack mechanics. Standalone — no ESP required.

## Features

- **Timed Block** — Block within a configurable parry window to negate damage, stagger the attacker, trigger hitstop, camera shake, stamina restore, slowmo, and explosion VFX.
- **Timed Dodge** — Dodge an incoming attack to trigger slow-motion, radial blur, i-frames, and a counter-attack window. Supports TK Dodge RE, DMCO, and Ultimate Dodge.
- **Counter Attack** — After a successful timed block or dodge, attack to lunge at the attacker with a damage bonus. Supports melee, ranged (bow/crossbow with draw-speed boost), and spell counters.
- **Ward Timed Block** — Block melee attacks with an active ward for stagger, magicka restore, and a spell-counter window. Supports Precision API for accurate hit detection.
- **In-Game Configuration** — Full SKSEMenuFramework (ImGui) menu for real-time tweaking. All settings are also exposed via INI and Papyrus.

## Requirements

- [Skyrim SE/AE](https://store.steampowered.com/app/489830/The_Elder_Scrolls_V_Skyrim_Special_Edition/) (1.5.97+ or 1.6.x)
- [SKSE64](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- [SKSEMenuFramework](https://www.nexusmods.com/skyrimspecialedition/mods/96925) (for in-game menu)
- A dodge mod (TK Dodge RE, DMCO, or Ultimate Dodge) for timed dodge features

## Building

Requires Visual Studio 2022 with C++23 support and vcpkg.

```bash
cmake --preset release
cmake --build build/release --config Release
```

Output goes to `Compile/SKSE/Plugins/` (DLL, PDB, INI).

### Dependencies (via vcpkg)

- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)
- [SimpleIni](https://github.com/brofield/simpleini)
- [Xbyak](https://github.com/herumi/xbyak)

## Installation

Copy the contents of `Compile/` into your Skyrim `Data/` folder:

```
Data/
  SKSE/
    Plugins/
      TimedBlockDodgeCounter.dll
      TimedBlockDodgeCounter.ini
```

Optional sound files go in `Data/SKSE/Plugins/TimedBlockDodgeCounter/` (WAV format).

## License

See source files for details.
