#include <stdlib.h>
#include <string.h>
#include <SDL2.h>

#define DEFAULT_RATE (48000)
/* try to determine a sane size which is roughly half a frame long at 60 FPS. 48000 / 120 = 400, nearest power of two is 512, user can set more fragments if they need */
#define DEFAULT_FRAGMENT_SIZE (512)

#define LOG_PRINTF(SYNTH, FMT, ...) \
    (SYNTH)->synth_log_cb((SYNTH)->synth_log_priv, \
    FMT, \
    ##__VA_ARGS__)

#define SYNTH_STOPPED (0)
#define SYNTH_ENABLED (1)
#define SYNTH_RUNNING (2)

typedef int (*synth_frame_cb_t)(void *priv);
typedef void (*synth_log_cb_t)(void *priv, const char *fmt, ...);

/* most common formats */
typedef enum {
    SYNTH_TYPE_U8,
    SYNTH_TYPE_S16,
    SYNTH_TYPE_F32
} SynthImportType;

typedef struct {
    unsigned int rate;
    unsigned int fragmentsize;
    unsigned int fragments;
    unsigned int channels;
    SynthBuffer *channelbuffers;
    unsigned int readcursor;
    unsigned int writecursor;
    unsigned int bufferfilled;
    unsigned int buffersize;
    int underrun;
    unsigned int enable;
    int needconversion;
    SDL_AudioCVT converter;

    synth_frame_cb_t synth_frame_cb;
    void *synth_frame_priv;

    SDL_AudioCVT U8toF32;
    SDL_AudioCVT S16toF32;

    SynthBuffer *buffer;
    unsigned int buffersmem;

    synth_log_cb_t synth_log_cb;
    void *synth_log_priv;
} Synth;

typedef struct {
    float *data;
    unsigned int size;
    unsigned int ref;
} SynthBuffer;

typedef struct {
} SynthPlayer;

/* implement a simple but hardly transparent ring buffer.
 * Guarantee that only up to the cursor or the end of the buffer may be
 * consumed or written to at a time, requiring the buffer user to call once,
 * update, see if there's still samples needed or available, then call and
 * update a second time, then finally check again to know that it is truly
 * filled or emptied. */
/* synth_get_samples_needed is "public" since it's needed by the script to know
 * how much it should fill in */
unsigned int synth_get_samples_needed(Synth *s) {
    if(s->readcursor == s->writecursor) {
        if(s->bufferfilled == s->buffersize) {
            return(0);
        } else {
            return(s->buffersize - s->writecursor);
        }
    } else if(s->writecursor < s->readcursor) {
        return(s->readcursor - s->writecursor);
    } else { /* s->writecursor > s->readcursor */
        return(s->buffersize - s->writecursor);
    }
}

static void update_samples_needed(Synth *s, unsigned int added) {
    s->writecursor += added;
    if(s->writecursor >= s->buffersize) {
        s->writecursor -= s->buffersize;
    }
    s->bufferfilled += added;
}

static unsigned int get_samples_available(Synth *s) {
    if(s->readcursor == s->writecursor) {
        if(s->bufferfilled == s->buffersize) {
            return(s->buffersize);
        } else {
            return(s->buffersize - s->readcursor);
        }
    } else if(s->readcursor < s->writecursor) {
        return(s->writecursor - s->readcursor);
    } else { /* s->readcursor > s->writecursor */
        return(s->buffersize - s->readcursor);
    }
}

static void update_samples_available(Synth *s, unsigned int consumed) {
    s->readcursor += consumed;
    if(s->readcursor == s->buffersize) {
        s->readcursor = 0;
    }
    s->bufferfilled -= consumed;
}

/* big ugly, overcomplicated function, but hopefully it isolates most of the
 * complexity in one place. */
void synth_audio_cb(void *userdata, Uint8 *stream, int len) {
    Synth *s = (Synth *)userdata;
    unsigned int i, j;
    unsigned int available = get_samples_available(s);
    unsigned int todo;
    /* get number of samples */
    unsigned int length = len / (s->channels * sizeof(float));

    if(channels == 1) {
        todo = length > available ? available : length;
        /* convert in-place, because it can only be shrunken from 32 bits to
         * 16 bits, or just left as-is as 32 bits. */
        s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
        s->converter.len = todo * sizeof(float);
        /* ignore return value because the documentation indicates the only
         * fail state is that buf is NULL, which it won't be. */
        SDL_ConvertAudio(&(s->converter));
        /* copy what has been converted */
        memcpy(stream,
               &(s->channelbuffers[0].data[readcursor]),
               todo * SDL_AUDIO_BITSIZE(s->converter.dst_format) / 8);
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            /* more to do, so do the same thing again */
            available = get_samples_available(s);
            todo = length > available ? available : length;
            s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
            s->converter.len = todo * sizeof(float);
            SDL_ConvertAudio(&(s->converter));
            memcpy(stream,
                   &(s->channelbuffers[0].data[readcursor]),
                   todo * SDL_AUDIO_BITSIZE(s->converter.dst_format) / 8);
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                /* SDL audio requested more, but there is no more,
                 * underrun. */
                s->underrun = 0;
            }
        }
    } else if(channels == 2) { /* hopefully faster stereo code path */
        /* much like mono, just do it to both channels and zipper them in to
         * the output */
        todo = length > available ? available : length;
        s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
        s->converter.len = todo * sizeof(float);
        SDL_ConvertAudio(&(s->converter));
        s->converter.buf = &(s->channelbuffers[1].data[readcursor]);
        SDL_ConvertAudio(&(s->converter));
        /* this is probably slow */
        if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
            for(i = 0; i < todo / sizeof(Sint32); i++) {
                ((Sint32 *)stream)[i * 2] =
                    ((Sint32 *)(s->channelbuffers[0].data))[readcursor + i];
                ((Sint32 *)stream)[i * 2 + 1] =
                    ((Sint32 *)(s->channelbuffers[1].data))[readcursor + i];
            }
        } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
            for(i = 0; i < todo / sizeof(Sint16); i++) {
                ((Sint16 *)stream)[i * 2] =
                    ((Sint16 *)(s->channelbuffers[0].data))[readcursor + i];
                ((Sint16 *)stream)[i * 2 + 1] =
                    ((Sint16 *)(s->channelbuffers[1].data))[readcursor + i];
            }
        } else { /* 8 */
            for(i = 0; i < todo; i++) {
                stream[i * 2] =
                    (char *)(s->channelbuffers[0].data)[readcursor + i];
                stream[i * 2 + 1] =
                    (char *)(s->channelbuffers[1].data)[readcursor + i];
            }
        }
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            todo = length > available ? available : length;
            s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
            s->converter.len = todo * sizeof(float);
            SDL_ConvertAudio(&(s->converter));
            s->converter.buf = &(s->channelbuffers[1].data[readcursor]);
            SDL_ConvertAudio(&(s->converter));
            if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
                for(i = 0; i < todo / sizeof(Sint32); i++) {
                    ((Sint32 *)stream)[i * 2] =
                        ((Sint32 *)(s->channelbuffers[0].data))[readcursor + i];
                    ((Sint32 *)stream)[i * 2 + 1] =
                        ((Sint32 *)(s->channelbuffers[1].data))[readcursor + i];
                }
            } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
                for(i = 0; i < todo / sizeof(Sint16); i++) {
                    ((Sint16 *)stream)[i * 2] =
                        ((Sint16 *)(s->channelbuffers[0].data))[readcursor + i];
                    ((Sint16 *)stream)[i * 2 + 1] =
                        ((Sint16 *)(s->channelbuffers[1].data))[readcursor + i];
                }
            } else { /* 8 */
                for(i = 0; i < todo; i++) {
                    stream[i * 2] =
                        (char *)(s->channelbuffers[0].data)[readcursor + i];
                    stream[i * 2 + 1] =
                        (char *)(s->channelbuffers[1].data)[readcursor + i];
                }
            }
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                s->underrun = 0;
            }
        }
    } else { /* unlikely case it's multichannel surround ... */
        /* much like stereo, but use a loop because i don't feel like making
         * a bunch of unrolled versions of this unless surround sound becomes
         * something frequently used with this.. */
        todo = length > available ? available : length;
        s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
        s->converter.len = todo * sizeof(float);
        SDL_ConvertAudio(&(s->converter));
        s->converter.buf = &(s->channelbuffers[1].data[readcursor]);
        SDL_ConvertAudio(&(s->converter));
        /* this is probably very slow */
        if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
            for(i = 0; i < todo / sizeof(Sint32); i++) {
                for(j = 0; j < s->channels; j++) {
                    ((Sint32 *)stream)[i * s->channels + j] =
                        ((Sint32 *)(s->channelbuffers[j].data))[readcursor + i];
                }
            }
        } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
            for(i = 0; i < todo / sizeof(Sint16); i++) {
                for(j = 0; j < s->channels; j++) {
                    ((Sint16 *)stream)[i * s->channels + j] =
                        ((Sint16 *)(s->channelbuffers[j].data))[readcursor + i];
                }
            }
        } else { /* 8 */
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    stream[i * s->channels + j] =
                        ((char *)(s->channelbuffers[j].data))[readcursor + i];
                }
            }
        }
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            todo = length > available ? available : length;
            s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
            s->converter.len = todo * sizeof(float);
            SDL_ConvertAudio(&(s->converter));
            s->converter.buf = &(s->channelbuffers[1].data[readcursor]);
            SDL_ConvertAudio(&(s->converter));
            if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
                for(i = 0; i < todo / sizeof(Sint32); i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((Sint32 *)stream)[i * s->channels + j] =
                            ((Sint32 *)(s->channelbuffers[j].data))[readcursor + i];
                    }
                }
            } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
                for(i = 0; i < todo / sizeof(Sint16); i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((Sint16 *)stream)[i * s->channels + j] =
                            ((Sint16 *)(s->channelbuffers[j].data))[readcursor + i];
                    }
                }
            } else { /* 8 */
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        stream[i * s->channels + j] =
                            ((char *)(s->channelbuffers[j].data))[readcursor + i];
                    }
                }
            }
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                s->underrun = 0;
            }
        }
    }
}

