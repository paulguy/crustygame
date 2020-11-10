/*
 * Copyright 2020 paulguy <paulguy119@gmail.com>
 *
 * This file is part of crustygame.
 *
 * crustygame is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * crustygame is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with crustygame.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef _CRUSTYGAME_H
#define _CRUSTYGAME_H

#include <SDL.h>
#include "crustyvm.h"
#include "tilemap.h"
#include "synth.h"

#define CRUSTYGAME_KEYDOWN              (1)
#define CRUSTYGAME_KEYUP                (2)
#define CRUSTYGAME_MOUSEMOTION          (3)
#define CRUSTYGAME_MOUSEBUTTONDOWN      (4)
#define CRUSTYGAME_MOUSEBUTTONUP        (5)
#define CRUSTYGAME_MOUSEWHEEL           (6)
#define CRUSTYGAME_JOYAXISMOTION        (7)
#define CRUSTYGAME_JOYBALLMOTION        (8)
#define CRUSTYGAME_JOYHATMOTION         (9)
#define CRUSTYGAME_JOYBUTTONDOWN        (10)
#define CRUSTYGAME_JOYBUTTONUP          (11)
#define CRUSTYGAME_CONTROLLERAXISMOTION (12)
#define CRUSTYGAME_CONTROLLERBUTTONDOWN (13)
#define CRUSTYGAME_CONTROLLERBUTTONUP   (14)
 
#define CRUSTYGAME_CONTROLLER_AXIS_LEFTX        (0)
#define CRUSTYGAME_CONTROLLER_AXIS_LEFTY        (1)
#define CRUSTYGAME_CONTROLLER_AXIS_RIGHTX       (2)
#define CRUSTYGAME_CONTROLLER_AXIS_RIGHTY       (3)
#define CRUSTYGAME_CONTROLLER_AXIS_TRIGGERLEFT  (4)
#define CRUSTYGAME_CONTROLLER_AXIS_TRIGGERRIGHT (5)

#define CRUSTYGAME_CONTROLLER_BUTTON_A              (0)
#define CRUSTYGAME_CONTROLLER_BUTTON_B              (1)
#define CRUSTYGAME_CONTROLLER_BUTTON_X              (2)
#define CRUSTYGAME_CONTROLLER_BUTTON_Y              (3)
#define CRUSTYGAME_CONTROLLER_BUTTON_BACK           (4)
#define CRUSTYGAME_CONTROLLER_BUTTON_GUIDE          (5)
#define CRUSTYGAME_CONTROLLER_BUTTON_START          (6)
#define CRUSTYGAME_CONTROLLER_BUTTON_LEFTSTICK      (7)
#define CRUSTYGAME_CONTROLLER_BUTTON_RIGHTSTICK     (8)
#define CRUSTYGAME_CONTROLLER_BUTTON_LEFTSHOULDER   (9)
#define CRUSTYGAME_CONTROLLER_BUTTON_RIGHTSHOULDER  (10)
#define CRUSTYGAME_CONTROLLER_BUTTON_DPAD_UP        (11)
#define CRUSTYGAME_CONTROLLER_BUTTON_DPAD_DOWN      (12)
#define CRUSTYGAME_CONTROLLER_BUTTON_DPAD_LEFT      (13)
#define CRUSTYGAME_CONTROLLER_BUTTON_DPAD_RIGHT     (14)

#define CRUSTYGAME_BLENDMODE_BLEND (0x01)
#define CRUSTYGAME_BLENDMODE_ADD   (0x02)
#define CRUSTYGAME_BLENDMODE_MOD   (0x04)
#define CRUSTYGAME_BLENDMODE_MUL   (0x08)

#define CRUSTYGAME_AUDIO_TYPE_U8  (0)
#define CRUSTYGAME_AUDIO_TYPE_S16 (1)
#define CRUSTYGAME_AUDIO_TYPE_F32 (2)
#define CRUSTYGAME_AUDIO_TYPE_F64 (3)

#define CRUSTYGAME_AUDIO_MODE_ONCE         (0)
#define CRUSTYGAME_AUDIO_MODE_LOOP         (1)
#define CRUSTYGAME_AUDIO_MODE_PINGPONG     (2)
#define CRUSTYGAME_AUDIO_MODE_PHASE_SOURCE (3)

#define CRUSTYGAME_AUDIO_VOLUME_MODE_CONSTANT (0)
#define CRUSTYGAME_AUDIO_VOLUME_MODE_SOURCE   (1)

#define CRUSTYGAME_AUDIO_SPEED_MODE_CONSTANT (0)
#define CRUSTYGAME_AUDIO_SPEED_MODE_SOURCE   (1)

#define CRUSTYGAME_AUDIO_OUTPUT_MODE_REPLACE (0)
#define CRUSTYGAME_AUDIO_OUTPUT_MODE_ADD     (1)

typedef struct {
    CrustyVM *cvm;
    SDL_Window *win;
    SDL_Renderer *renderer;
    SDL_Event lastEvent;
    LayerList *ll;
    Synth *s;
    int running;

    void *buffer;
    unsigned int size;
    int ret;

    int mouseCaptured;
    int mouseReleaseCombo;

    unsigned int savesize;
    FILE *savefile;
} CrustyGame;

extern CrustyGame state;
#endif
