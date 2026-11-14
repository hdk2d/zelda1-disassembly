#include <jni.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

struct ParsedRom {
    std::vector<std::uint8_t> prg;
    std::vector<std::uint8_t> chr;
    std::vector<std::uint8_t> trainer;
    std::uint32_t mapper = 0;
    std::uint32_t submapper = 0;
    bool nes2 = false;
    bool chrRam = false;
};

std::vector<std::uint8_t> g_prg_rom;
std::vector<std::uint8_t> g_chr_data;
std::vector<std::uint8_t> g_trainer;
std::uint32_t g_mapper = 0;
std::uint32_t g_submapper = 0;
bool g_chr_is_ram = false;
std::vector<std::uint32_t> g_link_pixels;
std::vector<std::uint32_t> g_background_pixels;
std::vector<std::uint32_t> g_background_brown_pixels;
std::vector<std::uint32_t> g_overworld_pixels;
std::vector<std::uint32_t> g_octorok_pixels;
std::vector<std::uint32_t> g_octorok_demo_pixels;
float g_octorok_demo_x = 24.0f;
float g_octorok_demo_y = 80.0f;
float g_octorok_demo_velocity = 0.5f;
std::uint32_t g_octorok_demo_tick = 0;
bool g_octorok_demo_active = false;

bool renderOctorokDemoFrame() {
    constexpr std::size_t width = 256;
    constexpr std::size_t height = 176;
    constexpr std::size_t spriteSize = 16;
    if (g_overworld_pixels.size() != width * height ||
        g_octorok_pixels.size() != 6 * spriteSize * spriteSize) return false;

    g_octorok_demo_pixels = g_overworld_pixels;
    const std::size_t frame = (g_octorok_demo_tick / 8u) % 6u;
    const int originX = static_cast<int>(g_octorok_demo_x);
    const int originY = static_cast<int>(g_octorok_demo_y);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const std::uint32_t pixel =
                g_octorok_pixels[frame * 256u + static_cast<std::size_t>(y * 16 + x)];
            if ((pixel >> 24) == 0) continue;
            const int targetX = originX + x;
            const int targetY = originY + y;
            if (targetX < 0 || targetX >= static_cast<int>(width) ||
                targetY < 0 || targetY >= static_cast<int>(height)) continue;
            g_octorok_demo_pixels[static_cast<std::size_t>(targetY) * width +
                                  static_cast<std::size_t>(targetX)] = pixel;
        }
    }
    return true;
}

std::uint8_t g_overworld_start_room = 0xFF;
std::uint8_t g_overworld_unique_room = 0xFF;

bool extractLinkSprite() {
    // Common sprite patterns begin at CPU address $807F in PRG bank 2.
    constexpr std::uint8_t signature[] = {
        0x07, 0x00, 0x07, 0x00, 0x00, 0xE0,
        0x00, 0x00, 0x10, 0x00, 0x1F, 0x20
    };
    constexpr std::size_t bankSize = 16 * 1024;
    constexpr std::size_t bank2Offset = 2 * bankSize;
    if (g_prg_rom.size() < bank2Offset + bankSize) return false;

    std::size_t spriteOffset = g_prg_rom.size();
    for (std::size_t i = 6; i + sizeof(signature) <= bankSize; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < sizeof(signature); ++j) {
            if (g_prg_rom[bank2Offset + i + j] != signature[j]) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        const std::uint16_t address =
            static_cast<std::uint16_t>(g_prg_rom[bank2Offset + i - 6]) |
            (static_cast<std::uint16_t>(g_prg_rom[bank2Offset + i - 5]) << 8);
        if (address < 0x8000 || address >= 0xC000) continue;
        spriteOffset = bank2Offset + (address - 0x8000);
        break;
    }
    if (spriteOffset == g_prg_rom.size() || spriteOffset + 0x700 > g_prg_rom.size()) {
        return false;
    }

    // Link uses two adjacent 8x16 OAM sprites. In 8x16 mode each OAM
    // entry draws an even tile followed by the next tile vertically:
    // left column = 00/01; right column = 02/03. The preview is:
    // 00 02
    // 01 03
    constexpr std::size_t tileIds[] = {0x00, 0x02, 0x01, 0x03};

    // Link's overworld sprite palette, stored in PRG at file offset 0x19323
    // (iNES header excluded from this buffer): 0F, 29, 27, 17.
    constexpr std::size_t linkPaletteOffset = 0x19313;
    if (linkPaletteOffset + 4 > g_prg_rom.size()) return false;
    const std::uint8_t paletteIndices[4] = {
        g_prg_rom[linkPaletteOffset], g_prg_rom[linkPaletteOffset + 1],
        g_prg_rom[linkPaletteOffset + 2], g_prg_rom[linkPaletteOffset + 3]
    };

    // Common 2C02 RGB approximation for the four palette entries used by Link.
    auto nesColor = [](std::uint8_t index) -> std::uint32_t {
        switch (index & 0x3F) {
            case 0x29: return 0xFF78D800; // green tunic
            case 0x27: return 0xFFFA9E00; // warm skin/tan
            case 0x17: return 0xFF994E00; // brown
            default:   return 0xFF000000;
        }
    };
    const std::uint32_t colors[4] = {
        0x00000000, nesColor(paletteIndices[1]),
        nesColor(paletteIndices[2]), nesColor(paletteIndices[3])
    };

    g_link_pixels.assign(16 * 16, 0);
    for (std::size_t tileIndex = 0; tileIndex < 4; ++tileIndex) {
        const std::size_t tile = spriteOffset + tileIds[tileIndex] * 16;
        const std::size_t tileX = (tileIndex & 1) * 8;
        const std::size_t tileY = (tileIndex >> 1) * 8;
        for (int y = 0; y < 8; ++y) {
            const std::uint8_t plane0 = g_prg_rom[tile + y];
            const std::uint8_t plane1 = g_prg_rom[tile + 8 + y];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const unsigned pixel =
                    ((plane0 >> bit) & 1u) | (((plane1 >> bit) & 1u) << 1);
                g_link_pixels[(tileY + y) * 16 + tileX + x] = colors[pixel];
            }
        }
    }
    return true;
}


