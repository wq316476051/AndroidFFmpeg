#include <jni.h>
#include <string>
#include <android/log.h>
#define LOG_TAG "AndroidFFmpeg"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
extern "C" {
// FFmpeg 是 C 语言库
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavcodec/jni.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

static double r2d(AVRational r) {
    return r.num == 0 || r.den == 0 ? 0. : (double) r.num / (double) r.den;
}

// 获取当前时间戳
long long getNowMs() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    int sec = tv.tv_sec % 360000; // 只要100小时内，避免
    long long t = sec * 1000 + tv.tv_usec / 1000;
    return t;
}

extern "C" JNIIMPORT jint JNICALL
JNI_OnLoad(JavaVM *vm, void * res) {
    // 硬解码时需要
    av_jni_set_java_vm(vm, 0);
    return JNI_VERSION_1_4;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_wang_androidffmpeg_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from C++";

    hello += avcodec_configuration();

    // 初始化解封装库
    av_register_all();
    // 初始化网络
    avformat_network_init();

    // 初始化编解码器
    avcodec_register_all();

    // 打开文件
    AVFormatContext *ic = nullptr;
    char path[] = "/storage/emulated/0/Movies/1080p.mp4";
    int re = avformat_open_input(&ic, path, nullptr, nullptr);
    if (re != 0) {
        LOGW("avformat_open_input %s failed! reason: %s", path, av_err2str(re));
        return env->NewStringUTF(hello.c_str());
    } else {
        LOGW("avformat_open_input %s success!", path);

        // 获取流信息（有的格式，无头部信息，或头部格式不寻常，可以探测）
        re = avformat_find_stream_info(ic, 0);
        if (re != 0) {
            LOGW("avformat_find_stream_info %s failed! %s", path, av_err2str(re));
        }
        LOGW("duration = %lld; nb_streams = %d", ic->duration, ic->nb_streams);

        int fps = 0;
        int width = 0;
        int height = 0;
        int videoStream = 0;
        int audioStream = 0;
        for (int i = 0; i < ic->nb_streams; i++) {
            AVStream *as = ic->streams[i];
            if (as->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
                LOGW("视频数据");
                videoStream = i;
                fps = r2d(as->avg_frame_rate);
                LOGW("fps = %d, width = %d, height = %d, codec_id = %d, pixel_format",
                        fps, as->codecpar->width, as->codecpar->height,
                        as->codecpar->codec_id,
                        as->codecpar->format);
            } else if (as->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                LOGW("音频数据");
                audioStream = i;
                LOGW("sample_rate = %d, channels = %d, sample_format = %d",
                        as->codecpar->sample_rate,
                        as->codecpar->channels,
                        as->codecpar->format); // 注意：平面、非平面。平面（planar），声道数据间隔排列
            }
        }

//        ic->streams[videoStream];

        // 获取音频流的索引，方法二
        audioStream = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
        LOGW("av_find_best_stream audioStream = %d", audioStream);

        //////////////////////////////////
        // 打开视频解码器
        // 软解码器
        AVCodec *vcodec = avcodec_find_decoder(ic->streams[videoStream]->codecpar->codec_id);
        // 硬解码：配合 Jni_OnLoad 中的代码使用
        vcodec = avcodec_find_decoder_by_name("h264_mediacodec");
        if (!vcodec) {
            LOGW("video decoder not found!");
            return env->NewStringUTF(hello.c_str());
        }
        // 解码器初始化
        AVCodecContext *vcc = avcodec_alloc_context3(vcodec);
        avcodec_parameters_to_context(vcc, ic->streams[videoStream]->codecpar);
        vcc->thread_count = 8; // 单线程解码
        // 打开解码器
        re = avcodec_open2(vcc, 0, 0);
        if (re != 0) { // 不能是 if (!re)
            LOGW("video avcodec_open2 failed!");
            return env->NewStringUTF(hello.c_str());
        }

        //////////////////////////////////
        // 打开音频解码器
        AVCodec *acodec = avcodec_find_decoder(ic->streams[audioStream]->codecpar->codec_id);
        if (!acodec) {
            LOGW("audio decoder not found!");
            return env->NewStringUTF(hello.c_str());
        }
        // 解码器初始化
        AVCodecContext *acc = avcodec_alloc_context3(acodec);
        avcodec_parameters_to_context(acc, ic->streams[audioStream]->codecpar);
        acc->thread_count = 8; // 单线程解码
        // 打开解码器
        re = avcodec_open2(acc, 0, 0);
        if (re != 0) { // 不能是 if (!re)
            LOGW("audio avcodec_open2 failed!");
            return env->NewStringUTF(hello.c_str());
        }


        // 读取帧数据
        AVPacket *pkt = av_packet_alloc();
        AVFrame *frame = av_frame_alloc();

        // 视频像素和尺寸转换上下文初始化
        SwsContext *vctx = NULL;
        int outWidth = 1280;
        int outHeight = 720;
        char *rgb = new char[1920 * 1080 * 4];

        char *pcm = new char[48000 * 4 * 2]; // 1秒音频，够大了

        // 音频重采样上下文初始化
        SwrContext *actx = swr_alloc();
        actx = swr_alloc_set_opts(actx,
                                  av_get_default_channel_layout(2),
                                  AV_SAMPLE_FMT_S16,
                                  acc->sample_rate,
                                  av_get_default_channel_layout(acc->channels),
                                  acc->sample_fmt,
                                  acc->sample_rate,0, 0);
        re = swr_init(actx);
        if (re != 0) {
            LOGW("swr_init failed!");
        }

        long long start = getNowMs();
        int frameCount = 0;
        for (;;) {
            if (getNowMs() - start >= 3000) { // 超过三秒
                LOGW("now decode fps is %d", frameCount / 3);
                start = getNowMs();
                frameCount = 0;
            }
            int re = av_read_frame(ic, pkt);
            if (re != 0) {
                LOGW("读取到结尾处！");
//                int pos = 10 * r2d(ic->streams[videoStream]->time_base);
//                av_seek_frame(ic, videoStream, pos, AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_FRAME); // 跳到
//                continue;
                break;
            }
//            LOGW("stream = %d, size = %d, pts = %lld, flag = %d",
//                    pkt->stream_index,
//                    pkt->size,
//                    pkt->pts,
//                    pkt->flags);
//
//            // 只测试视频
//            if (pkt->stream_index != videoStream) {
//                av_packet_unref(pkt);
//                continue;
//            }

            AVCodecContext *avcc = vcc;
            if (pkt->stream_index == audioStream) {
                avcc = acc;
            }
            // 解码
            re = avcodec_send_packet(avcc, pkt);

            // 释放空间
            av_packet_unref(pkt);

            if (re != 0) {
                LOGW("avcodec_send_packet failed!");
                continue;
            }

            for (;;) {
                re = avcodec_receive_frame(avcc, frame); // 问题：最后的几帧，还在缓冲区中
                if (re != 0) {
//                    LOGW("avcodec_receive_frame failed!");
                    break;
                }
                LOGW("avcodec_receive_frame %lld", frame->pts);
                if (avcc == vcc) { // 如果是视频帧
                    frameCount ++;
                    vctx = sws_getCachedContext(vctx, frame->width, frame->height,
                                                (AVPixelFormat) frame->format,
                                                outWidth, outHeight,
                                                AV_PIX_FMT_RGBA,
                                                SWS_FAST_BILINEAR,
                                                0, 0, 0);
                    if (!vctx) {
                        LOGW("sws_getCachedContext failed");
                    } else {
                        uint8_t *data[AV_NUM_DATA_POINTERS] = {0};
                        data[0] = (uint8_t *) rgb;
                        int lines[AV_NUM_DATA_POINTERS] = {0}; // 一行宽度的大小
                        lines[0] = outWidth * 4; // 4 for rgba
                        int h = sws_scale(vctx, (uint8_t **) frame->data,
                                frame->linesize,
                                0,
                                frame->height, data, lines);
                        LOGW("sws_scale = %d", h);
                    }
                } else { // 如果是音频帧
                    uint8_t *out[2] = {0};
                    out[0] = (uint8_t *) pcm;
                    // 音频重采样
                    int len = swr_convert(actx, out,
                                          frame->nb_samples,
                                          (const uint8_t **)frame->data,
                                          frame->nb_samples);
                    LOGW("swr_convert = %d", len);
                }
            }
        }

        delete[] rgb;
        // 关闭上下文
        avformat_close_input(&ic);
    }

    return env->NewStringUTF(hello.c_str());
}

extern "C"
JNIEXPORT jboolean JNICALL
Java_com_wang_androidffmpeg_MainActivity_open(JNIEnv *env, jobject thiz, jstring inputUrl,
                                              jobject handle) {
    const char *url = env->GetStringUTFChars(inputUrl, 0);

    FILE *fp = fopen(url, "rb");
    if (!fp) {
        LOGW("%s open failed", url);
    } else {
        LOGW("%s open succeed", url);
        fclose(fp);
    }
    env->ReleaseStringUTFChars(inputUrl, url);
    return true;
}