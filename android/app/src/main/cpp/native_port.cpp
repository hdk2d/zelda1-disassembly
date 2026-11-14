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
    const bool linkFound = extractLinkSprite();

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
           << "\\nEstado: PRG y CHR están en buffers independientes."
           << "\\nTodavía no se dibuja el juego; esta etapa prepara los datos para el decodificador gráfico.";
    return report.str();
}

}  // namespace


extern "C" JNIEXPORT jintArray JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeLinkPixels(JNIEnv* env, jobject) {
    if (g_link_pixels.size() != 16 * 16) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(g_link_pixels.size()));
    if (result == nullptr) return nullptr;
    env->SetIntArrayRegion(result, 0, static_cast<jsize>(g_link_pixels.size()),
                           reinterpret_cast<const jint*>(g_link_pixels.data()));
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