Synth *synth_new(synth_frame_cb_t synth_frame_cb,
                 void *synth_frame_priv,
                 synth_log_cb_t synth_log_cb,
                 void *synth_log_priv) {
    SDL_AudioSpec desired, obtained;
    Synth *s;

    s = malloc(sizeof(Synth));
    if(s == NULL) {
        synth_log_cb(synth_log_priv, "Failed to allocate synth.\n");
        return(NULL);
    }

    s->synth_log_cb = synth_log_cb;
    s->synth_log_priv = synth_log_priv;

    desired.freq = DEFAULT_RATE;
    /* may as well use this as the desired output format if the internal format
     * will be F32 anyway, but build a converter just in case it's needed. */
    desired.format = AUDIO_F32SYS;
    /* we _really_ want stereo but mono will work fine.  Surround is ...
     * technically supported but it'd probably be uselessly slow. */
    desired.channels = 2;
    desired.samples = DEFAULT_FRAGMENT_SIZE;
    desired.callback = synth_audio_cb;
    desired.userdata = s;
    if(SDL_OpenAudio(&desired, &obtained) < 0) {
        fprintf(stderr, "Failed to open SDL audio.\n");
        free(s);
    }

    if(SDL_AUDIO_BITSIZE(obtained.format) != 32 ||
       SDL_AUDIO_BITSIZE(obtained.format) != 16 ||
       SDL_AUDIO_BITSIZE(obtained.format) != 8) {
        fprintf(stderr, "Unsupported format size: %d.\n",
                        SDL_AUDIO_BITSIZE(obtained.format));
        SDL_CloseAudio();
        free(s);
    }

    /* just use the obtained spec for frequency but try to convert the format.
     * Specify mono because the buffers are separate until the end. */
    s->needconversion = SDL_BuildCVT(&(s->converter),
                                     desired.format,
                                     1,
                                     obtained.freq,
                                     obtained.format,
                                     1,
                                     obtained.freq);
    if(s->needconversion < 0) {
        fprintf(stderr, "Can't create audio output converter.\n");
        SDL_CloseAudio();
        free(s);
    }

    /* create converters now for allowing import later */
    if(SDL_BuildCVT(&(s->U8toF32),
                    AUDIO_U8,
                    1,
                    obtained.freq,
                    AUDIO_F32SYS,
                    1,
                    obtained.freq) < 0) {
        fprintf(stderr, "Failed to build U8 import converter.\n");
        SDL_CloseAudio();
        free(s);
    }
    if(SDL_BuildCVT(&(s->S16toF32),
                    AUDIO_S16SYS,
                    1,
                    obtained.freq,
                    AUDIO_F32SYS,
                    1,
                    obtained.freq) < 0) {
        fprintf(stderr, "Failed to build S16 import converter.\n");
        SDL_CloseAudio();
        free(s);
    }

    s->rate = obtained.rate;
    s->fragmentsize = obtained.samples;
    s->fragments = 0;
    s->channels = obtained.channels;
    /* Won't know what size to allocate to them until the user has set a number of fragments */
    s->channelbuffers = NULL;
    s->buffer = NULL;
    s->buffersmem = 0;
    s->underrun = 0;
    s->enable = 0;
    s->synth_frame_cb = synth_frame_cb;
    s->synth_frame_priv = synth_frame_priv;

    return(s);
}

