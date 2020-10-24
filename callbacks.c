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

#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <SDL.h>

#include "crustygame.h"
#include "crustyvm.h"
#include "tilemap.h"

#define VIDEO_MODE_STR_SIZE (256)
#define VIDEO_MODE_SEPARATOR 'x'

/* silly things to use as flags because stderr/stdout aren't usable as pointers
 * on their own. */
int CRUSTY_STDOUT = 0;
int CRUSTY_STDERR = 1;

/* general debug output things */
int write_to(void *priv,
             CrustyType type,
             unsigned int size,
             void *ptr,
             unsigned int index) {
    FILE *out;
    if(priv == &CRUSTY_STDOUT) {
        out = stdout;
    } else {
        out = stderr;
    }

    switch(type) {
        case CRUSTY_TYPE_CHAR:
            fprintf(out, "%c", *(char *)ptr);
            break;
        case CRUSTY_TYPE_INT:
            fprintf(out, "%d", *(int *)ptr);
            break;
        case CRUSTY_TYPE_FLOAT:
            fprintf(out, "%g", *(double *)ptr);
            break;
        default:
            fprintf(stderr, "Unknown type for printing.\n");
            return(-1);
    }

    return(0);
}

int write_string_to(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    FILE *out;

    if(type != CRUSTY_TYPE_CHAR) {
        fprintf(stderr, "Attempt to print non-string.\n");
        return(-1);
    }

    if(priv == &CRUSTY_STDOUT) {
        out = stdout;
    } else {
        out = stderr;
    }

    if(fwrite((char *)ptr, 1, size, out) < size) {
        fprintf(stderr, "Failed to print string.\n");
        return(-1);
    }

    return(0);
}

/* setting/getting general state */
int set_buffer(void *priv,
               CrustyType type,
               unsigned int size,
               void *ptr,
               unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    state->buffer = ptr;
    state->size = size;
    if(type == CRUSTY_TYPE_INT) {
        state->size *= sizeof(int);
    } else if(type == CRUSTY_TYPE_FLOAT) {
        state->size *= sizeof(double);
    }

    return(0);
}

int get_return(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    *((int *)val) = state->ret;
    
    return(0);
}

/* tileset stuff */
int gfx_add_tileset(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    int w, h, pitch, tw, th;
    /* check data type and size */
    if(type != CRUSTY_TYPE_INT || size < 5) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }
    /* set variables to usable names */
    int *buf = (int *)ptr;
    w = buf[0];  h = buf[1];  pitch = buf[2];
    tw = buf[3]; th = buf[4];
    if(w < 0 || h < 0 || pitch < 0 || tw < 0 || th < 0) {
        fprintf(stderr, "Value out of range.\n");
        return(-1);
    }

    /* check to see, given a particular dimensions and pitch, that
     * the buffer has enough space to create the entire surface */
    if(state->size < (pitch * (h - 1)) + (w * 4)) {
        fprintf(stderr, "Buffer too small to create requested tileset.\n");
        return(-1);
    }
    state->ret = tilemap_add_tileset(state->ll, state->buffer,
                                     w, h, pitch, tw, th);
    if(state->ret < 0) {
        return(-1);
    }

    return(0);
}

int gfx_free_tileset(void *priv,
                     CrustyType type,
                     unsigned int size,
                     void *ptr,
                     unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_free_tileset(state->ll, *(int *)ptr));
}

int gfx_add_tilemap(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    int w, h;
    /* check data type and size */
    if(type != CRUSTY_TYPE_INT || size < 2) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }
    /* set variables to usable names */
    int *buf = (int *)ptr;
    w = buf[0]; h = buf[1];
  
    if(w < 0 || h < 0) {
        fprintf(stderr, "Value out of range.\n");
        return(-1);
    }

    state->ret = tilemap_add_tilemap(state->ll, w, h);
    if(state->ret < 0) {
        return(-1);
    }

    return(0);
}

int gfx_free_tilemap(void *priv,
                     CrustyType type,
                     unsigned int size,
                     void *ptr,
                     unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_free_tilemap(state->ll, *(int *)ptr));
}

int gfx_set_tilemap_tileset(void *priv,
                            CrustyType type,
                            unsigned int size,
                            void *ptr,
                            unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_tilemap_tileset(state->ll, index, *(int *)ptr));
}

