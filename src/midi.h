/*
 * midi.h — RtMidi 输入：note on/off、velocity、pitch bend、modwheel CC1
 */

#ifndef MIDI_H
#define MIDI_H

#include "synth_ui.h"

namespace gp1_ui {

bool midi_start(AppState *app);
void midi_stop(AppState *app);
void midi_poll(AppState *app); /* 每帧调用，读入新 MIDI 事件 */

} /* namespace gp1_ui */

#endif /* MIDI_H */
