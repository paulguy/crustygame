#include <stdio.h>
#include <limits.h>
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
            fprintf(out, "%g", *(float *)ptr);
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

    if(fwrite((char *)ptr, 1, size - index, out) < size - index) {
        fprintf(stderr, "Failed to print string.\n");
        return(-1);
    }

    return(0);
}

/* setting/getting general state */
int gfx_set_buffer(void *priv,
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

int gfx_get_return(void *priv, void *val, unsigned int index) {
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

    if(((h - 1) * pitch) + w > state->size / sizeof(unsigned int)) {
        fprintf(stderr, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    return(tilemap_set_tilemap_map(state->ll,
                                   index,
                                   x, y,
                                   pitch,
                                   w, h,
                                   (unsigned int *)(state->buffer)));
}

int gfx_set_tilemap_attrs(void *priv,
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

    if(((h - 1) * pitch) + w < state->size / sizeof(unsigned int)) {
        fprintf(stderr, "Buffer too small to hold tilemap.\n");
        return(-1);
    }

    return(tilemap_set_tilemap_attrs(state->ll,
                                     index,
                                     x, y,
                                     pitch,
                                     w, h,
                                     (unsigned int *)(state->buffer)));
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
 
int gfx_set_layer_x(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_x(state->ll, index, *(int *)ptr));
}

int gfx_set_layer_y(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_y(state->ll, index, *(int *)ptr));
}
 
int gfx_set_layer_w(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_w(state->ll, index, *(int *)ptr));
}
 
int gfx_set_layer_h(void *priv,
                    CrustyType type,
                    unsigned int size,
                    void *ptr,
                    unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_h(state->ll, index, *(int *)ptr));
}
 
int gfx_set_layer_scroll_x(void *priv,
                           CrustyType type,
                           unsigned int size,
                           void *ptr,
                           unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_scroll_x(state->ll, index, *(int *)ptr));
}
 
int gfx_set_layer_scroll_y(void *priv,
                           CrustyType type,
                           unsigned int size,
                           void *ptr,
                           unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_INT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_scroll_y(state->ll, index, *(int *)ptr));
}

int gfx_set_layer_scale_x(void *priv,
                          CrustyType type,
                          unsigned int size,
                          void *ptr,
                          unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_FLOAT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_scale_x(state->ll, index, *(double *)ptr));
}

int gfx_set_layer_scale_y(void *priv,
                          CrustyType type,
                          unsigned int size,
                          void *ptr,
                          unsigned int index) {
    CrustyGame *state = (CrustyGame *)priv;

    if(type != CRUSTY_TYPE_FLOAT) {
        fprintf(stderr, "Wrong type.\n");
        return(-1);
    }

    return(tilemap_set_layer_scale_y(state->ll, index, *(double *)ptr));
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
    fprintf(stderr, "%s\n", mode);

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
        .name = "gfx_set_buffer", .length = 1,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_buffer, .writepriv = &state
    },
    {
        .name = "gfx_get_return", .length = 1,
        .readType = CRUSTY_TYPE_INT,
        .read = gfx_get_return, .readpriv = &state,
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
        .name = "gfx_set_tilemap_attrs", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_tilemap_attrs, .writepriv = &state
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
        .name = "gfx_set_layer_x", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_x, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_y", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_y, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_w", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_w, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_h", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_h, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_scroll_x", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_scroll_x, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_scroll_y", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_scroll_y, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_scale_x", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_scale_x, .writepriv = &state
    },
    {
        .name = "gfx_set_layer_scale_y", .length = INT_MAX,
        .readType = CRUSTY_TYPE_NONE,
        .read = NULL, .readpriv = NULL,
        .write = gfx_set_layer_scale_y, .writepriv = &state
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
    }
};

const int CRUSTYGAME_CALLBACKS = sizeof(cb) / sizeof(CrustyCallback);