int gfx_set_tilemap_map(void *priv,
                        CrustyType type,
                        unsigned int size,
                        void *ptr,
                        unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int x, y, pitch, w, h;

    if(type != CRUSTY_TYPE_INT || size < 5) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    x = buf[0]; y = buf[1]; pitch = buf[2];
    w = buf[3]; h = buf[4];

    if(x < 0 || y < 0) {
        fprintf(stderr, "Value out of range.\n");
        return(-1);
    }

    return(tilemap_set_tilemap_map(state->ll,
                                   index,
                                   x, y,
                                   pitch,
                                   w, h,
                                   (unsigned int *)(state->buffer),
                                   state->size / sizeof(unsigned int)));
}

int gfx_set_tilemap_attr_flags(void *priv,
                               CrustyType type,
                               unsigned int size,
                               void *ptr,
                               unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int x, y, pitch, w, h;

    if(type != CRUSTY_TYPE_INT || size < 5) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    x = buf[0]; y = buf[1]; pitch = buf[2];
    w = buf[3]; h = buf[4];

    if(x < 0 || y < 0) {
        fprintf(stderr, "Value out of range.\n");
        return(-1);
    }

    return(tilemap_set_tilemap_attr_flags(state->ll,
                                          index,
                                          x, y,
                                          pitch,
                                          w, h,
                                          (unsigned int *)(state->buffer),
                                          state->size / sizeof(unsigned int)));
}

int gfx_set_tilemap_attr_colormod(void *priv,
                                  CrustyType type,
                                  unsigned int size,
                                  void *ptr,
                                  unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int x, y, pitch, w, h;

    if(type != CRUSTY_TYPE_INT || size < 5) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    x = buf[0]; y = buf[1]; pitch = buf[2];
    w = buf[3]; h = buf[4];

    if(x < 0 || y < 0) {
        fprintf(stderr, "Value out of range.\n");
        return(-1);
    }

    return(tilemap_set_tilemap_attr_colormod(state->ll,
                                             index,
                                             x, y,
                                             pitch,
                                             w, h,
                                             (unsigned int *)(state->buffer),
                                             state->size / sizeof(unsigned int)));
}

int gfx_update_tilemap(void *priv,
                       CrustyType type,
                       unsigned int size,
                       void *ptr,
                       unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int x, y, w, h;

    if(type != CRUSTY_TYPE_INT || size < 4) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    x = buf[0]; y = buf[1];
    w = buf[2]; h = buf[3];

    if(x < 0 || y < 0 || w < 0 || h < 0) {
        fprintf(stderr, "Value out of range.\n");
        return(-1);
    }

    return(tilemap_update_tilemap(state->ll, index, x, y, w, h));
}

int gfx_add_layer(void *priv,
                  CrustyType type,
                  unsigned int size,
                  void *ptr,
                  unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    state->ret = tilemap_add_layer(state->ll, *(int *)ptr);
    if(state->ret < 0) {
        return(-1);
    }

    return(0);
}

int gfx_free_layer(void *priv,
                   CrustyType type,
                   unsigned int size,
                   void *ptr,
                   unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_free_layer(state->ll, *(int *)ptr));
}
 
int gfx_set_layer_pos(void *priv,
                      CrustyType type,
                      unsigned int size,
                      void *ptr,
                      unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int x, y;

    if(type != CRUSTY_TYPE_INT || size < 2) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    x = buf[0]; y = buf[1];
 
    return(tilemap_set_layer_pos(state->ll, index, x, y));
}
 
int gfx_set_layer_window(void *priv,
                         CrustyType type,
                         unsigned int size,
                         void *ptr,
                         unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int w, h;

    if(type != CRUSTY_TYPE_INT || size < 2) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    w = buf[0]; h = buf[1];
 
    return(tilemap_set_layer_window(state->ll, index, w, h));
}
 
int gfx_set_layer_scroll_pos(void *priv,
                             CrustyType type,
                             unsigned int size,
                             void *ptr,
                             unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int x, y;

    if(type != CRUSTY_TYPE_INT || size < 2) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    int *buf = (int *)ptr;
    x = buf[0]; y = buf[1];
 
    return(tilemap_set_layer_scroll_pos(state->ll, index, x, y));
}

int gfx_set_layer_scale(void *priv,
                        CrustyType type,
                        unsigned int size,
                        void *ptr,
                        unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    double x, y;

    if(type != CRUSTY_TYPE_FLOAT || size < 2) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    double *buf = (double *)ptr;
    x = buf[0]; y = buf[1];
 
    return(tilemap_set_layer_scale(state->ll, index, x, y));
}

int gfx_set_layer_colormod(void *priv,
                           CrustyType type,
                           unsigned int size,
                           void *ptr,
                           unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_colormod(state->ll, index, *(int *)ptr));
}

int gfx_set_layer_blendmode(void *priv,
                            CrustyType type,
                            unsigned int size,
                            void *ptr,
                            unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_blendmode(state->ll, index, *(int *)ptr));
}