void synth_free(Synth *s) {
    unsigned int i;

    SDL_CloseAudio();

    if(s->buffer != NULL) {
        free(s->buffer);
    }

    if(s->channelbuffers != NULL) {
        for(i = 0; i < s->channels; i++) {
            if(s->channelbuffers[i].data != NULL) {
                free(s->channelbuffers[i].data);
            }
        }
        free(s->channelbuffers);
    }

    free(s);
}

unsigned int synth_get_rate(Synth *s) {
    return(s->rate);
}

unsigned int synth_get_channels(Synth *s) {
    return(s->channels);
}

unsigned int synth_get_fragment_size(Synth *s) {
    return(s->fragmentsize);
}

int synth_has_underrun(Synth *s) {
    if(s->underrun == 0) {
        return(0);
    }

    s->underrun = 0;
    return(1);
}

int audio_set_enabled(Synth *s, int enabled) {
    if(enabled == 0) {
        SDL_PauseAudio(1);
        s->enabled = 0;
    } else {
        if(s->channelbuffers == NULL) {
            fprintf(stderr, "Audio buffers haven't been set up.  Set fragment "
                            "count first.\n");
            return(-1);
        }
        /* signal to enable */
        s->enabled = SYNTH_ENABLED;
    }

    return(0);
}

int synth_frame(Synth *s) {
    unsigned int needed;

    if(enabled == SYNTH_ENABLED) {
        s->bufferfilled = 0;
        s->readcursor = 0;
        s->writecursor = 0;
        s->underrun = 0;
        return(s->synth_frame_cb(synth_frame_priv));
        update_samples_needed(s->buffersize);
        s->enabled == SYNTH_RUNNING;
        SDL_PauseAudio(0);
    } else if(enabled == SYNTH_RUNNING) {
        needed = synth_get_samples_needed(s);
        if(needed > 0) {
            SDL_LockAudio();
            return(s->synth_frame_cb(synth_frame_priv));
            update_samples_needed(needed);
            /* get_samples_needed() returns only the remaining contiguous
             * buffer, so it may need to be called twice */
            if(needed > 0) {
                return(s->synth_frame_cb(synth_frame_priv));
                update_samples_needed(needed);
            }
            SDL_UnlockAudio();
        }
    }

    return(0);
}

