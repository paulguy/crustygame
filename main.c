#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <SDL.h>

#include "crustyvm.h"
#include "tilemap.h"

/* initial settings */
#define WINDOW_TITLE    "CrustyGame"
#define WINDOW_WIDTH    (640)
#define WINDOW_HEIGHT   (480)

#define VIDEO_MODE_STR_SIZE (256)
#define VIDEO_MODE_SEPARATOR 'x'

typedef struct {
    SDL_Window *win;
    SDL_Renderer *renderer;
    SDL_Event lastEvent;
    LayerList *ll;
    int running;

    void *buffer;
    unsigned int size;
    int ret;
} CrustyGame;

int initialize_SDL(SDL_Window **win,
                   SDL_Renderer **renderer,
                   Uint32 *format) {
    int drivers;
    int bestdrv, softdrv, selectdrv;
    int selectfmt;
    Uint32 bestfmt, softfmt;
    int i, j;
    SDL_RendererInfo driver;

    /* SDL/Windows/Render initialization stuff */
    if(SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n",
                SDL_GetError());
        return(-1);
    }

    /* try to determine what driver to use based on available
     * features and prioritize an accelerated driver then fall back to
     * the software driver */
    drivers = SDL_GetNumRenderDrivers();
    fprintf(stderr, "Video Drivers: %d\n", drivers);

    bestdrv = -1;
    bestfmt = SDL_PIXELFORMAT_UNKNOWN;
    softdrv = -1;
    softfmt = SDL_PIXELFORMAT_UNKNOWN;
    for(i = 0; i < drivers; i++) {
        if(SDL_GetRenderDriverInfo(i, &driver) < 0) {
            fprintf(stderr, "Couldn't get driver info for index %d.\n",
                    i);
            continue;
        }

        fprintf(stderr, "Driver %d: %s", i, driver.name);
        if((driver.flags & SDL_RENDERER_SOFTWARE) &&
           softdrv == -1) {
            for(j = 0; j < driver.num_texture_formats; j++) {
                if(SDL_BITSPERPIXEL(driver.texture_formats[j]) >= 24) {
                    fprintf(stderr, " (fallback)");
                    softfmt = driver.texture_formats[j];
                    softdrv = i;
                    break;
                }
            }
        } else if((driver.flags & SDL_RENDERER_ACCELERATED) &&
                  (driver.flags & SDL_RENDERER_TARGETTEXTURE) &&
                  bestdrv == -1) {
            for(j = 0; j < driver.num_texture_formats; j++) {
                if(SDL_BITSPERPIXEL(driver.texture_formats[j]) >= 24) {
                    fprintf(stderr, " (selected)");
                    bestfmt = driver.texture_formats[j];
                    bestdrv = i;
                    break;
                }
            }
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "Flags: (%08X) ", driver.flags);
        if(driver.flags & SDL_RENDERER_SOFTWARE)
            fprintf(stderr, "SOFTWARE ");
        if(driver.flags & SDL_RENDERER_ACCELERATED)
            fprintf(stderr, "ACCELERATED ");
        if(driver.flags & SDL_RENDERER_PRESENTVSYNC)
            fprintf(stderr, "PRESENTVSYNC ");
        if(driver.flags & SDL_RENDERER_TARGETTEXTURE)
            fprintf(stderr, "TARGETTEXTURE ");
        fprintf(stderr, "\n");
        fprintf(stderr, "Formats: ");
        for(j = 0; j < driver.num_texture_formats; j++) {
            fprintf(stderr, "(%08X) %s ",
                    driver.texture_formats[i],
                    SDL_GetPixelFormatName(driver.texture_formats[i]));
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "Max Texture Size: %d x %d\n",
                driver.max_texture_width,
                driver.max_texture_height);
    }

    /* create the window then try to create a renderer for it */
    *win = SDL_CreateWindow(WINDOW_TITLE,
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            WINDOW_WIDTH,
                            WINDOW_HEIGHT,
                            0);
    if(*win == NULL) {
        fprintf(stderr, "Failed to create SDL window.\n");
        goto error0;
    }
 
    if(bestdrv < 0) {
        if(softdrv < 0) {
            fprintf(stderr, "No accelerated or software driver found? "
                            "Trying index 0...\n");
            if(SDL_GetRenderDriverInfo(0, &driver) < 0) {
                fprintf(stderr, "Couldn't get driver info for index "
                                "0.\n");
                goto error1;
            }
            selectfmt = SDL_PIXELFORMAT_UNKNOWN;
            for(j = 0; j < driver.num_texture_formats; j++) {
                if(SDL_BITSPERPIXEL(driver.texture_formats[j]) >= 24) {
                    selectfmt = driver.texture_formats[j];
                    break;
                }
            }
            if(selectfmt == SDL_PIXELFORMAT_UNKNOWN) {
                fprintf(stderr, "Coulnd't find true color pixel "
                                "format.\n");
                goto error1;
            }

            *format = selectfmt;
            selectdrv = 0;
        } else {
            fprintf(stderr, "No accelerated driver found, falling "
                            "back to software (%d).\n", softdrv);
            *format = softfmt;
            selectdrv = softdrv;
        }
    } else {
        fprintf(stderr, "Selecting first accelerated driver (%d).\n",
                        bestdrv);
        *format = bestfmt;
        selectdrv = bestdrv;
    }

    *renderer = SDL_CreateRenderer(*win, selectdrv, SDL_RENDERER_PRESENTVSYNC);
    if(*renderer == NULL) {
        fprintf(stderr, "Failed to create SDL renderer.\n");
        goto error1;
    }

    return(0);

