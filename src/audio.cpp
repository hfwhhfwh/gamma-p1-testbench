/*
 * audio.cpp — RtAudio 回调实现
 *
 * 架构注释：RtAudio 回调 = 未来 STM32 的音频 ISR。
 * 本回调内只调用 synth_render_block，禁止 malloc/锁/打印。
 *
 * 变更摘要 (TBENCH-WIN-1, 2026-11-17):
 *   WP2a: RtAudio API 显式选择收进 #ifdef _WIN32 —— Windows 用
 *   WINDOWS_WASAPI (RtAudio 侧编译定义 __WINDOWS_WASAPI__), Mac 保留
 *   MACOSX_CORE 逐字不变。回调与流参数逻辑零改动。
 */

#include "audio.h"
#include "RtAudio.h"

#include <cmath>
#include <algorithm>
#include <chrono>

namespace gp1_ui {

static inline float fast_tanh(float x)
{
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static RtAudio *g_rtaudio = nullptr;

static int audio_callback(void *outputBuffer, void * /*inputBuffer*/,
                          unsigned int nFrames, double /*streamTime*/,
                          RtAudioStreamStatus /*status*/, void *userData)
{
    AppState *app = static_cast<AppState *>(userData);
    if (!app)
        return 0;

    auto t0 = std::chrono::high_resolution_clock::now();

    /* 单声道 -> 双声道（macOS CoreAudio 一般要求立体声） */
    float mono[KCAPI_MAX_BLOCK];
    int n = (nFrames > KCAPI_MAX_BLOCK) ? KCAPI_MAX_BLOCK : (int)nFrames;

    kcapi_render_block(app->synth, mono, n);

    float *out = static_cast<float *>(outputBuffer);
    float peak = 0.0f;
    int write_pos = app->scope_write_pos.load(std::memory_order_relaxed);

    for (int i = 0; i < (int)nFrames; i++) {
        float s = (i < n) ? mono[i] : 0.0f;
        s *= app->master_level;
        if (app->soft_clip_master && std::fabs(s) > 0.5f)
            s = fast_tanh(s);
        if (s > 1.5f) s = 1.5f;
        if (s < -1.5f) s = -1.5f;

        out[i * 2 + 0] = s;
        out[i * 2 + 1] = s;
        float a = std::fabs(s);
        if (a > peak)
            peak = a;

        int idx = (write_pos + i) & (SCOPE_SIZE - 1);
        app->scope_buffer[idx].store(s, std::memory_order_relaxed);
    }
    app->scope_write_pos.store((write_pos + (int)nFrames) & (SCOPE_SIZE - 1),
                               std::memory_order_relaxed);

    app->peak_output.store(peak, std::memory_order_relaxed);

    auto t1 = std::chrono::high_resolution_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    double block_us = (double)nFrames / KCAPI_SAMPLE_RATE * 1e6;
    float pct = (float)(us / block_us * 100.0);
    if (pct > 999.0f) pct = 999.0f;
    app->cpu_load_percent.store(pct, std::memory_order_relaxed);

    return 0;
}

bool audio_start(AppState *app)
{
    if (!app || g_rtaudio)
        return false;

    /* TBENCH-WIN-1 WP2a: Windows 默认 WASAPI (RtAudio 编译期需
     * __WINDOWS_WASAPI__, 见 CMakeLists); Mac 路径原样 */
#ifdef _WIN32
    g_rtaudio = new RtAudio(RtAudio::WINDOWS_WASAPI);
#else
    g_rtaudio = new RtAudio(RtAudio::MACOSX_CORE);
#endif
    if (g_rtaudio->getDeviceCount() < 1) {
        delete g_rtaudio;
        g_rtaudio = nullptr;
        return false;
    }

    RtAudio::StreamParameters params;
    params.deviceId = g_rtaudio->getDefaultOutputDevice();
    params.nChannels = 2;
    params.firstChannel = 0;

    unsigned int bufferFrames = KCAPI_MAX_BLOCK;
    RtAudioErrorType err = g_rtaudio->openStream(&params, nullptr, RTAUDIO_FLOAT32,
                                                 (unsigned int)KCAPI_SAMPLE_RATE, &bufferFrames,
                                                 audio_callback, app);
    if (err != RTAUDIO_NO_ERROR) {
        delete g_rtaudio;
        g_rtaudio = nullptr;
        return false;
    }
    err = g_rtaudio->startStream();
    if (err != RTAUDIO_NO_ERROR) {
        delete g_rtaudio;
        g_rtaudio = nullptr;
        return false;
    }

    return true;
}

void audio_stop(AppState *app)
{
    (void)app;
    if (!g_rtaudio)
        return;
    if (g_rtaudio->isStreamRunning())
        g_rtaudio->stopStream();
    if (g_rtaudio->isStreamOpen())
        g_rtaudio->closeStream();
    delete g_rtaudio;
    g_rtaudio = nullptr;
}

} /* namespace gp1_ui */