int synth_set_fragments(Synth *s,
                        unsigned int fragments) {
    unsigned int i;

    if(fragments == 0) {
        return(-1);
    }
    
    if(s->enabled != SYNTH_STOPPED) {
        fprintf(stderr, "Synth must be stopped before changing fragment size.\n");
        return(-1);
    }

    if(s->channelbuffers == NULL) {
        s->channelbuffers = malloc(sizeof(SynthBuffer * s->channels));
        if(s->channelbuffers == NULL) {
            fprintf(stderr, "Failed to allocate channel buffers.\n");
            return(-1);
        }
        for(i = 0; i < s->channels; i++) {
            s->channelbuffers[i].type = s->type;
            s->channelbuffers[i].data = NULL;
            s->channelbuffers[i].size = 0;
        }
    }

    s->buffersize = s->fragmentsize * fragments;
    for(i = 0; i < s->channels; i++) {
        if(s->channelbuffer[i].data != NULL) {
            free(s->channelbuffer[i].data);
        }
        s->channelbuffer[i].data =
            malloc(sizeof(float) * s->buffersize);
        if(s->channelbuffer[i].data == NULL) {
            fprintf(stderr, "Failed to allocate channel buffer memory.\n");
            for(i -= 1; i >= 0; i--) {
                free(s->channelbuffer[i].data);
                s->channelbuffer[i].data = NULL;
            }
            return(-1);
        }
        memset(s->channelbuffer[i].data, 0, sizeof(float) * s->buffersize);
    }

    s->channelbuffer[i].size = s->buffersize;

    return(0);
}

