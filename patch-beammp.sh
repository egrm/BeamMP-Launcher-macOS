#!/usr/bin/env bash
# Patches BeamMP.zip automatically after the launcher downloads it.
# Run BEFORE launching BeamMP. It watches for the download, patches, then exits.

BEAMMP_ZIP="$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Local/BeamNG/BeamNG.drive/current/mods/multiplayer/BeamMP.zip"
PATCH_DIR="/private/tmp/claude/beammp-patch"

echo "Waiting for BeamMP launcher to download BeamMP.zip..."
echo "Launch BeamMP now."

# Wait for the file to be written (launcher downloads it on each launch)
PREV_MOD=""
if [ -f "$BEAMMP_ZIP" ]; then
  PREV_MOD=$(stat -f %m "$BEAMMP_ZIP")
fi

while true; do
  if [ -f "$BEAMMP_ZIP" ]; then
    CUR_MOD=$(stat -f %m "$BEAMMP_ZIP")
    if [ "$CUR_MOD" != "$PREV_MOD" ]; then
      # File was updated — small delay to ensure write is complete
      sleep 0.3
      break
    fi
  fi
  sleep 0.2
done

echo "Download detected! Patching..."

mkdir -p "$PATCH_DIR"
cd "$PATCH_DIR"
unzip -o "$BEAMMP_ZIP" \
  "lua/ge/extensions/MPGameNetwork.lua" \
  "lua/ge/extensions/MPVehicleGE.lua" \
  "ui/modModules/multiplayer/multiplayer.js" \
  -d "$PATCH_DIR" > /dev/null 2>&1

# 1. MPGameNetwork.lua — nil guard on HandleNetwork[code]
sd 'HandleNetwork\[code\]\(data\)' 'local handler = HandleNetwork[code]; if handler then handler(data) end' \
  lua/ge/extensions/MPGameNetwork.lua 2>/dev/null

# 2. MPVehicleGE.lua — vehicle nil check in onServerVehicleCoupled
sd 'local vehicle = getVehicleByServerID\(serverVehicleID\) -- Get game ID\n\tif not vehicle\.isLocal then' \
  'local vehicle = getVehicleByServerID(serverVehicleID) -- Get game ID\n\tif not vehicle then return end\n\tif not vehicle.isLocal then' \
  lua/ge/extensions/MPVehicleGE.lua 2>/dev/null

# 3. multiplayer.js — isLoggedIn always true (Wine/CrossOver compat)
sd 'async function isLoggedIn\(\) \{' 'async function isLoggedIn() { return true; /* Wine compat */' \
  ui/modModules/multiplayer/multiplayer.js 2>/dev/null

# 5. multiplayer.js — LoginController: skip login screen, go straight to servers
LINE=$(grep -n "bngApi.engineLua('MPCoreNetwork.isLoggedIn()');" ui/modModules/multiplayer/multiplayer.js | head -1 | cut -d: -f1)
if [ -n "$LINE" ]; then
  sed -i '' "${LINE} a\\
	\$state.go('menu.multiplayer.servers'); /* Wine: skip login screen */
" ui/modModules/multiplayer/multiplayer.js
fi

# 4. multiplayer.js — null checks for clearFiltersButton
sd 'clearFiltersButton\.style\.display = "block"' 'if (clearFiltersButton) clearFiltersButton.style.display = "block"' \
  ui/modModules/multiplayer/multiplayer.js 2>/dev/null
sd 'clearFiltersButton\.style\.display = "none"' 'if (clearFiltersButton) clearFiltersButton.style.display = "none"' \
  ui/modModules/multiplayer/multiplayer.js 2>/dev/null

# Update zip
zip -u "$BEAMMP_ZIP" \
  lua/ge/extensions/MPGameNetwork.lua \
  lua/ge/extensions/MPVehicleGE.lua \
  ui/modModules/multiplayer/multiplayer.js > /dev/null 2>&1

echo "Patched! BeamNG will load the fixed version."
