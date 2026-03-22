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

# Bootstrap vcpkg
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg && ./bootstrap-vcpkg.sh && cd ..

# Install Windows cross-compile dependencies
./vcpkg/vcpkg install --triplet=x64-mingw-static

# Configure
export PATH="$HOME/llvm-mingw/bin:$PATH"
mkdir build_windows && cd build_windows

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-static \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_C_COMPILER="$HOME/llvm-mingw/bin/x86_64-w64-mingw32-clang" \
    -DCMAKE_CXX_COMPILER="$HOME/llvm-mingw/bin/x86_64-w64-mingw32-clang++" \
    -DCMAKE_RC_COMPILER="$HOME/llvm-mingw/bin/x86_64-w64-mingw32-windres"

# Patch httplib for mingw compatibility
python3 -c "
p = 'vcpkg_installed/x64-mingw-static/include/httplib.h'
c = open(p).read().replace(
    'if (cancel_handle) { ::GetAddrInfoExCancel(&cancel_handle); }',
    '// mingw fix')
open(p, 'w').write(c)"

# Build + manual link (vcpkg calls powershell.exe during link which doesn't exist on macOS)
cmake --build . --config Release -j$(sysctl -n hw.ncpu) || true

x86_64-w64-mingw32-clang++ -o BeamMP-Launcher.exe \
  CMakeFiles/Launcher.dir/src/*.obj \
  CMakeFiles/Launcher.dir/src/Network/*.obj \
  CMakeFiles/Launcher.dir/src/Security/*.obj \
  -static -Lvcpkg_installed/x64-mingw-static/lib \
  -lcurl -lssl -lcrypto -lzlib \
  -lbrotlienc -lbrotlidec -lbrotlicommon \
  -lws2_32 -lcrypt32 -lbcrypt -lwldap32 -ladvapi32 -lshell32 -luser32 \
  -liphlpapi -lsecur32
```

## Credits

- [BeamMP](https://beammp.com/) — the multiplayer mod
- [Alien4042x](https://github.com/Alien4042x/BeamMP-Launcher) — original macOS fork with the core networking fixes

## License

AGPL-3.0 — same as the official BeamMP Launcher.
