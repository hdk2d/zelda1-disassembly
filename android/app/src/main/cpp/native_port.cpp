#include <jni.h>

#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::uint8_t> g_rom;

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

std::string inspectRom(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 16) return "ROM inválida: el archivo es menor que la cabecera iNES (16 bytes).";
    if (bytes[0] != 'N' || bytes[1] != 'E' || bytes[2] != 'S' || bytes[3] != 0x1A) {
        return "Archivo no reconocido: falta la firma iNES «NES\x1A».";
    }

    const std::uint8_t flags6 = bytes[6];
    const std::uint8_t flags7 = bytes[7];
    const bool nes2 = (flags7 & 0x0C) == 0x08;
    const bool trainer = (flags6 & 0x04) != 0;
    const std::uint32_t mapper = (flags6 >> 4) | (flags7 & 0xF0) |
        (nes2 ? ((bytes[8] & 0x0F) << 8) : 0);
    const std::uint32_t submapper = nes2 ? (bytes[8] >> 4) : 0;

    std::uint64_t prgSize = 0;
    std::uint64_t chrSize = 0;
    if (nes2) {
        prgSize = nes2RomSize(bytes[4], bytes[9] & 0x0F, 16 * 1024);
        chrSize = nes2RomSize(bytes[5], bytes[9] >> 4, 8 * 1024);
    } else {
        prgSize = static_cast<std::uint64_t>(bytes[4]) * 16 * 1024;
        chrSize = static_cast<std::uint64_t>(bytes[5]) * 8 * 1024;
    }

    if (prgSize == 0 || prgSize > std::numeric_limits<std::size_t>::max() ||
        chrSize > std::numeric_limits<std::size_t>::max()) {
        return "Cabecera inválida: tamaño PRG/CHR no válido o no compatible.";
    }

    const std::uint64_t trainerSize = trainer ? 512 : 0;
    const std::uint64_t payloadOffset = 16 + trainerSize;
    if (payloadOffset > bytes.size() || prgSize > bytes.size() - payloadOffset ||
        chrSize > bytes.size() - payloadOffset - prgSize) {
        std::ostringstream error;
        error << "ROM incompleta: la cabecera declara PRG=" << prgSize
              << " bytes y CHR=" << chrSize << " bytes, pero el archivo solo contiene "
              << bytes.size() << " bytes.";
        return error.str();
    }

    const std::uint64_t expected = payloadOffset + prgSize + chrSize;
    std::ostringstream report;
    report << "ROM cargada y validada.\n"
           << "Formato: " << (nes2 ? "NES 2.0" : "iNES 1.0") << "\n"
           << "Mapper: " << mapper;
    if (nes2) report << " | Submapper: " << submapper;
    report << "\nPRG-ROM: " << prgSize / 1024 << " KiB"
           << "\nCHR-ROM: " << chrSize / 1024 << " KiB"
           << "\nTrainer: " << (trainer ? "sí" : "no")
           << "\nDatos adicionales: " << (bytes.size() > expected ? bytes.size() - expected : 0)
           << " bytes\n\nLos datos se leyeron en memoria nativa. "
           << "Todavía no se ejecuta el juego: faltan el motor de juego y el renderizador.";
    return report.str();
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeStatus(JNIEnv* env, jobject) {
    constexpr char status[] =
        "Motor nativo C++20 iniciado.\n"
        "Selecciona un archivo .nes para validar su cabecera y cargar sus datos.";
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
    if (env->ExceptionCheck()) return env->NewStringUTF("No se pudieron leer los bytes del archivo.");

    const std::string result = inspectRom(candidate);
    if (result.rfind("ROM cargada y validada.", 0) == 0) {
        g_rom = std::move(candidate);
    }
    return env->NewStringUTF(result.c_str());
}
