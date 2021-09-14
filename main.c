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

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>
#include <SDL.h>

#include "crustygame.h"
#include "crustyvm.h"
#include "tilemap.h"
#include "callbacks.h"
#include "xdg.h"

/* initial settings */
#define WINDOW_TITLE    "CrustyGame"
#define WINDOW_WIDTH    (640)
#define WINDOW_HEIGHT   (480)

const char META_PREFIX[] = ";crustygame ";
const char SAVE_SIZE_PREFIX[] = "save:";
const char SAVE_PATH_DIR[] = "/crustygame saves/";
const char SAVE_PATH_SUFFIX[] = ".sav";
#define SAVE_FILL_BUFFER_SIZE (64 * 1024)

CrustyGame state;

int initialize_SDL(SDL_Window **win,
                   SDL_Renderer **renderer,
                   Uint32 *format) {
    unsigned int drivers;
    int nameddrv, bestdrv, softdrv, selectdrv;
    int selectfmt;
    Uint32 namedfmt, bestfmt, softfmt;
    unsigned int i, j;
    SDL_RendererInfo driver;

    /* SDL/Windows/Render initialization stuff */
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "Failed to initialize SDL: %s\n",
                SDL_GetError());
        return(-1);
    }

    /* try to determine what driver to use based on available
     * features and prioritize an accelerated driver then fall back to
     * the software driver */
    drivers = SDL_GetNumRenderDrivers();
    fprintf(stderr, "Video Drivers: %d\n", drivers);

    nameddrv = -1;
    namedfmt = SDL_PIXELFORMAT_UNKNOWN;
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
                    softfmt = driver.texture_formats[j];
                    softdrv = i;
                    break;
                }
            }
        } else if((strcmp(driver.name, "direct3d11") == 0 ||
                  strncmp(driver.name, "opengles", 8) == 0 ||
                  strcmp(driver.name, "metal") == 0) &&
                  nameddrv == -1) {
            /* prefer direct3d 11 or opengles or metal for better blend mode support */
            for(j = 0; j < driver.num_texture_formats; j++) {
                if(SDL_BITSPERPIXEL(driver.texture_formats[j]) >= 24) {
                    namedfmt = driver.texture_formats[j];
                    nameddrv = i;
                    break;
                }
            }
        } else if((driver.flags & SDL_RENDERER_ACCELERATED) &&
                  (driver.flags & SDL_RENDERER_TARGETTEXTURE) &&
                  bestdrv == -1) {
            for(j = 0; j < driver.num_texture_formats; j++) {
                if(SDL_BITSPERPIXEL(driver.texture_formats[j]) >= 24) {
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
                    driver.texture_formats[j],
                    SDL_GetPixelFormatName(driver.texture_formats[j]));
        }
        fprintf(stderr, "\n");
        fprintf(stderr, "Max Texture Size: %d x %d\n",
                driver.max_texture_width,
                driver.max_texture_height);
    }

    if(nameddrv != -1) {
        bestfmt = namedfmt;
        bestdrv = nameddrv;
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
        fprintf(stderr, "Selecting driver %d.\n",
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

int update_settings(char *program, unsigned long len, unsigned int *savesize) {
    unsigned long i;
    unsigned long linelen;
    char held;
    char *end;
    unsigned int value;

    *savesize = 0;

    if(len > sizeof(META_PREFIX) - 1 &&
       strncmp(program, META_PREFIX, sizeof(META_PREFIX) - 1) == 0) {
        /* find end of line */
        for(i = 0; i < len; i++) {
            if(program[i] == '\r' || program[i] == '\n') {
                break;
            }
        }
        if(i == len) {
            fprintf(stderr, "Found end of program searching for end of line.\n");
            return(-1);
        }
        linelen = i;
        held = program[linelen];
        /* make strtol safe to use */
        program[linelen] = '\0';

        for(i = sizeof(META_PREFIX) - 1; i < linelen; i++) {
            if(linelen - i >= sizeof(SAVE_SIZE_PREFIX) - 1 &&
               strncmp(&(program[i]),
                       SAVE_SIZE_PREFIX,
                       sizeof(SAVE_SIZE_PREFIX) - 1) == 0) {
                i += sizeof(SAVE_SIZE_PREFIX) - 1;

                value = strtol(&(program[i]), &end, 0);
                if(end == &(program[i]) ||
                   (*end != ' ' &&
                    *end != '\t' &&
                    *end != '\0')) {
                    fprintf(stderr, "Save file size was not a number.\n");
                    goto failure;
                }
                *savesize = value;

                i += end - &(program[i]);
            } else if(program[i] == ' ' ||
                      program[i] == '\t') {
                continue;
            } else {
                for(value = i; value < linelen; value++) {
                    if(program[value] == ' ' ||
                       program[value] == '\t' ||
                       program[value] == '\0') {
                        break;
                    }
                }
                fprintf(stderr, "Invalid option string: ");
                fwrite(&(program[i]), value - i, 1, stderr);
                fprintf(stderr, "\n");
                goto failure;
            }
        }

        program[linelen] = held;
    }

    return(0);
failure:
    program[linelen] = held;

    return(-1);
}

/* must be an absolute path including with a file of some sort, at this point
 * this is guaranteed because fullpath came from crustyvm_open_file, which
 * determines if the file actually existed and is a regular file at all before
 * returning a path.
 * Only path needs to be freed. */
char *split_path_and_filename(const char *fullpath, char **filename) {
    char *slash;
    unsigned int pathlen;
    unsigned int slashpos;
    char *path;

    slash = strrchr(fullpath, '/');
    if(slash == NULL) {
        /* wasn't fed an absolute path for sure */
        return(NULL);
    }
    slashpos = slash - fullpath;
    pathlen = strlen(fullpath);
    if(slashpos == pathlen) {
        /* string ends in /, meaning it'd certainly be a directory already */
        return(NULL);
    }
    path = malloc(pathlen + 1);
    if(path == NULL) {
        return(NULL);
    }
    memcpy(path, fullpath, pathlen + 1);
    path[slashpos] = '\0';

    *filename = &(path[slashpos + 1]);
    return(path);
}

/* must accept only a filename, no path parts */
void trim_extension(char *filename) {
    char *dot;

    dot = strrchr(filename, '.');
    if(dot == NULL || dot == filename) {
        return;
    }

    *dot = '\0';
}

/* check to see if a directory exists and if not, try to recursively create
 * directories until it does.
 * must be an absolute path */
int is_existing_dir(const char *dir) {
    unsigned int len = strlen(dir);
    char path[len+1];
    unsigned int i;
    struct stat filestat;

    memcpy(path, dir, len + 1);

    /* don't process the first leading / */
    for(i = 1; i < len; i++) {
        if(path[i] == '/') {
            path[i] = '\0';
            if(stat(path, &filestat) == 0) {
                if(!S_ISDIR(filestat.st_mode)) {
                    /* something not a directory in the way */
                    return(0);
                }
            } else {
                /* directory doesn't exist, try to create it */
                if(mkdir(path, 0755) < 0) {
                    return(0);
                }
            }
            path[i] = '/';
        }
    }

    /* if there's no trailing /, make sure the full path is tried */
    if(path[len - 1] != '/') {
        if(stat(path, &filestat) == 0) {
            if(!S_ISDIR(filestat.st_mode)) {
                /* something not a directory in the way */
                return(0);
            }
        } else {
            /* directory doesn't exist, try to create it */
            if(mkdir(path, 0755) < 0) {
                return(0);
            }
        }
    }

    return(1);
}

FILE *create_save_file(const char *fullpath, unsigned int size) {
    char *path;
    char *filename;
    char *xdgdirs;
    unsigned int xdgcount;
    unsigned int xdgcountbackup;
    char *curpath;
    char savepath[PATH_MAX];
    char savename[PATH_MAX];

    FILE *savefile;
    char buffer[SAVE_FILL_BUFFER_SIZE];
    int need_fill;
    int this_fill;
    struct stat filestat;

    path = split_path_and_filename(fullpath, &filename);
    if(path == NULL) {
        fprintf(stderr, "Failed to split path and filename parts.\n");
        return(NULL);
    }
    trim_extension(filename);

    xdgcount = get_xdg_home_dirs(&xdgdirs, path);
    xdgcountbackup = xdgcount;
    /* check for an existing save file */
    curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
    while(curpath != NULL) {
        if(snprintf(savepath, PATH_MAX, "%s%s", curpath, SAVE_PATH_DIR) >= PATH_MAX) {
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }
        if(snprintf(savename, PATH_MAX, "%s%s%s", savepath, filename, SAVE_PATH_SUFFIX) >= PATH_MAX) {
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }

        if(stat(savename, &filestat) == 0) {
            if(S_ISREG(filestat.st_mode)) {
                if(filestat.st_size < size) {
                    /* old save file is smaller than requested size, grow it */
                    savefile = fopen(savename, "a");
                    if(savefile == NULL) {
                        fprintf(stderr, "Failed to open save file: %s\n", savename);
                        curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
                        continue;
                    }

                    memset(buffer, 0, SAVE_FILL_BUFFER_SIZE);
                    for(need_fill = filestat.st_size - size;
                        need_fill > 0;
                        need_fill -= SAVE_FILL_BUFFER_SIZE) {

                        this_fill = (need_fill > SAVE_FILL_BUFFER_SIZE) ?
                                    SAVE_FILL_BUFFER_SIZE :
                                    need_fill;
                        if(fwrite(buffer, 1, this_fill, savefile) <
                           (unsigned int)this_fill) {
                            fprintf(stderr, "Failed to fill save file.\n");
                            fclose(savefile);
                            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
                            continue;
                        }
                    }
                    fclose(savefile);
                }

                /* open the file for read+write */
                savefile = fopen(savename, "r+");
                if(savefile == NULL) {
                    fprintf(stderr, "Failed to open save file: %s\n", savename);
                    curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
                    continue;
                }
                fprintf(stderr, "Save file found at: %s\n", savename);

                free(xdgdirs);
                free(path);

                return(savefile);
            } else {
                fprintf(stderr, "Save file exists but is not a regular file: %s\n", savename);
                curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
                continue;
            }
        } else {
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }
    }

    /* Try to create a new save file. */
    xdgcount = xdgcountbackup;
    curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
    while(curpath != NULL) {
        if(snprintf(savepath, PATH_MAX, "%s%s", curpath, SAVE_PATH_DIR) >= PATH_MAX) {
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }
        if(snprintf(savename, PATH_MAX, "%s%s%s", savepath, filename, SAVE_PATH_SUFFIX) >= PATH_MAX) {
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }

        if(is_existing_dir(savepath) == 0) {
            fprintf(stderr, "Directory doesn't exist and couldn't be created: %s\n", savepath);
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }

        savefile = fopen(savename, "w+");
        if(savefile == NULL) {
            fprintf(stderr, "Failed to open save file: %s\n", savename);
            curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
            continue;
        }

        memset(buffer, 0, SAVE_FILL_BUFFER_SIZE);
        for(need_fill = size; need_fill > 0; need_fill -= SAVE_FILL_BUFFER_SIZE) {
            this_fill = (need_fill > SAVE_FILL_BUFFER_SIZE) ?
                        SAVE_FILL_BUFFER_SIZE :
                        need_fill;
            if(fwrite(buffer, 1, this_fill, savefile) < 
               (unsigned int)this_fill) {
                fprintf(stderr, "Failed to fill save file.\n");
                fclose(savefile);
                curpath = get_next_xdg_home_dir(xdgdirs, &xdgcount);
                continue;
            }
        }
        rewind(savefile);
        fprintf(stderr, "Save file createed at: %s\n", savename);

        free(xdgdirs);
        free(path);

        return(savefile);
    }

    free(xdgdirs);
    free(path);

    return(NULL);
}

int audio_frame_cb(void *priv) {
    CrustyGame *state = priv;

    if(crustyvm_run(state->cvm, "audio") < 0) {
        fprintf(stderr, "Program reached an exception while running: "
                        "%s\n",
                crustyvm_statusstr(crustyvm_get_status(state->cvm)));
        crustyvm_debugtrace(state->cvm, 1);
        return(-1);
    }

    return(0);
}

int notify_event(CrustyVM *cvm) {
    int result = crustyvm_run(cvm, "event");
    if(result < 0) {
        fprintf(stderr, "Program reached an exception while "
                        "running: %s\n",
                crustyvm_statusstr(crustyvm_get_status(cvm)));
        crustyvm_debugtrace(cvm, 0);
        return(-1);
    }

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
    state.buffer = NULL;
    state.size = 0;
    state.ret = 0;
    state.mouseCaptured = 0;
    state.savesize = 0;
    state.savefile = NULL;

    /* CrustyVM stuff */
    unsigned int i;
    const char *filename = NULL;
    char *fullpath;
    unsigned int arglen;
    char *equals;
    char *temp;
    char **tempa;
    char **var = NULL;
    char **value = NULL;
    unsigned int vars = 0;

    FILE *in = NULL;
    char *program = NULL;
    long len;
    int result;

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
                        goto error_arglist;
                    }
                    var = tempa;
                    tempa = realloc(value, sizeof(char *) * (vars + 1));
                    if(tempa == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for values list.\n");
                        goto error_arglist;
                    }
                    value = tempa;
                    /* difference from start, take away "-D", add
                     * space for '\0' */
                    temp = malloc(equals - argv[i] - 2 + 1);
                    if(temp == NULL) {
                        fprintf(stderr, "Failed to allocate memory "
                                        "for var.\n");
                        goto error_arglist;
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
                        goto error_arglist;
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
        fprintf(stderr, "USAGE: %s [(<filename>|-D<var>=<value>) ...] [-- <filename>]\n", argv[0]);
        goto error_arglist;
    }

    fullpath = NULL;
    in = crustyvm_open_file(filename, &fullpath, vprintf_cb, stderr);
    if(in == NULL) {
        fprintf(stderr, "Failed to open file %s.\n", filename);
        goto error_arglist;
    }

    if(fseek(in, 0, SEEK_END) < 0) {
       fprintf(stderr, "Failed to seek to end of file.\n");
       goto error_infile;
    }

    len = ftell(in);
    if(len < 0) {
        fprintf(stderr, "Failed to get file length.\n");
        goto error_infile;
    }
    rewind(in);

    program = malloc(len);
    if(program == NULL) {
        goto error_infile;
    }

    if(fread(program, 1, len, in) < (unsigned long)len) {
        fprintf(stderr, "Failed to read file.\n");
        goto error_infile;
    }

    fclose(in);
    in = NULL;
    if(update_settings(program, len, &(state.savesize)) < 0) {
        goto error_infile;
    }
    if(state.savesize > 0) {
        state.savefile = create_save_file(fullpath, state.savesize);
        if(state.savefile == NULL) {
            fprintf(stderr, "Couldn't create save file.\n");
            goto error_infile;
        }
    } else {
        state.savefile = NULL;
    }

    state.cvm = crustyvm_new(filename, fullpath, 
                       program, len,
                       CRUSTY_FLAG_DEFAULTS,
                       0,
                       cb, CRUSTYGAME_CALLBACKS,
                       (const char **)var, (const char **)value, vars,
                       vprintf_cb, stderr);
    if(state.cvm == NULL) {
        fprintf(stderr, "Failed to load program.\n");
        goto error_infile;
    }
    /* some early cleanup of things we're done with */
    free(program);
    program = NULL;
    CLEAN_ARGS
    fprintf(stderr, "Program loaded.\n");

    fprintf(stderr, "Token memory size: %u\n",
                    crustyvm_get_tokenmem(state.cvm));
    fprintf(stderr, "Stack size: %u\n",
                    crustyvm_get_stackmem(state.cvm));

    if(initialize_SDL(&(state.win),
                      &(state.renderer),
                      &format) < 0) {
        fprintf(stderr, "Failed to initialize SDL.\n");
        goto error_cvm;
    }

    /* initialize the layerlist */
    state.ll = layerlist_new(state.renderer,
                             format,
                             vprintf_cb,
                             stderr);
    if(state.ll == NULL) {
        fprintf(stderr, "Failed to create layerlist.\n");
        goto error_sdl;
    }

#if 0
    /* initialize the audio */
    state.s = synth_new(audio_frame_cb,
                        &state,
                        vprintf_cb,
                        stderr);
    if(state.s == NULL) {
        fprintf(stderr, "Failed to create synth.\n");
        goto error_ll;
    }
#endif

    /* seed random */
    srand(time(NULL));

    /* init may flag program quit due to error */
    state.running = 1;
    /* call program init */
    result = crustyvm_run(state.cvm, "init");
    if(result < 0) {
        fprintf(stderr, "Program reached an exception while running: "
                        "%s\n",
                crustyvm_statusstr(crustyvm_get_status(state.cvm)));
        crustyvm_debugtrace(state.cvm, 1);
        goto error_synth;
    }

    while(state.running) {
        if(SDL_SetRenderDrawColor(state.renderer, 0, 0, 0, SDL_ALPHA_OPAQUE) < 0) {
            fprintf(stderr, "Failed to set render draw color.\n");
            goto error_synth;
        } 

        if(SDL_RenderClear(state.renderer) < 0) {
            fprintf(stderr, "Failed to clear screen.\n");
            goto error_synth;
        }

        /* needs to be transparent so tilemap updates work */
        if(SDL_SetRenderDrawColor(state.renderer,
                                  0, 0, 0,
                                  SDL_ALPHA_TRANSPARENT) < 0) {
            fprintf(stderr, "Failed to set render draw color.\n");
            goto error_synth;
        } 

        while(state.running && SDL_PollEvent(&(state.lastEvent))) {
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
                    if(((SDL_KeyboardEvent *)&(state.lastEvent))->repeat) {
                        continue;
                    }
                    if(notify_event(state.cvm) < 0) {
                        goto error_synth;
                    }
                    break;
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
                    if(notify_event(state.cvm) < 0) {
                        goto error_synth;
                    }
                    break;
                default:
                    break;
            }
        }

#if 0
        if(synth_frame(state.s) < 0) {
            fprintf(stderr, "Audio failed.\n");
            goto error_synth;
        }
#endif

        result = crustyvm_run(state.cvm, "frame");
        if(result < 0) {
            fprintf(stderr, "Program reached an exception while "
                            "running: %s\n",
                    crustyvm_statusstr(crustyvm_get_status(state.cvm)));
            crustyvm_debugtrace(state.cvm, 0);
            goto error_synth;
        }

        SDL_RenderPresent(state.renderer);
    }

    fprintf(stderr, "Program completed successfully.\n");
/*
    synth_free(state.s);
*/
    layerlist_free(state.ll);

    SDL_DestroyWindow(state.win);
    SDL_Quit();

    crustyvm_free(state.cvm);

    exit(EXIT_SUCCESS);

error_synth:
/*
    synth_free(state.s);
*/
error_ll:
    layerlist_free(state.ll);
error_sdl:
    SDL_DestroyWindow(state.win);
    SDL_Quit();
error_cvm:
    crustyvm_free(state.cvm);
error_infile:
    if(program != NULL) {
        free(program);
    }

    if(state.savefile != NULL) {
        fclose(state.savefile);
    }

    if(in != NULL) {
        fclose(in);
    }

    free(fullpath);
error_arglist:
    CLEAN_ARGS

    return(EXIT_FAILURE);
}
