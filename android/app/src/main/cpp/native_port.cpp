#include <jni.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_hdk2d_zeldanative_MainActivity_nativeStatus(JNIEnv* env, jobject /* thiz */) {
    constexpr char kStatus[] =
        "Motor C++20 cargado correctamente.\n"
        "Siguiente etapa: implementar los sistemas del juego.";
    return env->NewStringUTF(kStatus);
}