bool extractBackgroundTiles() {
    // Common background tiles occupy IDs $00-$6F (112 tiles).
    constexpr std::uint8_t signature[] = {
        0x07, 0x00, 0x07, 0x00, 0x00, 0xE0,
        0x00, 0x00, 0x10, 0x00, 0x1F, 0x20
    };
    constexpr std::size_t bankSize = 16 * 1024;
    constexpr std::size_t bank2Offset = 2 * bankSize;
    if (g_prg_rom.size() < bank2Offset + bankSize) return false;

    std::size_t commonOffset = g_prg_rom.size();
    std::size_t miscOffset = g_prg_rom.size();
    for (std::size_t i = 6; i + sizeof(signature) <= bankSize; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < sizeof(signature); ++j) {
            if (g_prg_rom[bank2Offset + i + j] != signature[j]) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        const std::uint16_t address =
            static_cast<std::uint16_t>(g_prg_rom[bank2Offset + i - 4]) |
            (static_cast<std::uint16_t>(g_prg_rom[bank2Offset + i - 3]) << 8);
        if (address < 0x8000 || address >= 0xC000) continue;
        commonOffset = bank2Offset + (address - 0x8000);
        const std::uint16_t miscAddress =
            static_cast<std::uint16_t>(g_prg_rom[bank2Offset + i - 2]) |
            (static_cast<std::uint16_t>(g_prg_rom[bank2Offset + i - 1]) << 8);
        if (miscAddress >= 0x8000 && miscAddress < 0xC000) {
            miscOffset = bank2Offset + (miscAddress - 0x8000);
        }
        break;
    }

    // The overworld-specific background block is PatternBlockOWBG:
    // PRG bank 3, CPU address $893B, 0x820 bytes = 130 tiles ($70-$F1).
    constexpr std::size_t overworldOffset = 3 * bankSize + 0x093B;
    constexpr std::size_t commonSize = 0x700;
    constexpr std::size_t miscSize = 0x00E0;
    constexpr std::size_t overworldSize = 0x0820;
    if (commonOffset == g_prg_rom.size() || miscOffset == g_prg_rom.size() ||
        commonOffset + commonSize > g_prg_rom.size() ||
        miscOffset + miscSize > g_prg_rom.size() ||
        overworldOffset + overworldSize > g_prg_rom.size() ||
        g_prg_rom.size() <= 0x19312) return false;

    // Original overworld palettes in the PRG data:
    // green/forest = $1930B: 0F 1A 37 12
    // brown/mountain = $1930F: 0F 17 37 12
    const std::uint8_t greenPalette[4] = {
        g_prg_rom[0x1930B], g_prg_rom[0x1930C],
        g_prg_rom[0x1930D], g_prg_rom[0x1930E]
    };
    const std::uint8_t brownPalette[4] = {
        g_prg_rom[0x1930F], g_prg_rom[0x19310],
        g_prg_rom[0x19311], g_prg_rom[0x19312]
    };

    // Standard 2C02 RGB approximation. Palette indices are read from the ROM.
    constexpr std::uint32_t nesRgb[64] = {
        0xFF7C7C7C,0xFF0000FC,0xFF0000BC,0xFF4428BC,
        0xFF940084,0xFFA80020,0xFFA81000,0xFF881400,
        0xFF503000,0xFF007800,0xFF006800,0xFF005800,
        0xFF004058,0xFF000000,0xFF000000,0xFF000000,
        0xFFBCBCBC,0xFF0078F8,0xFF0058F8,0xFF6844FC,
        0xFFD800CC,0xFFE40058,0xFFF83800,0xFFE45C10,
        0xFFAC7C00,0xFF00B800,0xFF00A800,0xFF00A844,
        0xFF008888,0xFF000000,0xFF000000,0xFF000000,
        0xFFF8F8F8,0xFF3CBCFC,0xFF6888FC,0xFF9878F8,
        0xFFF878F8,0xFFF85898,0xFFF87858,0xFFFCA044,
        0xFFF8B800,0xFFB8F818,0xFF58D854,0xFF58F898,
        0xFF00E8D8,0xFF787878,0xFF000000,0xFF000000,
        0xFFFCFCFC,0xFFA4E4FC,0xFFB8B8F8,0xFFD8B8F8,
        0xFFF8B8F8,0xFFF8A4C0,0xFFF0D0B0,0xFFFCE0A8,
        0xFFF8D878,0xFFD8F878,0xFFB8F8B8,0xFFB8F8D8,
        0xFF00FCFC,0xFFF8D8F8,0xFF000000,0xFF000000
    };

    constexpr std::size_t columns = 16;
    constexpr std::size_t atlasWidth = 128;
    constexpr std::size_t atlasHeight = 128; // 256 tile slots, 16x16 tiles.
    g_background_pixels.assign(atlasWidth * atlasHeight, 0xFF202020);
    g_background_brown_pixels.assign(atlasWidth * atlasHeight, 0xFF202020);

    for (std::size_t tileIndex = 0; tileIndex < 256; ++tileIndex) {
        std::size_t source = 0;
        if (tileIndex < 112) {
            source = commonOffset + tileIndex * 16;
        } else if (tileIndex >= 0x70 && tileIndex <= 0xF1) {
            source = overworldOffset + (tileIndex - 0x70) * 16;
        } else if (tileIndex >= 0xF2 && tileIndex <= 0xFF) {
            source = miscOffset + (tileIndex - 0xF2) * 16;
        } else {
            continue; // Unused/reserved background tile slots.
        }

        const std::size_t tileX = (tileIndex % columns) * 8;
        const std::size_t tileY = (tileIndex / columns) * 8;
        for (int y = 0; y < 8; ++y) {
            const std::uint8_t plane0 = g_prg_rom[source + y];
            const std::uint8_t plane1 = g_prg_rom[source + 8 + y];
            for (int x = 0; x < 8; ++x) {
                const int bit = 7 - x;
                const unsigned pixel =
                    ((plane0 >> bit) & 1u) | (((plane1 >> bit) & 1u) << 1);
                const std::size_t dst = (tileY + y) * atlasWidth + tileX + x;
                const std::uint8_t greenIndex = greenPalette[pixel] & 0x3F;
                const std::uint8_t brownIndex = brownPalette[pixel] & 0x3F;
                g_background_pixels[dst] = nesRgb[greenIndex];
                g_background_brown_pixels[dst] = nesRgb[brownIndex];
            }
        }
    }
    return true;
}



