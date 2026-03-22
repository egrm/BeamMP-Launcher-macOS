# BeamMP Launcher for macOS

Play BeamNG.drive multiplayer on macOS using CrossOver.

> This is **not** the official BeamMP launcher. Windows and Linux users should use the [official release](https://beammp.com/).

## Setup

### Prerequisites

- macOS with [CrossOver](https://www.codeweavers.com/crossover) (D3DMetal)
- BeamNG.drive installed via Steam inside a CrossOver bottle
- The game should launch and work in single-player first

### Installation

1. Download `BeamMP-Launcher.exe` from the [latest release](https://github.com/egrm/BeamMP-Launcher-macOS/releases)
2. Place it in your CrossOver bottle at:
   ```
   ~/Library/Application Support/CrossOver/Bottles/<YourBottle>/drive_c/users/crossover/AppData/Roaming/BeamMP-Launcher/BeamMP-Launcher.exe
   ```
   If the folder doesn't exist yet, run the [official BeamMP installer](https://beammp.com/) through CrossOver first — it creates the folder structure. Then replace the exe with ours.
3. Launch BeamMP through CrossOver
4. Click Multiplayer, Direct Connect, enter your server IP

That's it. No scripts, no manual patching.

### Updating

When BeamMP releases a new version, the launcher auto-updates its own exe but not the mod patches. Download the latest release from this repo and replace the exe again.

## What This Fixes

The official BeamMP launcher has bugs that break multiplayer under Wine/CrossOver. This fork fixes them.

### C++ Fixes (compiled into the launcher)

**1. TCP receive — `MSG_WAITALL` broken under Wine**

Wine's `MSG_WAITALL` returns partial data without error. The launcher receives truncated compressed packets, causing `zlib uncompress() failed (code: -3, message: data error)`. No vehicle sync, no position updates.

Fixed by replacing the single `recv()` call with a manual loop that accumulates bytes until the full packet arrives.

**2. Zlib decompression — streaming inflate**

Replaced `uncompress()` with streaming `inflate()` via `inflateInit2()`. More robust and handles partial/streaming data better under Wine's zlib environment.

### Mod Fixes (auto-patched in BeamMP.zip)

The launcher re-downloads `BeamMP.zip` (the in-game mod) on every launch. This fork automatically patches it after download.

**3. Network handler crash — `HandleNetwork[code](data)`**

Unknown packet codes crash the handler, killing all subsequent packet processing for that frame. Vehicle spawns, position updates — everything after the bad packet is dropped. Fixed with a nil guard.

**4. Vehicle coupler crash — `onServerVehicleCoupled`**

Coupler events for not-yet-registered vehicles crash with nil index. Fixed with a nil check.

**5. Login screen stuck — `isLoggedIn()` callback**

The `isLoggedIn()` JS function calls the Lua engine and waits for a callback that sometimes doesn't fire under Wine. The login screen shows "Attempting to log in..." forever. Fixed by returning `true` immediately.

**6. Data channel deadlock — non-blocking socket connect**

`connectToLauncher()` creates a non-blocking TCP socket. The immediate `send('A')` fails because the socket hasn't finished connecting. This sets `launcherConnected` to false permanently — the heartbeat only sends when `launcherConnected` is true, creating a deadlock where the flag can never flip. Fixed with a frame-based retry that periodically sends until the connection establishes.

## Building from Source

### Prerequisites

```bash
brew install cmake
```

Download [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) (ucrt-macos-universal) and extract to `~/llvm-mingw`.

### Build

```bash
git clone https://github.com/egrm/BeamMP-Launcher-macOS.git
cd BeamMP-Launcher-macOS

# First-time setup: downloads vcpkg, installs deps, configures, builds, and links
make setup

# Install into your CrossOver bottle (kills running BeamNG, copies exe, deletes stale BeamMP.zip)
make install
```

For subsequent builds after code changes:

```bash
make all install
```

The Makefile expects llvm-mingw at `/opt/llvm-mingw`. Override with `make MINGW_PATH=/your/path all`.

## Credits

- [BeamMP](https://beammp.com/) — the multiplayer mod
- [Alien4042x](https://github.com/Alien4042x/BeamMP-Launcher) — original macOS fork with the core networking fixes

## License

AGPL-3.0 — same as the official BeamMP Launcher.
