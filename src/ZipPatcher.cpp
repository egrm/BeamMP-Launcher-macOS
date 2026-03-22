/*
 * ZipPatcher - Patches BeamMP.zip after download to fix Wine/CrossOver issues.
 * Rebuilds the zip with modified files, using only zlib (no minizip dependency).
 */

#include "Logger.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <zlib.h>

namespace {

struct ZipEntry {
    std::string filename;
    std::vector<uint8_t> uncompressed;
    uint16_t method;
    uint32_t crc32;
    uint32_t modTime;
    uint32_t modDate;
};

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.length(), to);
        pos += to.length();
    }
    return s;
}

std::string applyPatches(const std::string& filename, std::string content) {
    if (filename == "lua/ge/extensions/MPGameNetwork.lua") {
        content = replaceAll(content,
            "HandleNetwork[code](data)",
            "local handler = HandleNetwork[code]; if handler then handler(data) end");
        info("[ZipPatcher] Patched MPGameNetwork.lua (HandleNetwork nil guard)");
    }
    else if (filename == "lua/ge/extensions/MPVehicleGE.lua") {
        content = replaceAll(content,
            "local vehicle = getVehicleByServerID(serverVehicleID) -- Get game ID\n"
            "\tif not vehicle.isLocal then",
            "local vehicle = getVehicleByServerID(serverVehicleID) -- Get game ID\n"
            "\tif not vehicle then return end\n"
            "\tif not vehicle.isLocal then");
        info("[ZipPatcher] Patched MPVehicleGE.lua (vehicle nil check)");
    }
    else if (filename == "ui/modModules/multiplayer/multiplayer.js") {
        content = replaceAll(content,
            "async function isLoggedIn() {",
            "async function isLoggedIn() { return true; /* Wine compat */");
        content = replaceAll(content,
            "clearFiltersButton.style.display = \"block\"",
            "if (clearFiltersButton) clearFiltersButton.style.display = \"block\"");
        content = replaceAll(content,
            "clearFiltersButton.style.display = \"none\"",
            "if (clearFiltersButton) clearFiltersButton.style.display = \"none\"");
        info("[ZipPatcher] Patched multiplayer.js (login bypass + null checks)");
    }
    return content;
}

bool shouldPatch(const std::string& filename) {
    return filename == "lua/ge/extensions/MPGameNetwork.lua"
        || filename == "lua/ge/extensions/MPVehicleGE.lua"
        || filename == "ui/modModules/multiplayer/multiplayer.js";
}

// Read little-endian integers from buffer
uint16_t readU16(const uint8_t* p) { return p[0] | (p[1] << 8); }
uint32_t readU32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }
void writeU16(uint8_t* p, uint16_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; }
void writeU32(uint8_t* p, uint32_t v) { p[0] = v & 0xff; p[1] = (v >> 8) & 0xff; p[2] = (v >> 16) & 0xff; p[3] = (v >> 24) & 0xff; }

} // namespace

