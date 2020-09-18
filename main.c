#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <SDL.h>

#include "crustygame.h"
#include "crustyvm.h"
#include "tilemap.h"
#include "callbacks.h"

/* initial settings */
#define WINDOW_TITLE    "CrustyGame"
#define WINDOW_WIDTH    (640)
#define WINDOW_HEIGHT   (480)

CrustyGame state;

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
    state.buffer = NULL;
    state.size = 0;
    state.ret = 0;
    state.mouseCaptured = 0;

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
                       cb, CRUSTYGAME_CALLBACKS,
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
        crustyvm_debugtrace(cvm, 0);
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
            /* allow the user to press CTRL+F10 (like DOSBOX) to uncapture a
             * captured mouse, and also enforce disallowing recapture until
             * reallowed by pressing the same combo again. */
            if(state.lastEvent.type == SDL_KEYDOWN) {
                if(((SDL_KeyboardEvent *)&(state.lastEvent))->keysym.sym ==
                   SDLK_LCTRL) {
                    state.mouseReleaseCombo |= 1;
                } else if(((SDL_KeyboardEvent *)&(state.lastEvent))->keysym.sym ==
                   SDLK_F10) {
                    state.mouseReleaseCombo |= 2;
                }

                if(state.mouseReleaseCombo == 3) {
                    if(state.mouseCaptured < 0) {
                        state.mouseCaptured = 0;
                    } else {
                        if(SDL_SetRelativeMouseMode(0) < 0) {
                            fprintf(stderr, "Failed to clear relative mouse mode.\n");
                            return(-1);
                        }
                        state.mouseCaptured = -1;
                    }
                    state.mouseReleaseCombo = 0;
                }
            } else if(state.lastEvent.type == SDL_KEYUP) {
                if(((SDL_KeyboardEvent *)&(state.lastEvent))->keysym.sym ==
                   SDLK_LCTRL) {
                    state.mouseReleaseCombo &= ~1;
                } else if(((SDL_KeyboardEvent *)&(state.lastEvent))->keysym.sym ==
                   SDLK_F10) {
                    state.mouseReleaseCombo &= ~2;
                }
            }

            switch(state.lastEvent.type) {
                case SDL_QUIT:
                    state.running = 0;
                    continue;
                case SDL_KEYDOWN:
                case SDL_KEYUP:
                case SDL_MOUSEMOTION:
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                case SDL_MOUSEWHEEL:
                case SDL_JOYAXISMOTION:
                case SDL_JOYBALLMOTION:
                case SDL_JOYHATMOTION:
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYBUTTONUP:
                case SDL_CONTROLLERAXISMOTION:
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP:
                    result = crustyvm_run(cvm, "event");
                    if(result < 0) {
                        fprintf(stderr, "Program reached an exception while "
                                        "running: %s\n",
                                crustyvm_statusstr(crustyvm_get_status(cvm)));
                        crustyvm_debugtrace(cvm, 0);
                        goto error4;
                    }
                    break;
                default:
                    break;
            }
        }
 
        result = crustyvm_run(cvm, "frame");
        if(result < 0) {
            fprintf(stderr, "Program reached an exception while "
                            "running: %s\n",
                    crustyvm_statusstr(crustyvm_get_status(cvm)));
            crustyvm_debugtrace(cvm, 0);
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