error1:
    SDL_DestroyWindow(*win);
error0:
    SDL_Quit();

    return(-1);
}

void vprintf_cb(void *priv, const char *fmt, ...) {
    va_list ap;
    FILE *out = priv;

    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
}

/* general debug output things */
int write_to(void *priv,
             CrustyType type,
             unsigned int size,
             void *ptr,
             unsigned int index) {
    switch(type) {
        case CRUSTY_TYPE_CHAR:
            fprintf((FILE *)priv, "%c", *(char *)ptr);
            break;
        case CRUSTY_TYPE_INT:
            fprintf((FILE *)priv, "%d", *(int *)ptr);
            break;
        case CRUSTY_TYPE_FLOAT:
            fprintf((FILE *)priv, "%g", *(float *)ptr);
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
    if(type != CRUSTY_TYPE_CHAR) {
        fprintf(stderr, "Attempt to print non-string.\n");
        return(-1);
    }

    if(fwrite((char *)ptr,
              1,
              size - index,
              (FILE *)priv) < size - index) {
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

    if(((h - 1) * pitch) + w < state->size / sizeof(unsigned int)) {
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

/* TODO: better access to input events */
int access_event(void *priv, void *val, unsigned int index) {
    *(int *)val = ((char *)priv)[index];

    return(0);
}

#define CLEAN_ARGS \
    if(vars > 0) { \
        for(i = 0; i < vars; i++) { \
            free(var[i]); \
            free(value[i]); \
        } \
        free(var); \
        free(value); \
    } \
    vars = 0;

int main(int argc, char **argv) {
    /* SDL and CrustyGame stuff */
    Uint32 format;
    CrustyGame state;
    state.buffer = NULL;
    state.size = 0;
    state.ret = 0;

    /* CrustyVM stuff */
    unsigned int i;
    const char *filename = NULL;
    unsigned int arglen;
    char *equals;
    char *temp;
    char **tempa;
    char **var = NULL;
    char **value = NULL;
    unsigned int vars = 0;

    FILE *in = NULL;
    CrustyVM *cvm;
    char *program = NULL;
    long len;
    int result;
    CrustyCallback cb[] = {
        {
            .name = "out", .length = 1, .readType = CRUSTY_TYPE_NONE,
            .read = NULL, .readpriv = NULL,
            .write = write_to, .writepriv = stdout
        },
        {
            .name = "err", .length = 1, .readType = CRUSTY_TYPE_NONE,
            .read = NULL, .readpriv = NULL,
            .write = write_to, .writepriv = stderr
        },
        {
            .name = "string_out",
            .length = 1, .readType = CRUSTY_TYPE_NONE,
            .read = NULL, .readpriv = NULL,
            .write = write_string_to, .writepriv = stdout
        },
        {
            .name = "string_err",
            .length = 1, .readType = CRUSTY_TYPE_NONE,
            .read = NULL, .readpriv = NULL,
            .write = write_string_to, .writepriv = stderr
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
            .name = "event", .length = sizeof(SDL_Event),
            .readType = CRUSTY_TYPE_INT,
            .read = access_event, .readpriv = &(state.lastEvent),
            .write = NULL, .writepriv = NULL
        }
    };

    if(initialize_SDL(&(state.win),
                      &(state.renderer),
                      &format) < 0) {
        fprintf(stderr, "Failed to initialize SDL.\n");
        goto error0;
    }

    /* CrustyVM stuff */

    for(i = 1; i < (unsigned int)argc; i++) {
        arglen = strlen(argv[i]);
        if(arglen > 0 && argv[i][0] == '-') {
            if(arglen > 1) {
                if(argv[i][1] == '-') {
                    if(filename != NULL) {
                        filename = NULL;
                        break;
                    }
                    if(i + 1 < (unsigned int)argc) {
                        filename = argv[i + 1];
                    }
                    break;
                } else if(argv[i][1] == 'D') {
                    if(argv[i][2] == '=') {
                        filename = NULL;
                        break;
                    }
                    equals = strchr(&(argv[i][2]), '=');
                    if(equals == NULL) {
                        filename = NULL;
                        break;
                    }

                    tempa = realloc(var, sizeof(char *) * (vars + 1));
                    if(tempa == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for vars list.\n");
                        goto error2;
                    }
                    var = tempa;
                    tempa = realloc(value, sizeof(char *) * (vars + 1));
                    if(tempa == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for values list.\n");
                        goto error2;
                    }
                    value = tempa;
                    /* difference from start, take away "-D", add
                     * space for '\0' */
                    temp = malloc(equals - argv[i] - 2 + 1);
                    if(temp == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for var.\n");
                        goto error2;
                    }
                    memcpy(temp, &(argv[i][2]), equals - argv[i] - 2);
                    temp[equals - argv[i] - 2] = '\0';
                    var[vars] = temp;
                    /* total length, take away the length of the first
                     * part, take away the '=', add the '\0' */
                    temp = malloc(arglen -
                                  (equals - argv[i] - 2) -
                                  1 +
                                  1);
                    if(temp == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for value.\n");
                        goto error2;
                    }
                    memcpy(temp,
                           &(equals[1]),
                           arglen - (equals - argv[i] - 2) - 1);
                    temp[arglen - (equals - argv[i] - 2) - 1] = '\0';
                    value[vars] = temp;
                    vars++;
                } else {
                    filename = NULL;
                    break;
                }
            }
        } else {
            if(filename != NULL) {
                filename = NULL;
                break;
            }
            filename = argv[i];
        }
    }

    if(filename == NULL) {
        fprintf(stderr, "USAGE: %s [(<filename>|-D<var>=<value>) ...]"
                        " [-- <filename>]\n", argv[0]);
        goto error2;
    }

    in = fopen(filename, "rb");
    if(in == NULL) {
        fprintf(stderr, "Failed to open file %s.\n", filename);
        goto error2;
    }

    if(fseek(in, 0, SEEK_END) < 0) {
       fprintf(stderr, "Failed to seek to end of file.\n");
       goto error2;
    }

    len = ftell(in);
    if(len < 0) {
        fprintf(stderr, "Failed to get file length.\n");
        goto error2;
    }
    rewind(in);

    program = malloc(len);
    if(program == NULL) {
        goto error2;
    }

    if(fread(program, 1, len, in) < (unsigned long)len) {
        fprintf(stderr, "Failed to read file.\n");
        goto error2;
    }

    fclose(in);
    in = NULL;

    cvm = crustyvm_new(filename, program, len,
                       CRUSTY_FLAG_DEFAULTS,
                       0,
                       cb, sizeof(cb) / sizeof(CrustyCallback),
                       (const char **)var, (const char **)value, vars,
                       vprintf_cb, stderr);
    if(cvm == NULL) {
        fprintf(stderr, "Failed to load program.\n");
        goto error2;
    }
    free(program);
    program = NULL;
    CLEAN_ARGS
    fprintf(stderr, "Program loaded.\n");

    fprintf(stderr, "Token memory size: %u\n",
                    crustyvm_get_tokenmem(cvm));
    fprintf(stderr, "Stack size: %u\n",
                    crustyvm_get_stackmem(cvm));

    /* initialize the layerlist */
    state.ll = layerlist_new(state.renderer,
                             format,
                             vprintf_cb,
                             stderr);
    if(state.ll == NULL) {
        fprintf(stderr, "Failed to create layerlist.\n");
        goto error3;
    }

    /* call program init */
    result = crustyvm_run(cvm, "init");
    if(result < 0) {
        fprintf(stderr, "Program reached an exception while running: "
                        "%s\n",
                crustyvm_statusstr(crustyvm_get_status(cvm)));
        crustyvm_debugtrace(cvm, 1);
        goto error2;
    }

    state.running = 1;
    while(state.running) {
        if(SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) < 0) {
            fprintf(stderr, "Failed to set render draw color.\n");
            goto error1;
        } 

        if(SDL_RenderClear(state.renderer) < 0) {
            fprintf(stderr, "Failed to clear screen.\n");
            goto error1;
        }

        /* needs to be transparent so tilemap updates work */
        if(SDL_SetRenderDrawColor(state.renderer,
                                  0, 0, 0,
                                  SDL_ALPHA_TRANSPARENT) < 0) {
            fprintf(stderr, "Failed to set render draw color.\n");
            goto error1;
        } 

        while(SDL_PollEvent(&(state.lastEvent))) {
            if(state.lastEvent.type == SDL_QUIT) {
                state.running = 0;
                continue;
            }

            result = crustyvm_run(cvm, "event");
            if(result < 0) {
                fprintf(stderr, "Program reached an exception while "
                                "running: %s\n",
                        crustyvm_statusstr(crustyvm_get_status(cvm)));
                crustyvm_debugtrace(cvm, 1);
                goto error4;
            }
        }
 
        result = crustyvm_run(cvm, "frame");
        if(result < 0) {
            fprintf(stderr, "Program reached an exception while "
                            "running: %s\n",
                    crustyvm_statusstr(crustyvm_get_status(cvm)));
            crustyvm_debugtrace(cvm, 1);
            goto error4;
        }

        SDL_RenderPresent(state.renderer);
    }

    fprintf(stderr, "Program completed successfully.\n");
    crustyvm_free(cvm);
    layerlist_free(state.ll);

    SDL_DestroyWindow(state.win);
    SDL_Quit();

    exit(EXIT_SUCCESS);

error4:
    layerlist_free(state.ll);
error3:
    crustyvm_free(cvm);
error2:
    if(program != NULL) {
        free(program);
    }

    if(in != NULL) {
        fclose(in);
    }

    CLEAN_ARGS
error1:
    SDL_DestroyWindow(state.win);
    SDL_Quit();
error0:
    return(EXIT_FAILURE);
}
