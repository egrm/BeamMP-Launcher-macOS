# BeamMP Launcher - macOS Fork

Experimental fork of the [BeamMP Launcher](https://github.com/BeamMP/BeamMP-Launcher) that fixes multiplayer for **macOS via CrossOver/D3DMetal**.

> **This is NOT the official BeamMP launcher.** Windows and Linux users should use the [official release](https://beammp.com/).

## What's Fixed

The official BeamMP launcher has two bugs that break multiplayer under Wine/CrossOver:

### 1. TCP Receive (`MSG_WAITALL` broken under Wine)

Wine's implementation of `MSG_WAITALL` returns partial data without error. The launcher uses this flag to receive compressed packets — when it gets a truncated packet, zlib decompression fails with `Z_DATA_ERROR (-3)`.

**Fix:** Replaced single `recv(..., MSG_WAITALL)` call with a manual receive loop that accumulates bytes until the full packet arrives.

```cpp
// Before (broken under Wine):
recv(Sock, Data.data(), Header, MSG_WAITALL);

// After:
while (received < Header) {
    int chunk = recv(Sock, Data.data() + received, Header - received, 0);
    if (chunk <= 0) return "";
    received += chunk;
}
```

**File:** `src/Network/VehicleEvent.cpp`

### 2. Zlib Decompression (streaming inflate)

Replaced `uncompress()` with streaming `inflate()` + `inflateInit2()`. The streaming API is more robust and handles edge cases better under Wine's zlib environment.

**File:** `src/Compressor.cpp`

### Result

Without these fixes, you get:
- `zlib uncompress() failed (code: -3, message: data error)` on every connection
- Can't see other players' cars
- No vehicle sync, position updates, or flood mod effects
- Chat works (small TCP packets that don't hit the bug)

With these fixes: full multiplayer works — vehicle sync, position updates, mods, everything.

## Additional Client-Side Patches

The launcher re-downloads `BeamMP.zip` (the in-game mod) on every launch. This mod has issues under Wine that require patching after each download. A `patch-beammp` script is included that fixes:

1. **`MPGameNetwork.lua:495`** - Network handler crashes on unknown packet codes, killing all subsequent packet processing (vehicle sync, position updates). Fixed with nil guard.
2. **`MPVehicleGE.lua:1908`** - Vehicle coupler crash when receiving data for unknown vehicles. Fixed with nil check.
3. **`multiplayer.js`** - `isLoggedIn()` callback never fires under Wine, causing stuck login screen. Fixed by making it return `true` immediately.

Run `patch-beammp` **before** launching BeamMP — it watches for the download and patches the zip before the game loads it.

## Building (Cross-compile on macOS)

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

# Install Windows dependencies
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

# Patch httplib for mingw
python3 -c "
p = 'vcpkg_installed/x64-mingw-static/include/httplib.h'
with open(p) as f: c = f.read()
c = c.replace('if (cancel_handle) { ::GetAddrInfoExCancel(&cancel_handle); }', '// mingw fix')
with open(p, 'w') as f: f.write(c)
"

# Build (link manually to avoid powershell dependency)
cmake --build . --config Release -j$(sysctl -n hw.ncpu) 2>&1 || true

# Manual link (vcpkg tries to call powershell.exe during link step)
VCPKG_LIBS="vcpkg_installed/x64-mingw-static/lib"
x86_64-w64-mingw32-clang++ -o BeamMP-Launcher.exe \
  CMakeFiles/Launcher.dir/src/*.obj \
  CMakeFiles/Launcher.dir/src/Network/*.obj \
  CMakeFiles/Launcher.dir/src/Security/*.obj \
  -static -L"$VCPKG_LIBS" \
  -lcurl -lssl -lcrypto -lzlib \
  -lbrotlienc -lbrotlidec -lbrotlicommon \
  -lws2_32 -lcrypt32 -lbcrypt -lwldap32 -ladvapi32 -lshell32 -luser32 \
  -liphlpapi -lsecur32
```

### Install

Copy the built `BeamMP-Launcher.exe` to your CrossOver bottle:

```bash
LAUNCHER="$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Roaming/BeamMP-Launcher"
cp "$LAUNCHER/BeamMP-Launcher.exe" "$LAUNCHER/BeamMP-Launcher.exe.backup"
cp build_windows/BeamMP-Launcher.exe "$LAUNCHER/"
```

## Usage

1. Run `patch-beammp` in a terminal (watches for BeamMP.zip download)
2. Launch BeamMP through CrossOver
3. The script patches the mod zip before the game loads it
4. Click Multiplayer, Direct Connect to your server

## Credits

- [BeamMP](https://beammp.com/) — the multiplayer mod
- [Alien4042x](https://github.com/Alien4042x/BeamMP-Launcher) — original macOS fork with the key networking fixes
- Built and tested on macOS with CrossOver 24 + D3DMetal + BeamNG.drive 0.38.4

## License

AGPL-3.0 — same as the official BeamMP Launcher.
