MINGW_PATH ?= /opt/llvm-mingw
BUILD_DIR   = build_windows
VCPKG_LIBS  = $(BUILD_DIR)/vcpkg_installed/x64-mingw-static/lib
OBJS        = $(BUILD_DIR)/CMakeFiles/Launcher.dir/src/*.obj \
              $(BUILD_DIR)/CMakeFiles/Launcher.dir/src/Network/*.obj \
              $(BUILD_DIR)/CMakeFiles/Launcher.dir/src/Security/*.obj
LIBS        = -lcurl -lssl -lcrypto -lzlib \
              -lbrotlienc -lbrotlidec -lbrotlicommon \
              -lws2_32 -lcrypt32 -lbcrypt -lwldap32 -ladvapi32 -lshell32 -luser32 \
              -liphlpapi -lsecur32
EXE         = $(BUILD_DIR)/BeamMP-Launcher.exe

export PATH := $(MINGW_PATH)/bin:$(PATH)

.PHONY: all configure build link install clean deps patch-httplib

all: build link

# One-time: bootstrap vcpkg and install dependencies
deps:
	@if [ ! -d vcpkg ]; then \
		echo "Cloning vcpkg..."; \
		git clone https://github.com/Microsoft/vcpkg.git; \
		cd vcpkg && ./bootstrap-vcpkg.sh; \
	fi
	./vcpkg/vcpkg install --triplet=x64-mingw-static

# Configure cmake (run after deps, or when CMakeLists.txt changes)
configure: patch-httplib
	@mkdir -p $(BUILD_DIR)
	cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
		-DVCPKG_TARGET_TRIPLET=x64-mingw-static \
		-DCMAKE_SYSTEM_NAME=Windows \
		-DCMAKE_C_COMPILER="$(MINGW_PATH)/bin/x86_64-w64-mingw32-clang" \
		-DCMAKE_CXX_COMPILER="$(MINGW_PATH)/bin/x86_64-w64-mingw32-clang++" \
		-DCMAKE_RC_COMPILER="$(MINGW_PATH)/bin/x86_64-w64-mingw32-windres"

patch-httplib:
	@HTTPLIB="$(BUILD_DIR)/vcpkg_installed/x64-mingw-static/include/httplib.h"; \
	if [ -f "$$HTTPLIB" ] && grep -q 'GetAddrInfoExCancel' "$$HTTPLIB"; then \
		python3 -c "p='$$HTTPLIB';c=open(p).read().replace('if (cancel_handle) { ::GetAddrInfoExCancel(&cancel_handle); }','// mingw fix');open(p,'w').write(c)"; \
		echo "Patched httplib.h"; \
	fi

# Compile (cmake handles incremental builds)
build:
	cd $(BUILD_DIR) && cmake --build . --config Release -j$$(sysctl -n hw.ncpu) 2>&1 | grep -v "^make\|powershell" || true

# Link (manual step — vcpkg tries to call powershell.exe which doesn't exist on macOS)
link:
	x86_64-w64-mingw32-clang++ -o $(EXE) $(OBJS) -static -L$(VCPKG_LIBS) $(LIBS)
	@echo "Built: $(EXE) ($$(du -h $(EXE) | cut -f1))"

# Install into CrossOver bottle (auto-detects bottle path)
install:
	@LAUNCHER_DIR="$$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Roaming/BeamMP-Launcher"; \
	if [ ! -d "$$LAUNCHER_DIR" ]; then echo "BeamMP-Launcher folder not found. Run the official installer first."; exit 1; fi; \
	pkill -9 -f "BeamNG" 2>/dev/null || true; \
	pkill -9 -f "BeamMP" 2>/dev/null || true; \
	sleep 1; \
	cp $(EXE) "$$LAUNCHER_DIR/BeamMP-Launcher.exe"; \
	rm -f "$$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/users/crossover/AppData/Local/BeamNG/BeamNG.drive/current/mods/multiplayer/BeamMP.zip"; \
	echo "Installed. Launch BeamMP."

clean:
	rm -rf $(BUILD_DIR)

# First-time setup: deps + configure + build + link
setup: deps configure all
