#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include<iostream>

extern "C" {
// FFmpeg 是 C 语言库
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavcodec/jni.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#define LOG_TAG "AndroidFFmpeg"
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using namespace std;

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
JNI_OnLoad(JavaVM *vm, void *res) {
    // 硬解码时需要
    av_jni_set_java_vm(vm, 0);
    return JNI_VERSION_1_4;
}

static SLObjectItf engineSL = NULL;

SLEngineItf CreateSL() {
    SLresult re;
    SLEngineItf en;
    re = slCreateEngine(&engineSL, 0, 0, 0, 0, 0);
    if (re != SL_RESULT_SUCCESS) {
        return NULL;
    }
    // 实例化
    re = (*engineSL)->Realize(engineSL, SL_BOOLEAN_FALSE);
    if (re != SL_RESULT_SUCCESS) {
        return NULL;
    }
    re = (*engineSL)->GetInterface(engineSL, SL_IID_ENGINE, &en);
    if (re != SL_RESULT_SUCCESS) {
        return NULL;
    }
    return en;
}

void PcmCall(SLAndroidSimpleBufferQueueItf bf, void *context) {
    LOGW("PcmCall");
    static FILE *fp = NULL;
    static char *buf = NULL;
    if (!buf) {
        buf = new char[1024 * 1024];
    }
    if (!fp) {
        fp = fopen("/sdcard/test.pcm", "rb");
    }
    if (!fp) {
        LOGW("fopen failed");
        return;
    }
    if (feof(fp) == 0) {
        LOGW("feof 0");
        int len = fread(buf, 1, 1024, fp);
        LOGW("feof len = %d", len);
        if (len > 0) {
            (*bf)->Enqueue(bf, buf, len);
        }
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_wang_androidffmpeg_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";

    hello += avcodec_configuration();

    // 创建引擎
    SLEngineItf eng = CreateSL();
    if (eng) {
        LOGW("CreateSL success!");
    } else {
        LOGW("CreateSL failed!");
    }

    // 创建混音器
    SLObjectItf mix = NULL;
    SLresult re = 0;
    re = (*eng)->CreateOutputMix(eng, &mix, 0, 0, 0);
    if (re != SL_RESULT_SUCCESS) {
        LOGW("CreateOutputMix failed!");
    }
    re = (*mix)->Realize(mix, SL_BOOLEAN_FALSE); // 阻塞式等待
    if (re != SL_RESULT_SUCCESS) {
        LOGW("mix Realize failed!");
    }
    SLDataLocator_OutputMix outmix = {SL_DATALOCATOR_OUTPUTMIX, mix};
    SLDataSink audioSink = {&outmix, 0};

    // 配置音频信息
    SLDataLocator_AndroidSimpleBufferQueue que = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 10};
    // 音频格式
    SLDataFormat_PCM pcm = {
            SL_DATAFORMAT_PCM,
            2, // 声道数
            SL_SAMPLINGRATE_44_1,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_PCMSAMPLEFORMAT_FIXED_16,
            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT,
            SL_BYTEORDER_LITTLEENDIAN}; // 字节序，小端

    SLDataSource ds = {&que, &pcm};
    // 创建播放器
    SLObjectItf player = NULL;
    SLPlayItf iplayer = NULL;
    SLAndroidSimpleBufferQueueItf pcmQue = NULL;
    const SLInterfaceID ids[] = {SL_IID_BUFFERQUEUE};
    const SLboolean req[] = {SL_BOOLEAN_TRUE};
    re = (*eng)->CreateAudioPlayer(eng, &player, &ds, &audioSink, sizeof(ids)/sizeof(SLInterfaceID), ids, req);
    if (re != SL_RESULT_SUCCESS) {
        LOGW("CreateAudioPlayer failed!");
    } else {
        LOGW("CreateAudioPlayer success!");
    }
    (*player)->Realize(player, SL_BOOLEAN_FALSE);
    // 获取player接口
    re = (*player)->GetInterface(player, SL_IID_PLAY, &iplayer);
    if (re != SL_RESULT_SUCCESS) {
        LOGW("GetInterface SL_IID_PLAY failed");
    }
    // 获取队列
    re = (*player)->GetInterface(player, SL_IID_BUFFERQUEUE, &pcmQue);
    if (re != SL_RESULT_SUCCESS) {
        LOGW("GetInterface SL_IID_BUFFERQUEUE failed");
    }

    // 设置回调函数，播放队列调用
    (*pcmQue)->RegisterCallback(pcmQue, PcmCall, 0);

    (*iplayer)->SetPlayState(iplayer, SL_PLAYSTATE_PLAYING);

    // 启动队列回调
    (*pcmQue)->Enqueue(pcmQue, "", 1);
    return env->NewStringUTF(hello.c_str());
}

extern "C"
JNIEXPORT void JNICALL
Java_com_wang_androidffmpeg_XPlay_open(JNIEnv *env, jobject thiz, jstring url, jobject surface) {

    const char *path = env->GetStringUTFChars(url, 0);

    // 初始化解封装库
    av_register_all();
    // 初始化网络
    avformat_network_init();

    // 初始化编解码器
    avcodec_register_all();

    // 打开文件
    AVFormatContext *ic = NULL;
    int re = avformat_open_input(&ic, path, 0, 0);
    if (re != 0) {
        LOGW("avformat_open_input %s failed! reason: %s", path, av_err2str(re));
        return;
    }
    LOGW("avformat_open_input %s success!", path);

    // 获取流信息（有的格式，无头部信息，或头部格式不寻常，可以探测）
    re = avformat_find_stream_info(ic, 0);
    if (re != 0) {
        LOGW("avformat_find_stream_info %s failed! %s", path, av_err2str(re));
    }
    LOGW("duration = %lld; nb_streams = %d", ic->duration, ic->nb_streams);

    int fps = 0;
    int videoStream = 0;
    int audioStream = 1;
    for (int i = 0; i < ic->nb_streams; i++) {
        AVStream *as = ic->streams[i];
        if (as->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            LOGW("视频数据");
            videoStream = i;
            fps = r2d(as->avg_frame_rate);
            LOGW("fps = %d, width = %d, height = %d, codec_id = %d, pixel_format = %d", fps,
                 as->codecpar->width,
                 as->codecpar->height,
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

    // 获取音频流的索引，方法二
    audioStream = av_find_best_stream(ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    LOGW("av_find_best_stream audioStream = %d", audioStream);

    //////////////////////////////////
    // 打开视频解码器
    // 软解码器
    AVCodec *codec = avcodec_find_decoder(ic->streams[videoStream]->codecpar->codec_id);
    // 硬解码：配合 Jni_OnLoad 中的代码使用
    codec = avcodec_find_decoder_by_name("h264_mediacodec");
    if (!codec) {
        LOGW("video decoder not found!");
        return;
    }
    // 解码器初始化
    AVCodecContext *vc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(vc, ic->streams[videoStream]->codecpar);
    vc->thread_count = 2; // 单线程解码
    // 打开解码器
    re = avcodec_open2(vc, 0, 0);
    LOGW("vc timebase = %d/ %d", vc->time_base.num, vc->time_base.den);
    if (re != 0) { // 不能是 if (!re)
        LOGW("video avcodec_open2 failed!");
        return;
    }

    //////////////////////////////////
    // 打开音频解码器
    AVCodec *acodec = avcodec_find_decoder(ic->streams[audioStream]->codecpar->codec_id);
    if (!acodec) {
        LOGW("audio decoder not found!");
        return;
    }
    // 解码器初始化
    AVCodecContext *ac = avcodec_alloc_context3(acodec);
    avcodec_parameters_to_context(ac, ic->streams[audioStream]->codecpar);
    ac->thread_count = 2; // 单线程解码
    // 打开解码器
    re = avcodec_open2(ac, 0, 0);
    if (re != 0) { // 不能是 if (!re)
        LOGW("audio avcodec_open2 failed!");
        return;
    }

    // 读取帧数据
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    long long start = getNowMs();
    int frameCount = 0;

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
                              AV_SAMPLE_FMT_S16, ac->sample_rate,
                              av_get_default_channel_layout(ac->channels),
                              ac->sample_fmt, ac->sample_rate,
                              0, 0);
    re = swr_init(actx);
    if (re != 0) {
        LOGW("swr_init failed!");
    }

    // 显示窗口初始化
    ANativeWindow *nwin = ANativeWindow_fromSurface(env, surface);
    ANativeWindow_setBuffersGeometry(nwin, outWidth, outHeight, WINDOW_FORMAT_RGBA_8888);
    ANativeWindow_Buffer wbuf;

    for (;;) {
        //超过三秒
        if (getNowMs() - start >= 3000) {
            LOGW("now decode fps is %d", frameCount / 3);
            start = getNowMs();
            frameCount = 0;
        }
        int re = av_read_frame(ic, pkt);
        if (re != 0) {
            LOGW("读取到结尾处！");
            break;
        }

        AVCodecContext *cc = vc;
        if (pkt->stream_index == audioStream) {
            cc = ac;
        }
        // 解码
        re = avcodec_send_packet(cc, pkt);

        //发送到线程中解码
        av_packet_unref(pkt);

        if (re != 0) {
            LOGW("avcodec_send_packet failed!");
            continue;
        }

        for (;;) {
            re = avcodec_receive_frame(cc, frame); // 问题：最后的几帧，还在缓冲区中
            if (re != 0) {
//                    LOGW("avcodec_receive_frame failed!");
                break;
            }
            //LOGW("avcodec_receive_frame %lld", frame->pts);
            if (cc == vc) { // 如果是视频帧
                frameCount++;
                vctx = sws_getCachedContext(vctx,
                                            frame->width,
                                            frame->height,
                                            (AVPixelFormat) frame->format,
                                            outWidth,
                                            outHeight,
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
                    int h = sws_scale(vctx,
                                      (const uint8_t **) frame->data,
                                      frame->linesize, 0,
                                      frame->height,
                                      data, lines);
                    LOGW("sws_scale = %d", h);
                    if (h > 0) {
                        ANativeWindow_lock(nwin, &wbuf, 0);
                        uint8_t *dst = (uint8_t *) wbuf.bits;
                        memcpy(dst, rgb, outWidth * outHeight * 4); // 4 for rgba，4个字节
                        ANativeWindow_unlockAndPost(nwin);
                    }
                }
            } else { // 如果是音频帧
                uint8_t *out[2] = {0};
                out[0] = (uint8_t *) pcm;
                // 音频重采样
                int len = swr_convert(actx, out,
                                      frame->nb_samples,
                                      (const uint8_t **) frame->data,
                                      frame->nb_samples);
                LOGW("swr_convert = %d", len);
            }
        }
    }

    delete rgb;
    delete pcm;

    // 关闭上下文
    avformat_close_input(&ic);
    env->ReleaseStringUTFChars(url, path);
}