void PatchBeamMPZip(const std::filesystem::path& zipPath) {
    // Read entire zip into memory
    std::ifstream in(zipPath, std::ios::binary);
    if (!in) { error("[ZipPatcher] Cannot open " + zipPath.string()); return; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();

    // Find End of Central Directory (scan backwards for signature 0x06054b50)
    int eocdPos = -1;
    for (int i = data.size() - 22; i >= 0 && i >= (int)data.size() - 65557; i--) {
        if (readU32(&data[i]) == 0x06054b50) { eocdPos = i; break; }
    }
    if (eocdPos < 0) { error("[ZipPatcher] Not a valid zip"); return; }

    uint16_t numEntries = readU16(&data[eocdPos + 10]);
    uint32_t cdOffset = readU32(&data[eocdPos + 16]);

    // Parse central directory to find all entries
    std::vector<ZipEntry> entries;
    uint32_t pos = cdOffset;
    for (int i = 0; i < numEntries; i++) {
        if (readU32(&data[pos]) != 0x02014b50) { error("[ZipPatcher] Bad CD entry"); return; }
        uint16_t method = readU16(&data[pos + 10]);
        uint16_t modTime = readU16(&data[pos + 12]);
        uint16_t modDate = readU16(&data[pos + 14]);
        uint32_t crc = readU32(&data[pos + 16]);
        uint32_t compSize = readU32(&data[pos + 20]);
        uint32_t uncompSize = readU32(&data[pos + 24]);
        uint16_t nameLen = readU16(&data[pos + 28]);
        uint16_t extraLen = readU16(&data[pos + 30]);
        uint16_t commentLen = readU16(&data[pos + 32]);
        uint32_t localOffset = readU32(&data[pos + 42]);

        std::string name((char*)&data[pos + 46], nameLen);

        // Read from local file header to get actual data
        uint32_t lhPos = localOffset;
        if (readU32(&data[lhPos]) != 0x04034b50) { error("[ZipPatcher] Bad local header"); return; }
        uint16_t lhNameLen = readU16(&data[lhPos + 26]);
        uint16_t lhExtraLen = readU16(&data[lhPos + 28]);
        uint32_t dataStart = lhPos + 30 + lhNameLen + lhExtraLen;

        ZipEntry entry;
        entry.filename = name;
        entry.method = method;
        entry.crc32 = crc;
        entry.modTime = modTime;
        entry.modDate = modDate;

        if (method == 0) { // stored
            entry.uncompressed.assign(data.begin() + dataStart, data.begin() + dataStart + uncompSize);
        } else if (method == 8) { // deflated
            entry.uncompressed.resize(uncompSize);
            z_stream strm{};
            strm.next_in = &data[dataStart];
            strm.avail_in = compSize;
            strm.next_out = entry.uncompressed.data();
            strm.avail_out = uncompSize;
            inflateInit2(&strm, -15); // raw deflate
            inflate(&strm, Z_FINISH);
            inflateEnd(&strm);
            entry.uncompressed.resize(strm.total_out);
        }

        // Apply patches if needed
        if (shouldPatch(name)) {
            std::string content(entry.uncompressed.begin(), entry.uncompressed.end());
            std::string patched = applyPatches(name, content);
            entry.uncompressed.assign(patched.begin(), patched.end());
            entry.crc32 = ::crc32(0, entry.uncompressed.data(), entry.uncompressed.size());
        }

        entries.push_back(std::move(entry));
        pos += 46 + nameLen + extraLen + commentLen;
    }

    // Rebuild zip
    std::vector<uint8_t> out;
    struct CDEntry { uint32_t offset; std::string name; uint16_t method; uint32_t crc; uint32_t compSize; uint32_t uncompSize; uint16_t modTime; uint16_t modDate; };
    std::vector<CDEntry> cdEntries;

    for (auto& e : entries) {
        CDEntry cd;
        cd.offset = out.size();
        cd.name = e.filename;
        cd.modTime = e.modTime;
        cd.modDate = e.modDate;
        cd.crc = e.crc32;
        cd.uncompSize = e.uncompressed.size();

        // Compress
        std::vector<uint8_t> compressed;
        if (e.uncompressed.empty()) {
            cd.method = 0;
            cd.compSize = 0;
        } else {
            uLongf compLen = compressBound(e.uncompressed.size());
            compressed.resize(compLen);
            // Use raw deflate (no zlib header)
            z_stream strm{};
            deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
            strm.next_in = e.uncompressed.data();
            strm.avail_in = e.uncompressed.size();
            strm.next_out = compressed.data();
            strm.avail_out = compressed.size();
            deflate(&strm, Z_FINISH);
            deflateEnd(&strm);
            compressed.resize(strm.total_out);
            cd.method = 8;
            cd.compSize = compressed.size();
        }

        // Write local file header (signature 0x04034b50)
        uint8_t lh[30] = {};
        writeU32(lh, 0x04034b50);
        writeU16(lh + 4, 20); // version needed
        writeU16(lh + 8, cd.method);
        writeU16(lh + 10, cd.modTime);
        writeU16(lh + 12, cd.modDate);
        writeU32(lh + 14, cd.crc);
        writeU32(lh + 18, cd.compSize);
        writeU32(lh + 22, cd.uncompSize);
        writeU16(lh + 26, e.filename.size());
        out.insert(out.end(), lh, lh + 30);
        out.insert(out.end(), e.filename.begin(), e.filename.end());
        out.insert(out.end(), compressed.begin(), compressed.end());

        cdEntries.push_back(cd);
    }

    // Write central directory
    uint32_t cdStart = out.size();
    for (auto& cd : cdEntries) {
        uint8_t cde[46] = {};
        writeU32(cde, 0x02014b50);
        writeU16(cde + 4, 20); // version made by
        writeU16(cde + 6, 20); // version needed
        writeU16(cde + 10, cd.method);
        writeU16(cde + 12, cd.modTime);
        writeU16(cde + 14, cd.modDate);
        writeU32(cde + 16, cd.crc);
        writeU32(cde + 20, cd.compSize);
        writeU32(cde + 24, cd.uncompSize);
        writeU16(cde + 28, cd.name.size());
        writeU32(cde + 42, cd.offset);
        out.insert(out.end(), cde, cde + 46);
        out.insert(out.end(), cd.name.begin(), cd.name.end());
    }
    uint32_t cdSize = out.size() - cdStart;

    // Write EOCD
    uint8_t eocd[22] = {};
    writeU32(eocd, 0x06054b50);
    writeU16(eocd + 8, cdEntries.size());
    writeU16(eocd + 10, cdEntries.size());
    writeU32(eocd + 12, cdSize);
    writeU32(eocd + 16, cdStart);
    out.insert(out.end(), eocd, eocd + 22);

    // Write back
    std::ofstream outFile(zipPath, std::ios::binary | std::ios::trunc);
    outFile.write((char*)out.data(), out.size());
    outFile.close();

    info("[ZipPatcher] BeamMP.zip patched successfully (" + std::to_string(entries.size()) + " entries)");
}