bool extractOverworldOctorok() {
    // PatternBlockOWSP starts at PRG bank 3, CPU address $915B.
    // The six slow red Octorok frames are selected by ObjAnimFrameHeap.
    constexpr std::size_t bankSize = 16 * 1024;
    constexpr std::size_t spriteBlockOffset = 3 * bankSize + 0x115B;
    constexpr std::size_t spriteBlockSize = 0x0720;
    constexpr std::uint8_t frameTiles[6] = {
        0xB4, 0xB0, 0xB0, 0xB8, 0xB2, 0xB2
    };
    constexpr std::uint8_t frameAttributes[6] = {
        0x02, 0x82, 0x02, 0x02, 0x82, 0x02
    };
    constexpr std::size_t firstPatternTile = 0x8E;
    constexpr std::size_t frameWidth = 16;
    constexpr std::size_t frameHeight = 16;
    constexpr std::size_t frameCount = 6;
    constexpr std::size_t redEnemyPaletteOffset = 0x1931B;
    if (g_prg_rom.size() < spriteBlockOffset + spriteBlockSize ||
        redEnemyPaletteOffset + 4 > g_prg_rom.size()) return false;

    // The US PRG0 overworld red-enemy sprite palette is stored at PRG offset
    // $1931B: 0F, 16, 27, 30. Read it from the loaded ROM, not from constants.
    const std::uint8_t paletteIndices[4] = {
        g_prg_rom[redEnemyPaletteOffset],
        g_prg_rom[redEnemyPaletteOffset + 1],
        g_prg_rom[redEnemyPaletteOffset + 2],
        g_prg_rom[redEnemyPaletteOffset + 3]
    };

    // Standard 2C02 RGB approximation, shared in value with the overworld
    // background extractor. Pixel 0 remains transparent for NES sprites.
    constexpr std::uint32_t nesRgb[64] = {
        0xFF7C7C7C,0xFF0000FC,0xFF0000BC,0xFF4428BC,
        0xFF940084,0xFFA80020,0xFFA81000,0xFF881400,
        0xFF503000,0xFF007800,0xFF006800,0xFF005800,
        0xFF004058,0xFF000000,0xFF000000,0xFF000000,
        0xFFBCBCBC,0xFF0078F8,0xFF0058F8,0xFF6844FC,
        0xFFD800CC,0xFFE40058,0xFFF83800,0xFFE45C10,
        0xFFAC7C00,0xFF00B800,0xFF00A800,0xFF00A844,
        0xFF008888,0xFF000000,0xFF000000,0xFF000000,
        0xFFF8F8F8,0xFF3CBCFC,0xFF6888FC,0xFF9878F8,
        0xFFF878F8,0xFFF85898,0xFFF87858,0xFFFCA044,
        0xFFF8B800,0xFFB8F818,0xFF58D854,0xFF58F898,
        0xFF00E8D8,0xFF787878,0xFF000000,0xFF000000,
        0xFFFCFCFC,0xFFA4E4FC,0xFFB8B8F8,0xFFD8B8F8,
        0xFFF8B8F8,0xFFF8A4C0,0xFFF0D0B0,0xFFFCE0A8,
        0xFFF8D878,0xFFD8F878,0xFFB8F8B8,0xFFB8F8D8,
        0xFF00FCFC,0xFFF8D8F8,0xFF000000,0xFF000000
    };
    const std::uint32_t colors[4] = {
        0x00000000,
        nesRgb[paletteIndices[1] & 0x3F],
        nesRgb[paletteIndices[2] & 0x3F],
        nesRgb[paletteIndices[3] & 0x3F]
    };
    g_octorok_pixels.assign(frameWidth * frameHeight * frameCount, 0);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const std::size_t baseTile = frameTiles[frame] - firstPatternTile;
        for (std::size_t tile = 0; tile < 4; ++tile) {
            const std::size_t tileId = baseTile + (tile & 1u) * 2u + (tile >> 1u);
            const std::size_t source = spriteBlockOffset + tileId * 16;
            if (source + 16 > spriteBlockOffset + spriteBlockSize) return false;
            for (std::size_t y = 0; y < 8; ++y) {
                const std::size_t sourceY =
                    (frameAttributes[frame] & 0x80u) ? 7u - y : y;
                const std::uint8_t plane0 = g_prg_rom[source + sourceY];
                const std::uint8_t plane1 = g_prg_rom[source + 8 + sourceY];
                for (std::size_t x = 0; x < 8; ++x) {
                    const unsigned bit = static_cast<unsigned>(7 - x);
                    const unsigned pixel =
                        ((plane0 >> bit) & 1u) | (((plane1 >> bit) & 1u) << 1u);
                    const std::size_t tileX = (tile & 1u) * 8u;
                    const std::size_t tileY = (tile >> 1u) * 8u;
                    const std::size_t target =
                        frame * frameWidth * frameHeight +
                        (tileY + y) * frameWidth + tileX + x;
                    g_octorok_pixels[target] = colors[pixel];
                }
            }
        }
    }
    return true;
}

