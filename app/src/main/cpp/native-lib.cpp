#include <jni.h>
#include <string>
extern "C" {
// FFmpeg 是 C 语言库
#include <libavcodec/avcodec.h>
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_wang_androidffmpeg_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";

    hello += avcodec_configuration();
    return env->NewStringUTF(hello.c_str());
}