int synth_add_buffer(Synth *s,
                     SynthImportType type,
                     void *data,
                     unsigned int size) {
    unsigned int i, j;
    SynthBuffer *temp;

    /* first loaded buffer, so do some initial setup */
    if(s->buffersmem == 0) {
        s->buffer = malloc(sizeof(SynthBuffer));
        if(s->buffer == NULL) {
            LOG_PRINTF(s, "Failed to allocate buffers memory.\n");
            return(-1);
        }
        s->buffersmem = 1;
        s->buffer[0].type = type;
        s->buffer[0].size = size;
        s->buffer[0].data = malloc(size * sizeof(float));
        if(s->buffer[0].data == NULL) {
            LOG_PRINTF(s, "Failed to allocate buffer data memory.\n");
            return(-1);
        }
        if(data != NULL) {
            if(type == SYNTH_TYPE_U8) {
                memcpy(s->buffer[0].data, data, size * sizeof(Uint8));
                s->U8toF32.buf = s->buffer[0].data;
                s->U8toF32.len = size;
                SDL_ConvertAudio(s->U8toF32);
            } else if(type == SYNTH_TYPE_S16) {
                memcpy(s->buffer[0].data, data, size * sizeof(Sint16));
                s->S16toF32.buf = s->buffer[0].data;
                s->S16toF32.len = size;
                SDL_ConvertAudio(s->S16toF32);
            } else { /* F32 */
                memcpy(s->buffer[i].data, data, size * sizeof(float));
            }
        } else {
            memset(s->buffer[0].data, 0, size * sizeof(float));
        }
        s->buffer[0].ref = 0;
        return(s->channels);
    }

    /* find first NULL buffer and assign it */
    for(i = 0; i < s->buffersmem; i++) {
        if(s->buffer[i].size == 0) {
            s->buffer[i].type = type;
            s->buffer[i].size = size;
            s->buffer[i].data = malloc(size * sizeof(float));
            if(s->buffer[i].data == NULL) {
                LOG_PRINTF(s, "Failed to allocate buffer data memory.\n");
                return(-1);
            }
            if(data != NULL) {
                if(type == SYNTH_TYPE_U8) {
                    memcpy(s->buffer[i].data, data, size * sizeof(Uint8));
                    s->U8toF32.buf = s->buffer[i].data;
                    s->U8toF32.len = size;
                    SDL_ConvertAudio(s->U8toF32);
                } else if(type == SYNTH_TYPE_S16) {
                    memcpy(s->buffer[i].data, data, size * sizeof(Sint16));
                    s->S16toF32.buf = s->buffer[i].data;
                    s->S16toF32.len = size;
                    SDL_ConvertAudio(s->S16toF32);
                } else { /* F32 */
                    memcpy(s->buffer[i].data, data, size * sizeof(float));
                }
            } else {
                memset(s->buffer[i].data, 0, size * sizeof(float));
            }
            s->buffer[i].ref = 0;
            return(s->channels + i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(s->buffer,
                   sizeof(SyntbBuffer) * s->buffersmem * 2);
    if(temp == NULL) {
        LOG_PRINTF(s, "Failed to allocate buffers memory.\n");
        return(-1);
    }
    s->buffer = temp;
    s->buffersmem *= 2;
    s->buffer[i].type = type;
    s->buffer[i].size = size;
    s->buffer[i].data = malloc(size * sizeof(float));
    if(s->buffer[i].data == NULL) {
        LOG_PRINTF(s, "Failed to allocate buffer data memory.\n");
        return(-1);
    }
    if(data != NULL) {
        if(type == SYNTH_TYPE_U8) {
            memcpy(s->buffer[i].data, data, size * sizeof(Uint8));
            s->U8toF32.buf = s->buffer[i].data;
            s->U8toF32.len = size;
            SDL_ConvertAudio(s->U8toF32);
        } else if(type == SYNTH_TYPE_S16) {
            memcpy(s->buffer[i].data, data, size * sizeof(Sint16));
            s->S16toF32.buf = s->buffer[i].data;
            s->S16toF32.len = size;
            SDL_ConvertAudio(s->S16toF32);
        } else { /* F32 */
            memcpy(s->buffer[i].data, data, size * sizeof(float));
        }
    } else {
        memset(s->buffer[i].data, 0, size * sizeof(float));
    }
    s->buffer[i].ref = 0;

    /* initialize empty excess buffers as empty */
    for(j = i + 1; j < s->buffersmem; j++) {
        s->buffer[j].size = 0;
    }
 
    return(s->channels + i);
}

int synth_free_buffer(Synth *s, unsigned int index) {
    index -= s->channels;

    if(index > s->buffersmem ||
       s->buffer[index].size == 0 ||
       s->buffer[index].ref != 0) {
        fprintf(stderr, "Invalid buffer index or buffer in use.\n");
        return(-1);
    }

    free(s->buffer[index].data);
    s->buffer[index].size = 0;

    return(0);
}