bool extractStartingOverworldRoom() {
    if (g_prg_rom.size() < 8 * 16 * 1024 ||
        g_background_pixels.size() != 128 * 128) return false;

    constexpr std::size_t bankSize = 16 * 1024;
    constexpr std::size_t bank5 = 5 * bankSize;
    constexpr std::size_t bank6 = 6 * bankSize;

    // LevelBlockAddrsQ1 and LevelInfoAddrs are the first two ROM tables
    // in bank 6. Their first entries point to the original overworld data.
    const auto readWord = [](const std::vector<std::uint8_t>& data,
                             std::size_t offset) -> std::uint16_t {
        return static_cast<std::uint16_t>(data[offset]) |
               (static_cast<std::uint16_t>(data[offset + 1]) << 8);
    };
    const std::uint16_t levelBlockAddress = readWord(g_prg_rom, bank6);
    const std::uint16_t levelInfoAddress = readWord(g_prg_rom, bank6 + 20);
    if (levelBlockAddress < 0x8000 || levelBlockAddress >= 0xC000 ||
        levelInfoAddress < 0x8000 || levelInfoAddress >= 0xC000) return false;

    const std::size_t levelBlockOffset =
        bank6 + static_cast<std::size_t>(levelBlockAddress - 0x8000);
    const std::size_t levelInfoOffset =
        bank6 + static_cast<std::size_t>(levelInfoAddress - 0x8000);
    if (levelBlockOffset + 0x300 > g_prg_rom.size() ||
        levelInfoOffset + 0x100 > g_prg_rom.size()) return false;

    // LevelInfo_StartRoomId is at offset $2F in the 256-byte OW info block.
    // LevelBlockAttrsD is the fourth 128-byte array in LevelBlockOW.
    g_overworld_start_room = g_prg_rom[levelInfoOffset + 0x2F];
    if (g_overworld_start_room >= 0x80) return false;
    const std::uint8_t roomAttributes =
        g_prg_rom[levelBlockOffset + 0x180 + g_overworld_start_room];
    g_overworld_unique_room = roomAttributes & 0x3F;

    // RoomLayoutsOW is 0x790 bytes in the original ROM. The three
    // 16-byte cave layouts immediately precede ColumnHeapOW0.
    constexpr std::size_t roomLayoutsDataSize = 0x0790;
    constexpr std::size_t caveLayoutsSize = 3 * 16;
    constexpr std::size_t roomLayoutCount = 64;
    constexpr std::size_t roomLayoutSize = 16;
    constexpr std::uint8_t columnHeapSignature[] = {
        0xDB, 0x5B, 0x5B, 0xDB, 0x5B, 0x1B, 0x0E, 0x1A,
        0x5B, 0xDB, 0x4E, 0x4E, 0x4E, 0x0E, 0x1A, 0x1B
    };
    std::size_t columnHeap0Offset = g_prg_rom.size();
    for (std::size_t i = bank5; i + sizeof(columnHeapSignature) <= bank5 + bankSize; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < sizeof(columnHeapSignature); ++j) {
            if (g_prg_rom[i + j] != columnHeapSignature[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            columnHeap0Offset = i;
            break;
        }
    }
    const std::size_t layoutsAndCavesSize =
        roomLayoutsDataSize + caveLayoutsSize;
    if (columnHeap0Offset == g_prg_rom.size() ||
        columnHeap0Offset < bank5 + layoutsAndCavesSize) return false;

    // The room ID indexes the first 64 layouts, exactly as LayoutRoomOW does.
    const std::size_t roomLayoutsOffset =
        columnHeap0Offset - layoutsAndCavesSize;
    const std::size_t roomLayoutsEnd =
        roomLayoutsOffset + roomLayoutCount * roomLayoutSize;
    const std::size_t roomLayoutOffset =
        roomLayoutsOffset +
        static_cast<std::size_t>(g_overworld_unique_room) * roomLayoutSize;
    if (roomLayoutOffset + roomLayoutSize > roomLayoutsEnd ||
        roomLayoutsEnd > columnHeap0Offset - caveLayoutsSize) return false;

    constexpr std::uint16_t columnDirectory[16] = {
        0x9BD8, 0x9C0D, 0x9C3E, 0x9C80,
        0x9CC4, 0x9CF6, 0x9D32, 0x9D6D,
        0x9DA8, 0x9DE6, 0x9E27, 0x9E6C,
        0x9EA9, 0x9EDF, 0x9F21, 0x9F55
    };
    constexpr std::uint8_t primarySquares[56] = {
        0x24, 0x6F, 0xF3, 0xFA, 0x98, 0x90, 0x8F, 0x95,
        0x8E, 0x90, 0x74, 0x76, 0xF3, 0x24, 0x26, 0x89,
        0x03, 0x04, 0x70, 0xC8, 0xBC, 0x8D, 0x8F, 0x93,
        0x95, 0xC4, 0xCE, 0xD8, 0xB0, 0xB4, 0xAA, 0xAC,
        0xB8, 0x9C, 0xA6, 0x9A, 0xA2, 0xA0, 0xE5, 0xE6,
        0xE7, 0xE8, 0xE9, 0xEA, 0xC0, 0xE0, 0x78, 0x7A,
        0x7E, 0x80, 0xCC, 0xD0, 0xD4, 0xDC, 0x89, 0x84
    };
    constexpr std::uint8_t secondarySquares[64] = {
        0x24, 0x24, 0x24, 0x24, 0x6F, 0x6F, 0x6F, 0x6F,
        0xF3, 0xF3, 0xF3, 0xF3, 0xFA, 0xFA, 0xFA, 0xFA,
        0x98, 0x95, 0x26, 0x26, 0x90, 0x95, 0x90, 0x95,
        0x8F, 0x90, 0x8F, 0x90, 0x95, 0x96, 0x95, 0x96,
        0x8E, 0x93, 0x90, 0x95, 0x90, 0x95, 0x92, 0x97,
        0x74, 0x74, 0x75, 0x75, 0x76, 0x77, 0x76, 0x77,
        0xF3, 0x24, 0xF3, 0x24, 0x24, 0x24, 0x24, 0x24,
        0x26, 0x26, 0x26, 0x26, 0x89, 0x88, 0x8B, 0x88
    };

    // The original layout is 16 columns x 11 two-by-two-tile squares.
    std::uint8_t tileMap[32 * 22] = {};
    for (std::size_t column = 0; column < 16; ++column) {
        const std::uint8_t descriptor = g_prg_rom[roomLayoutOffset + column];
        const std::size_t tableIndex = descriptor >> 4;
        const std::size_t columnIndex = descriptor & 0x0F;
        const std::uint16_t tableAddress = columnDirectory[tableIndex];
        const std::size_t tableOffset =
            bank5 + static_cast<std::size_t>(tableAddress - 0x8000);
        const std::size_t tableEnd = tableIndex + 1 < 16
            ? bank5 + static_cast<std::size_t>(columnDirectory[tableIndex + 1] - 0x8000)
            : bank5 + bankSize;
        if (tableOffset >= tableEnd || tableEnd > bank5 + bankSize) return false;

        std::size_t position = tableEnd;
        std::size_t foundColumns = 0;
        for (std::size_t scan = tableOffset; scan < tableEnd; ++scan) {
            if ((g_prg_rom[scan] & 0x80) == 0) continue;
            if (foundColumns == columnIndex) {
                position = scan;
                break;
            }
            ++foundColumns;
        }
        if (position == tableEnd) return false;

        bool repeatSquare = false;
        for (std::size_t squareRow = 0; squareRow < 11; ++squareRow) {
            if (position >= tableEnd) return false;
            const std::uint8_t squareDescriptor = g_prg_rom[position];
            const std::uint8_t squareIndex = squareDescriptor & 0x3F;
            std::uint8_t tiles[4];
            if (squareIndex < 0x10) {
                const std::size_t secondaryOffset =
                    static_cast<std::size_t>(squareIndex) * 4;
                for (std::size_t i = 0; i < 4; ++i) {
                    tiles[i] = secondarySquares[secondaryOffset + i];
                }
            } else {
                if (squareIndex >= sizeof(primarySquares)) return false;
                const std::uint8_t first = primarySquares[squareIndex];
                tiles[0] = first;
                tiles[1] = static_cast<std::uint8_t>(first + 1);
                tiles[2] = static_cast<std::uint8_t>(first + 2);
                tiles[3] = static_cast<std::uint8_t>(first + 3);
            }

            const std::size_t x = column * 2;
            const std::size_t y = squareRow * 2;
            tileMap[y * 32 + x] = tiles[0];
            tileMap[(y + 1) * 32 + x] = tiles[1];
            tileMap[y * 32 + x + 1] = tiles[2];
            tileMap[(y + 1) * 32 + x + 1] = tiles[3];

            if ((squareDescriptor & 0x40) != 0) {
                repeatSquare = !repeatSquare;
                if (!repeatSquare) ++position;
            } else {
                ++position;
            }
        }
    }

    constexpr std::size_t mapWidth = 32 * 8;
    constexpr std::size_t mapHeight = 22 * 8;
    g_overworld_pixels.assign(mapWidth * mapHeight, 0xFF000000);
    for (std::size_t tileY = 0; tileY < 22; ++tileY) {
        for (std::size_t tileX = 0; tileX < 32; ++tileX) {
            const std::uint8_t tile = tileMap[tileY * 32 + tileX];
            const std::size_t atlasX = (tile % 16) * 8;
            const std::size_t atlasY = (tile / 16) * 8;
            for (std::size_t y = 0; y < 8; ++y) {
                for (std::size_t x = 0; x < 8; ++x) {
                    const std::size_t source = (atlasY + y) * 128 + atlasX + x;
                    const std::size_t target = (tileY * 8 + y) * mapWidth +
                                               tileX * 8 + x;
                    g_overworld_pixels[target] = g_background_pixels[source];
                }
            }
        }
    }
    return true;
}

std::uint64_t nes2RomSize(std::uint8_t low, std::uint8_t highNibble,
                          std::uint64_t unit) {
    if (highNibble != 0x0F) {
        return (static_cast<std::uint64_t>((highNibble << 8) | low)) * unit;
    }
    const unsigned exponent = low >> 2;
    const unsigned multiplier = low & 0x03;
    if (exponent >= 63) return 0;
    return (std::uint64_t{1} << exponent) * (2 * multiplier + 1);
}

bool parseRom(const std::vector<std::uint8_t>& bytes, ParsedRom& parsed,
              std::string& error) {
    if (bytes.size() < 16) {
        error = "ROM inválida: el archivo es menor que la cabecera iNES (16 bytes).";
        return false;
    }
    if (bytes[0] != 'N' || bytes[1] != 'E' || bytes[2] != 'S' || bytes[3] != 0x1A) {
        error = "Archivo no reconocido: falta la firma iNES NES\\x1A.";
        return false;
    }

    const std::uint8_t flags6 = bytes[6];
    const std::uint8_t flags7 = bytes[7];
    parsed.nes2 = (flags7 & 0x0C) == 0x08;
    const bool trainerPresent = (flags6 & 0x04) != 0;
    parsed.mapper = (flags6 >> 4) | (flags7 & 0xF0) |
        (parsed.nes2 ? ((bytes[8] & 0x0F) << 8) : 0);
    parsed.submapper = parsed.nes2 ? (bytes[8] >> 4) : 0;

    std::uint64_t prgSize = 0;
    std::uint64_t chrSize = 0;
    if (parsed.nes2) {
        prgSize = nes2RomSize(bytes[4], bytes[9] & 0x0F, 16 * 1024);
        chrSize = nes2RomSize(bytes[5], bytes[9] >> 4, 8 * 1024);
    } else {
        prgSize = static_cast<std::uint64_t>(bytes[4]) * 16 * 1024;
        chrSize = static_cast<std::uint64_t>(bytes[5]) * 8 * 1024;
    }

    if (prgSize == 0 || prgSize > std::numeric_limits<std::size_t>::max() ||
        chrSize > std::numeric_limits<std::size_t>::max()) {
        error = "Cabecera inválida: tamaño PRG/CHR no válido o no compatible.";
        return false;
    }

    const std::uint64_t trainerSize = trainerPresent ? 512 : 0;
    const std::uint64_t payloadOffset = 16 + trainerSize;
    if (payloadOffset > bytes.size() || prgSize > bytes.size() - payloadOffset ||
        chrSize > bytes.size() - payloadOffset - prgSize) {
        std::ostringstream message;
        message << "ROM incompleta: la cabecera declara PRG=" << prgSize
                << " bytes y CHR=" << chrSize << " bytes; el archivo contiene "
                << bytes.size() << " bytes.";
        error = message.str();
        return false;
    }

    const std::size_t trainerOffset = 16;
    const std::size_t prgOffset = static_cast<std::size_t>(payloadOffset);
    const std::size_t chrOffset = prgOffset + static_cast<std::size_t>(prgSize);

    if (trainerPresent) {
        parsed.trainer.assign(bytes.begin() + trainerOffset,
                              bytes.begin() + prgOffset);
    }
    parsed.prg.assign(bytes.begin() + prgOffset, bytes.begin() + chrOffset);

    parsed.chrRam = chrSize == 0;
    if (parsed.chrRam) {
        // Cartuchos sin CHR-ROM usan memoria CHR-RAM. Zelda MMC1 normalmente
        // carga sus patrones gráficos desde PRG-ROM hacia esta memoria.
        parsed.chr.assign(8 * 1024, 0);
    } else {
        parsed.chr.assign(bytes.begin() + chrOffset,
                          bytes.begin() + chrOffset + static_cast<std::size_t>(chrSize));
    }
    return true;
}

std::string loadRom(const std::vector<std::uint8_t>& bytes) {
    ParsedRom parsed;
    std::string error;
    if (!parseRom(bytes, parsed, error)) return error;

    // Publicamos los nuevos bancos solo después de validar y extraer todo.
    g_prg_rom = std::move(parsed.prg);
    g_chr_data = std::move(parsed.chr);
    g_trainer = std::move(parsed.trainer);
    g_mapper = parsed.mapper;
    g_submapper = parsed.submapper;
    g_chr_is_ram = parsed.chrRam;
    g_link_pixels.clear();
    g_background_pixels.clear();
    g_background_brown_pixels.clear();
    g_overworld_pixels.clear();
    g_octorok_pixels.clear();
    g_octorok_demo_pixels.clear();
    g_octorok_demo_active = false;
    g_octorok_demo_tick = 0;
    g_overworld_start_room = 0xFF;
    g_overworld_unique_room = 0xFF;
    const bool linkFound = extractLinkSprite();
    const bool backgroundFound = extractBackgroundTiles();
    const bool overworldFound = backgroundFound && extractStartingOverworldRoom();
    const bool octorokFound = extractOverworldOctorok();

    std::ostringstream report;
    report << "ROM cargada y separada en memoria nativa.\\n"
           << "Formato: " << (parsed.nes2 ? "NES 2.0" : "iNES 1.0") << "\\n"
           << "Mapper: " << g_mapper;
    if (parsed.nes2) report << " | Submapper: " << g_submapper;
    report << "\\nPRG-ROM: " << g_prg_rom.size() / 1024 << " KiB"
           << "\\nCHR-" << (g_chr_is_ram ? "RAM" : "ROM") << ": "
           << g_chr_data.size() / 1024 << " KiB"
           << "\\nTrainer: " << (g_trainer.empty() ? "no" : "512 bytes")
           << "\\nLink: " << (linkFound ? "metasprite 2x2 decodificado con la paleta original de Link." : "no se pudo localizar el bloque gráfico.")
           << "\\nAmbiente: " << (backgroundFound ? "patrones comunes y 130 patrones del mundo exterior decodificados; atlas con paletas verde y marrón originales." : "no se localizó el bloque de patrones de fondo.")
           << "\\nOverworld: " << (overworldFound ? "pantalla inicial reconstruida desde las tablas originales (32x22 tiles, sala $" : "no se pudo reconstruir la sala inicial.");
    if (overworldFound) {
        report << std::hex << static_cast<unsigned>(g_overworld_start_room)
               << ", layout $" << static_cast<unsigned>(g_overworld_unique_room) << ").";
    }
    report
           << "\\nOctorok: " << (octorokFound ? "seis cuadros extraídos con la paleta original de enemigos rojos." : "no se pudo localizar el bloque de patrones.")
           << "\\nEstado: PRG y CHR están en buffers independientes."
           << "\\nEsta etapa muestra la sala inicial reconstruida; movimiento, colisiones y lógica de juego aún no están implementados.";
    return report.str();
}

}  // namespace


