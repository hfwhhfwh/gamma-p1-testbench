/*
 * audio.h — RtAudio 实时音频回调 + 无锁示波器 ring buffer
 */

#ifndef AUDIO_H
#define AUDIO_H

#include "synth_ui.h"

namespace gp1_ui {

/* 打开/关闭默认 CoreAudio 输出，固定 48kHz/64 帧 */
bool audio_start(AppState *app);
void audio_stop(AppState *app);

} /* namespace gp1_ui */

#endif /* AUDIO_H */