int gfx_draw_layer(void *priv,
                   CrustyType type,
                   unsigned int size,
                   void *ptr,
                   unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_draw_layer(state->ll, *(int *)ptr));
}

int gfx_set_video_mode(void *priv,
                       CrustyType type,
                       unsigned int size,
                       void *ptr,
                       unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    char mode[VIDEO_MODE_STR_SIZE];

    if(type != CRUSTY_TYPE_CHAR ||
       size > VIDEO_MODE_STR_SIZE - 1) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    memcpy(mode, (char *)ptr, size);
    mode[size] = '\0';

    if(strcmp("fullscreen", mode) == 0) {
        if(SDL_SetWindowFullscreen(state->win,
                                   SDL_WINDOW_FULLSCREEN_DESKTOP) < 0) {
            fprintf(stderr, "Failed to set window fullscreen.\n");
            return(-1);
        }
    } else {
        char *x;
        char *end;
        x = strchr(mode, VIDEO_MODE_SEPARATOR);
        if(x == NULL || x == mode || x[0] == '\0') {
            fprintf(stderr, "Malformed video mode: %s\n", mode);
            return(-1);
        }
        x[0] = '\0';
        x = &(x[1]);
        unsigned long int width, height;
        width = strtoul(mode, &end, 10);
        if(end == mode || end[0] != '\0') {
            fprintf(stderr, "Malformed number: %s\n", mode);
            return(-1);
        }
        height = strtoul(x, &end, 10);
        if(end == x || end[0] != '\0') {
            fprintf(stderr, "Malformed number: %s\n", x);
            return(-1);
        }
        if(SDL_SetWindowFullscreen(state->win, 0) < 0) {
            fprintf(stderr, "Failed to set windowed.\n");
            return(-1);
        }
        SDL_SetWindowSize(state->win, width, height);
    }

    return(0);
}

int gfx_get_width(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int temp;

    if(SDL_GetRendererOutputSize(state->renderer, (int *)val, &temp) < 0) {
        fprintf(stderr, "Failed to get renderer output size.\n");
        return(-1);
    }

    return(0);
}

int gfx_get_height(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int temp;

    if(SDL_GetRendererOutputSize(state->renderer, &temp, (int *)val) < 0) {
        fprintf(stderr, "Failed to get renderer output size.\n");
        return(-1);
    }

    return(0);
}

int get_ticks(void *priv, void *val, unsigned int index) {
    *(int *)val = SDL_GetTicks();

    return(0);
}

int set_running(void *priv,
                CrustyType type,
                unsigned int size,
                void *ptr,
                unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    state->running = *(int *)ptr;

    return(0);
}

int get_random(void *priv, void *val, unsigned int index) {
    *(int *)val = rand();

    return(0);
}

int event_get_type(void *priv, void *val, unsigned int index) {
    SDL_Event *event = (SDL_Event *)priv;
 
    switch(event->type) {
        case SDL_KEYDOWN:
            *(int *)val = CRUSTYGAME_KEYDOWN;
            break;
        case SDL_KEYUP:
            *(int *)val = CRUSTYGAME_KEYUP;
            break;
        case SDL_MOUSEMOTION:
            *(int *)val = CRUSTYGAME_MOUSEMOTION;
            break;
        case SDL_MOUSEBUTTONDOWN:
            *(int *)val = CRUSTYGAME_MOUSEBUTTONDOWN;
            break;
        case SDL_MOUSEBUTTONUP:
            *(int *)val = CRUSTYGAME_MOUSEBUTTONUP;
            break;
        case SDL_MOUSEWHEEL:
            *(int *)val = CRUSTYGAME_MOUSEWHEEL;
            break;
        case SDL_JOYAXISMOTION:
            *(int *)val = CRUSTYGAME_JOYAXISMOTION;
            break;
        case SDL_JOYBALLMOTION:
            *(int *)val = CRUSTYGAME_JOYBALLMOTION;
            break;
        case SDL_JOYHATMOTION:
            *(int *)val = CRUSTYGAME_JOYHATMOTION;
            break;
        case SDL_JOYBUTTONDOWN:
            *(int *)val = CRUSTYGAME_JOYBUTTONDOWN;
            break;
        case SDL_JOYBUTTONUP:
            *(int *)val = CRUSTYGAME_JOYBUTTONUP;
            break;
        case SDL_CONTROLLERAXISMOTION:
            *(int *)val = CRUSTYGAME_CONTROLLERAXISMOTION;
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            *(int *)val = CRUSTYGAME_CONTROLLERBUTTONDOWN;
            break;
        case SDL_CONTROLLERBUTTONUP:
            *(int *)val = CRUSTYGAME_CONTROLLERBUTTONUP;
            break;
        default:
            fprintf(stderr, "Invalid event: %d\n", event->type);
            return(-1);
    }
 
    return(0);
}