extern "C" JNIEXPORT jboolean JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeStartOctorokDemo(JNIEnv*, jobject) {
    if (g_overworld_pixels.size() != 256 * 176 ||
        g_octorok_pixels.size() != 6 * 256) return JNI_FALSE;
    g_octorok_demo_x = 24.0f;
    g_octorok_demo_y = 80.0f;
    g_octorok_demo_velocity = 0.5f;
    g_octorok_demo_tick = 0;
    g_octorok_demo_active = renderOctorokDemoFrame();
    return g_octorok_demo_active ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeAdvanceOctorokDemo(JNIEnv* env, jobject) {
    if (!g_octorok_demo_active) return nullptr;
    g_octorok_demo_x += g_octorok_demo_velocity;
    if (g_octorok_demo_x > 224.0f) {
        g_octorok_demo_x = 224.0f;
        g_octorok_demo_velocity = -0.5f;
    } else if (g_octorok_demo_x < 16.0f) {
        g_octorok_demo_x = 16.0f;
        g_octorok_demo_velocity = 0.5f;
    }
    ++g_octorok_demo_tick;
    if (!renderOctorokDemoFrame()) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_octorok_demo_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_octorok_demo_pixels.size()),
                           reinterpret_cast<const jint*>(g_octorok_demo_pixels.data()));
    return result;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeLinkPixels(JNIEnv* env, jobject) {
    if (g_link_pixels.size() != 16 * 16) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_link_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_link_pixels.size()),
                           reinterpret_cast<const jint*>(g_link_pixels.data()));
    return result;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeOctorokPixels(JNIEnv* env, jobject) {
    if (g_octorok_pixels.size() != 6 * 16 * 16) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_octorok_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_octorok_pixels.size()),
                           reinterpret_cast<const jint*>(g_octorok_pixels.data()));
    return result;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeBackgroundPixels(JNIEnv* env, jobject) {
    if (g_background_pixels.size() != 128 * 128) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_background_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_background_pixels.size()),
                           reinterpret_cast<const jint*>(g_background_pixels.data()));
    return result;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeOverworldPixels(JNIEnv* env, jobject) {
    if (g_overworld_pixels.size() != 256 * 176) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_overworld_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_overworld_pixels.size()),
                           reinterpret_cast<const jint*>(g_overworld_pixels.data()));
    return result;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeBackgroundBrownPixels(JNIEnv* env, jobject) {
    if (g_background_brown_pixels.size() != 128 * 128) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_background_brown_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_background_brown_pixels.size()),
                           reinterpret_cast<const jint*>(g_background_brown_pixels.data()));
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeStatus(JNIEnv* env, jobject) {
    constexpr char status[] =
        "Motor nativo C++20 iniciado.\\n"
        "Selecciona un archivo .nes para separar PRG y CHR en memoria nativa.";
    return env->NewStringUTF(status);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeLoadRom(
    JNIEnv* env, jobject, jbyteArray input) {
    if (input == nullptr) return env->NewStringUTF("No se recibió ningún archivo.");
    const jsize length = env->GetArrayLength(input);
    if (length <= 0) return env->NewStringUTF("El archivo está vacío.");

    std::vector<std::uint8_t> candidate(static_cast<std::size_t>(length));
    env->GetByteArrayRegion(input, 0, length, reinterpret_cast<jbyte*>(candidate.data()));
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return env->NewStringUTF("No se pudieron leer los bytes del archivo.");
    }

    const std::string result = loadRom(candidate);
    return env->NewStringUTF(result.c_str());
}