int event_get_time(void *priv, void *val, unsigned int index) {
    SDL_CommonEvent *event = (SDL_CommonEvent *)priv;
    *(int *)val = event->timestamp;

    return(0);
}

int event_get_button(void *priv, void *val, unsigned int index) {
    SDL_Event *event = (SDL_Event *)priv;

    switch(event->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            *(int *)val = ((SDL_KeyboardEvent *)priv)->keysym.sym;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            *(int *)val = ((SDL_MouseButtonEvent *)priv)->button;
            break;
        case SDL_JOYAXISMOTION:
            *(int *)val = ((SDL_JoyAxisEvent *)priv)->axis;
            break;
        case SDL_JOYBALLMOTION:
            *(int *)val = ((SDL_JoyBallEvent *)priv)->ball;
            break;
        case SDL_JOYHATMOTION:
            *(int *)val = ((SDL_JoyHatEvent *)priv)->hat;
            break;
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            *(int *)val = ((SDL_JoyButtonEvent *)priv)->button;
            break;
        case SDL_CONTROLLERAXISMOTION:
            switch(((SDL_ControllerAxisEvent *)priv)->axis) {
                case SDL_CONTROLLER_AXIS_LEFTX:
                    *(int *)val = CRUSTYGAME_CONTROLLER_AXIS_LEFTX;
                    break;
                case SDL_CONTROLLER_AXIS_LEFTY:
                    *(int *)val = CRUSTYGAME_CONTROLLER_AXIS_LEFTY;
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTX:
                    *(int *)val = CRUSTYGAME_CONTROLLER_AXIS_RIGHTX;
                    break;
                case SDL_CONTROLLER_AXIS_RIGHTY:
                    *(int *)val = CRUSTYGAME_CONTROLLER_AXIS_RIGHTY;
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                    *(int *)val = CRUSTYGAME_CONTROLLER_AXIS_TRIGGERLEFT;
                    break;
                case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                    *(int *)val = CRUSTYGAME_CONTROLLER_AXIS_TRIGGERRIGHT;
                    break;
                default:
                    fprintf(stderr, "Invalid game controller axis: %d\n", 
                                    ((SDL_ControllerAxisEvent *)priv)->axis);
                    return(-1);
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            switch(((SDL_ControllerButtonEvent *)priv)->button) {
                case SDL_CONTROLLER_BUTTON_A:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_A;
                    break;
                case SDL_CONTROLLER_BUTTON_B:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_B;
                    break;
                case SDL_CONTROLLER_BUTTON_X:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_X;
                    break;
                case SDL_CONTROLLER_BUTTON_Y:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_Y;
                    break;
                case SDL_CONTROLLER_BUTTON_BACK:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_BACK;
                    break;
                case SDL_CONTROLLER_BUTTON_GUIDE:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_GUIDE;
                    break;
                case SDL_CONTROLLER_BUTTON_START:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_START;
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSTICK:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_LEFTSTICK;
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_RIGHTSTICK;
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_LEFTSHOULDER;
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_RIGHTSHOULDER;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_DPAD_UP;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_DPAD_DOWN;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_DPAD_LEFT;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
                    *(int *)val = CRUSTYGAME_CONTROLLER_BUTTON_DPAD_RIGHT;
                    break;
                default:
                    fprintf(stderr, "Invalid game controller button: %d\n",
                                    ((SDL_ControllerButtonEvent *)priv)->button);
                    return(-1);
            }
            break;
        default:
            fprintf(stderr, "Invalid event type.\n");
            return(-1);
    }

    return(0);
}

int event_get_x(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    SDL_Event *event = &(state->lastEvent);

    switch(event->type) {
        case SDL_MOUSEMOTION:
            /* return the absolute window position if the cursor is visible
             * otherwise return the relative motion if the cursor is locked. */
            if(state->mouseCaptured > 0) {
                *(int *)val = ((SDL_MouseMotionEvent *)priv)->xrel;
            } else {
                *(int *)val = ((SDL_MouseMotionEvent *)priv)->x;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            *(int *)val = ((SDL_MouseButtonEvent *)priv)->x;
            break;
        case SDL_MOUSEWHEEL:
            *(int *)val = ((SDL_MouseWheelEvent *)priv)->x;
            break;
        case SDL_JOYAXISMOTION:
            *(int *)val = ((SDL_JoyAxisEvent *)priv)->value;
            break;
        case SDL_JOYBALLMOTION:
            *(int *)val = ((SDL_JoyBallEvent *)priv)->xrel;
            break;
        case SDL_JOYHATMOTION:
            *(int *)val = ((SDL_JoyHatEvent *)priv)->value;
            break;
        case SDL_CONTROLLERAXISMOTION:
            *(int *)val = ((SDL_ControllerAxisEvent *)priv)->value;
            break;
        default:
            fprintf(stderr, "Invalid event type.\n");
            return(-1);
    }

    return(0);
}

int event_get_y(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    SDL_Event *event = &(state->lastEvent);

    switch(event->type) {
        case SDL_MOUSEMOTION:
            /* return the absolute window position if the cursor is visible
             * otherwise return the relative motion if the cursor is locked. */
            if(state->mouseCaptured > 0) {
                *(int *)val = ((SDL_MouseMotionEvent *)priv)->yrel;
            } else {
                *(int *)val = ((SDL_MouseMotionEvent *)priv)->y;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            *(int *)val = ((SDL_MouseButtonEvent *)priv)->y;
            break;
        case SDL_MOUSEWHEEL:
            *(int *)val = ((SDL_MouseWheelEvent *)priv)->y;
            break;
        case SDL_JOYBALLMOTION:
            *(int *)val = ((SDL_JoyBallEvent *)priv)->yrel;
            break;
        default:
            fprintf(stderr, "Invalid event type.\n");
            return(-1);
    }

    return(0);
}

int event_set_mouse_capture(void *priv,
                            CrustyType type,
                            unsigned int size,
                            void *ptr,
                            unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    if(state->mouseCaptured < 0 && *(int *)ptr != 0) {
        fprintf(stderr, "Attempt to recapture mouse after forced release, "
                        "press CTRL+F10 again to allow recapturing.\n");
    } else if(state->mouseCaptured == 0 && *(int *)ptr != 0) {
        if(SDL_SetRelativeMouseMode(1) < 0) {
            fprintf(stderr, "Failed to set relative mouse mode.\n");
            return(-1);
        }
        state->mouseCaptured = 1;
    } else if(state->mouseCaptured > 0 && *(int *)ptr == 0) {
        if(SDL_SetRelativeMouseMode(0) < 0) {
            fprintf(stderr, "Failed to clear relative mouse mode.\n");
            return(-1);
        }
        state->mouseCaptured = 0;
    }

    return(0);
}

int savedata_seek(void *priv,
                  CrustyType type,
                  unsigned int size,
                  void *ptr,
                  unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int pos;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    if(state->savefile == NULL) {
        fprintf(stderr, "Seek in nonexistent save memory.\n");
        return(-1);
    }

    pos = *(int *)ptr;
    if(pos < 0) {
        fprintf(stderr, "Negative seek index.\n");
        return(-1);
    }
    /* save mirroring */
    pos %= state->savesize;

    if(fseek(state->savefile, pos, SEEK_SET) < 0) {
        fprintf(stderr, "Failed to seek in save file: %s\n",
                        strerror(errno));
        return(-1);
    }

    return(0);
}
        
int savedata_write(void *priv,
                   CrustyType type,
                   unsigned int size,
                   void *ptr,
                   unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int pos;

    if(state->savefile == NULL) {
        fprintf(stderr, "Write to nonexistent save memory.\n");
        return(-1);
    }

    pos = ftell(state->savefile);
    if(pos < 0) {
        fprintf(stderr, "Failed to get position in save file.\n");
        return(-1);
    }

    if(type == CRUSTY_TYPE_CHAR) {
        if(fwrite((unsigned char *)ptr, 1, 1, state->savefile) < 1) {
            fprintf(stderr, "Failed to write to savefile.\n");
            return(-1);
        }
        if(pos + 1 == state->savesize) {
            if(fseek(state->savefile, 0, SEEK_SET)) {
                fprintf(stderr, "Failed to seek in save file: %s\n",
                                strerror(errno));
                fprintf(stderr, "Failed to seek in savefile.\n");
                return(-1);
            }
        }
    } else if(type == CRUSTY_TYPE_INT) {
        if(pos > state->savesize - sizeof(int)) {
            fprintf(stderr, "Not enough space to write int value.\n");
            return(-1);
        }
        if(fwrite((int *)ptr, 1, sizeof(int), state->savefile) < sizeof(int)) {
            fprintf(stderr, "Failed to write to savefile.\n");
            return(-1);
        }
        if(pos + sizeof(int) == state->savesize) {
            if(fseek(state->savefile, 0, SEEK_SET)) {
                fprintf(stderr, "Failed to seek in save file: %s\n",
                                strerror(errno));
                fprintf(stderr, "Failed to seek in savefile.\n");
                return(-1);
            }
        }
    } else { /* FLOAT */
        if(pos > state->savesize - sizeof(double)) {
            fprintf(stderr, "Not enough space to write float value.\n");
            return(-1);
        }
        if(fwrite((double *)ptr, 1, sizeof(double), state->savefile) < sizeof(double)) {
            fprintf(stderr, "Failed to write to savefile.\n");
            return(-1);
        }
        if(pos + sizeof(double) == state->savesize) {
            if(fseek(state->savefile, 0, SEEK_SET)) {
                fprintf(stderr, "Failed to seek in save file: %s\n",
                                strerror(errno));
                return(-1);
            }
        }
    }

    return(0);
}

int savedata_read_char(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int pos;

    if(state->savefile == NULL) {
        fprintf(stderr, "Write to nonexistent save memory.\n");
        return(-1);
    }

    pos = ftell(state->savefile);
    if(pos < 0) {
        fprintf(stderr, "Failed to get position in save file.\n");
        return(-1);
    }

    if(fread((unsigned char *)val, 1, 1, state->savefile) < 1) {
        fprintf(stderr, "Failed to read from savefile.\n");
        return(-1);
    }
    if(pos + 1 == state->savesize) {
        if(fseek(state->savefile, 0, SEEK_SET)) {
            fprintf(stderr, "Failed to seek in save file: %s\n",
                            strerror(errno));
            return(-1);
        }
    }

    return(0);
}

int savedata_read_int(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int pos;

    if(state->savefile == NULL) {
        fprintf(stderr, "Write to nonexistent save memory.\n");
        return(-1);
    }

    pos = ftell(state->savefile);
    if(pos < 0) {
        fprintf(stderr, "Failed to get position in save file.\n");
        return(-1);
    }

    /* no need to check pos beforehand because it should fail to read anyway */
    if(fread((int *)val, 1, sizeof(int), state->savefile) < sizeof(int)) {
        fprintf(stderr, "Failed to read from savefile.\n");
        return(-1);
    }
    if(pos + sizeof(int) == state->savesize) {
        if(fseek(state->savefile, 0, SEEK_SET)) {
            fprintf(stderr, "Failed to seek in save file: %s\n",
                            strerror(errno));
            return(-1);
        }
    }

    return(0);
}

int savedata_read_float(void *priv, void *val, unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;
    int pos;

    if(state->savefile == NULL) {
        fprintf(stderr, "Write to nonexistent save memory.\n");
        return(-1);
    }

    pos = ftell(state->savefile);
    if(pos < 0) {
        fprintf(stderr, "Failed to get position in save file.\n");
        return(-1);
    }

    if(fread((float *)val, 1, sizeof(float), state->savefile) < sizeof(float)) {
        fprintf(stderr, "Failed to read from savefile.\n");
        return(-1);
    }
    if(pos + sizeof(float) == state->savesize) {
        if(fseek(state->savefile, 0, SEEK_SET)) {
            fprintf(stderr, "Failed to seek in save file: %s\n",
                            strerror(errno));
            return(-1);
        }
    }

    return(0);
}

int set_window_title(void *priv,
                     CrustyType type,
                     unsigned int size,
                     void *ptr,
                     unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_CHAR) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    char title[size + 1];
    memcpy(title, ptr, size + 1);
    title[size] = '\0';
 
    SDL_SetWindowTitle(state->win, title);

    return(0);
}

int audio_get_samples_needed(void *priv, void *val, unsigned int index) {
}

int audio_get_rate(void *priv, void *val, unsigned int index) {
}

int audio_get_channels(void *priv, void *val, unsigned int index) {
}

int audio_get_fragment_size(void *priv, void *val, unsigned int index) {
}

int audio_has_underrun(void *priv, void *val, unsigned int index) {
}

int audio_set_enabled(void *priv,
                      CrustyType type,
                      unsigned int size,
                      void *ptr,
                      unsigned int index) {
}

int audio_set_fragments(void *priv,
                        CrustyType type,
                        unsigned int size,
                        void *ptr,
                        unsigned int index) {
}

int audio_add_buffer(void *priv,
                     CrustyType type,
                     unsigned int size,
                     void *ptr,
                     unsigned int index) {
}

int audio_free_buffer(void *priv,
                      CrustyType type,
                      unsigned int size,
                      void *ptr,
                      unsigned int index) {
}

int audio_add_player(void *priv,
                     CrustyType type,
                     unsigned int size,
                     void *ptr,
                     unsigned int index) {
}

int audio_free_player(void *priv,
                      CrustyType type,
                      unsigned int size,
                      void *ptr,
                      unsigned int index) {
}

int audio_set_player_input_buffer(void *priv,
                                  CrustyType type,
                                  unsigned int size,
                                  void *ptr,
                                  unsigned int index) {
}

int audio_set_player_input_buffer_pos(void *priv,
                                      CrustyType type,
                                      unsigned int size,
                                      void *ptr,
                                      unsigned int index) {
}

int audio_set_player_output_buffer(void *priv,
                                   CrustyType type,
                                   unsigned int size,
                                   void *ptr,
                                   unsigned int index) {
}

int audio_set_player_output_buffer_pos(void *priv,
                                       CrustyType type,
                                       unsigned int size,
                                       void *ptr,
                                       unsigned int index) {
}

int audio_set_player_output_mode(void *priv,
                                 CrustyType type,
                                 unsigned int size,
                                 void *ptr,
                                 unsigned int index) {
}

int audio_set_player_volume_mode(void *priv,
                                 CrustyType type,
                                 unsigned int size,
                                 void *ptr,
                                 unsigned int index) {
}

int audio_set_player_volume(void *priv,
                            CrustyType type,
                            unsigned int size,
                            void *ptr,
                            unsigned int index) {
}

int audio_set_player_volume_source(void *priv,
                                   CrustyType type,
                                   unsigned int size,
                                   void *ptr,
                                   unsigned int index) {
}

int audio_set_player_volume_source_scale(void *priv,
                                         CrustyType type,
                                         unsigned int size,
                                         void *ptr,
                                         unsigned int index) {
}

int audio_set_player_mode(void *priv,
                          CrustyType type,
                          unsigned int size,
                          void *ptr,
                          unsigned int index) {
}

int audio_set_player_loop_start(void *priv,
                                CrustyType type,
                                unsigned int size,
                                void *ptr,
                                unsigned int index) {
}

int audio_set_player_loop_end(void *priv,
                              CrustyType type,
                              unsigned int size,
                              void *ptr,
                              unsigned int index) {
}

int audio_set_player_phase_source(void *priv,
                                  CrustyType type,
                                  unsigned int size,
                                  void *ptr,
                                  unsigned int index) {
}

int audio_set_player_speed_mode(void *priv,
                                CrustyType type,
                                unsigned int size,
                                void *ptr,
                                unsigned int index) {
}

int audio_set_player_speed(void *priv,
                           CrustyType type,
                           unsigned int size,
                           void *ptr,
                           unsigned int index) {
}

int audio_set_player_speed_source(void *priv,
                                  CrustyType type,
                                  unsigned int size,
                                  void *ptr,
                                  unsigned int index) {
}

int audio_set_player_speed_source_scale(void *priv,
                                        CrustyType type,
                                        unsigned int size,
                                        void *ptr,
                                        unsigned int index) {
}

int audio_run_player(void *priv,
                     CrustyType type,
                     unsigned int size,
                     void *ptr,
                     unsigned int index) {
}

CrustyCallback cb[] = {
    {
        .name = "out", .length = 1, .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = write_to, .writepriv = &CRUSTY_STDOUT
    },
    {
        .name = "err", .length = 1, .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = write_to, .writepriv = &CRUSTY_STDERR
    },
    {
        .name = "string_out",
        .length = 1, .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = write_string_to, .writepriv = &CRUSTY_STDOUT
    },
    {
        .name = "string_err",
        .length = 1, .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = write_string_to, .writepriv = &CRUSTY_STDERR
    },
    {
        .name = "set_buffer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = set_buffer, .writepriv = &state
    },
    {
        .name = "get_return", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = get_return, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "gfx_add_tileset", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_add_tileset, .writepriv = &state
    },
    {
        .name = "gfx_free_tileset", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_add_tileset, .writepriv = &state
    },
    {
        .name = "gfx_add_tilemap", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_add_tilemap, .writepriv = &state
    },
    {
        .name = "gfx_free_tilemap", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_add_tilemap, .writepriv = &state
    },
    {
        .name = "gfx_set_tilemap_tileset", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_tilemap_tileset, .writepriv = &state
    },
    {
        .name = "gfx_set_tilemap_map", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_tilemap_map, .writepriv = &state
    },
    {
        .name = "gfx_set_tilemap_attr_flags", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_tilemap_attr_flags, .writepriv = &state
    },
    {
        .name = "gfx_set_tilemap_attr_colormod", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_tilemap_attr_colormod, .writepriv = &state
    },
    {
        .name = "gfx_update_tilemap", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_update_tilemap, .writepriv = &state
    },
    {
        .name = "gfx_add_layer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_add_layer, .writepriv = &state
    },
    {
        .name = "gfx_free_layer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_add_layer, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_pos", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_pos, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_window", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_window, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_scroll_pos", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_scroll_pos, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_scale", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_scale, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_colormod", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_colormod, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_blendmode", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_blendmode, .writepriv = &state
    },
    {
        .name = "gfx_draw_layer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_draw_layer, .writepriv = &state
    },
    {
        .name = "gfx_set_video_mode", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_video_mode, .writepriv = &state
    },
    {
        .name = "gfx_get_width", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = gfx_get_width, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "gfx_get_height", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = gfx_get_height, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "get_ticks", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = get_ticks, .readpriv = NULL,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "set_running", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = set_running, .writepriv = &state
    },
    {
        .name = "get_random", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = get_random, .readpriv = NULL,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "event_get_type", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = event_get_type, .readpriv = &(state.lastEvent),
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "event_get_time", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = event_get_time, .readpriv = &(state.lastEvent),
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "event_get_button", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = event_get_button, .readpriv = &(state.lastEvent),
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "event_get_x", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = event_get_x, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "event_get_y", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = event_get_y, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "event_set_mouse_capture", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = event_set_mouse_capture, .writepriv = &state
    },
    {
        .name = "savedata_seek", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = savedata_seek, .writepriv = &state
    },
    {
        .name = "savedata_write", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = savedata_write, .writepriv = &state
    },
    {
        .name = "savedata_read_char", .length = 1,
        .readType = CRUSTY_TYPE_CHAR,
        .read = savedata_read_char, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "savedata_read_int", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = savedata_read_int, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "savedata_read_float", .length = 1,
        .readType = CRUSTY_TYPE_FLOAT,
        .read = savedata_read_float, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "set_window_title", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = set_window_title, .writepriv = &state
    },
    {
        .name = "audio_get_samples_needed", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = audio_get_samples_needed, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "audio_get_rate", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = audio_get_rate, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "audio_get_channels", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = audio_get_channels, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "audio_get_fragment_size", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = audio_get_fragment_size, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "audio_has_underrun", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = audio_has_underrun, .readpriv = &state,
        .write = NULL, .writepriv = NULL
    },
    {
        .name = "audio_set_enabled", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_enabled, .writepriv = &state
    },
    {
        .name = "audio_set_fragments", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_fragments, .writepriv = &state
    },
    {
        .name = "audio_add_buffer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_add_buffer, .writepriv = &state
    },
    {
        .name = "audio_free_buffer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_free_buffer, .writepriv = &state
    },
    {
        .name = "audio_add_player", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_add_player, .writepriv = &state
    },
    {
        .name = "audio_free_player", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_free_player, .writepriv = &state
    },
    {
        .name = "audio_set_player_input_buffer", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_input_buffer, .writepriv = &state
    },
    {
        .name = "audio_set_player_input_buffer_pos", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_input_buffer_pos, .writepriv = &state
    },
    {
        .name = "audio_set_player_output_buffer", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_output_buffer, .writepriv = &state
    },
    {
        .name = "audio_set_player_output_buffer_pos", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_output_buffer_pos, .writepriv = &state
    },
    {
        .name = "audio_set_player_output_mode", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_output_mode, .writepriv = &state
    },
    {
        .name = "audio_set_player_volume_mode", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_volume_mode, .writepriv = &state
    },
    {
        .name = "audio_set_player_volume", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_volume, .writepriv = &state
    },
    {
        .name = "audio_set_player_volume_source", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_volume_source, .writepriv = &state
    },
    {
        .name = "audio_set_player_volume_source_scale", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_volume_source_scale, .writepriv = &state
    },
    {
        .name = "audio_set_player_mode", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_mode, .writepriv = &state
    },
    {
        .name = "audio_set_player_loop_start", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_loop_start, .writepriv = &state
    },
    {
        .name = "audio_set_player_loop_end", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_loop_end, .writepriv = &state
    },
    {
        .name = "audio_set_player_phase_source", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_phase_source, .writepriv = &state
    },
    {
        .name = "audio_set_player_speed_mode", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_speed_mode, .writepriv = &state
    },
    {
        .name = "audio_set_player_speed", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_speed, .writepriv = &state
    },
    {
        .name = "audio_set_player_source", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_source, .writepriv = &state
    },
    {
        .name = "audio_set_player_source_scale", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_set_player_source_scale, .writepriv = &state
    },
    {
        .name = "audio_run_player", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = audio_run_player, .writepriv = &state
    }
};

const int CRUSTYGAME_CALLBACKS = sizeof(cb) / sizeof(CrustyCallback);
