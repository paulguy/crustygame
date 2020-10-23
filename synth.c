#include <stdlib.h>
#include <string.h>
#include <SDL2.h>

#include "synth.h"

#define DEFAULT_RATE (48000)
/* try to determine a sane size which is roughly half a frame long at 60 FPS. 48000 / 120 = 400, nearest power of two is 512, user can set more fragments if they need */
#define DEFAULT_FRAGMENT_SIZE (512)

#define LOG_PRINTF(SYNTH, FMT, ...) \
    (SYNTH)->synth_log_cb((SYNTH)->synth_log_priv, \
    FMT, \
    ##__VA_ARGS__)

typedef enum {
    SYNTH_STOPPED,
    SYNTH_ENABLED,
    SYNTH_RUNNING
} SynthState;

typedef struct {
    float *data;
    unsigned int size;
    unsigned int ref;
} SynthBuffer;

typedef enum {
    SYNTH_OUTPUT_REPLACE,
    SYNTH_OUTPUT_ADD
} SynthOutputOperation;

typedef enum {
    SYNTH_VOLUME_CONSTANT,
    SYNTH_VOLUME_SOURCE
} SynthVolumeMode;

typedef enum {
    SYNTH_SPEED_CONSTANT,
    SYNTH_SPEED_SOURCE
} SynthSpeedMode;

typedef enum {
    SYNTH_MODE_ONCE,
    SYNTH_MODE_LOOP,
    SYNTH_MODE_PINGPONG,
    SYNTH_MODE_PHASE_SOURCE
} SynthPlayerMode;

typedef struct {
    unsigned int inBuffer;
    unsigned int outBuffer;

    float inPos;
    unsigned int outPos;

    SynthOutputOperation outOp;

    SynthVolumeMode volMode;
    float volume;
    unsigned int volBuffer;
    unsigned int volPos;
    float volScale;

    SynthPlayerMode playMode;
    unsigned int loopStart;
    unsigned int loopEnd;
    unsigned int phaseBuffer;
    unsigned int phasePos;

    SynthSpeedMode speedMode;
    float speed;
    unsigned int speedBuffer;
    unsigned int speedPos;
    float speedScale;
} SynthPlayer;

typedef struct {
} SynthEnvelope;

typedef struct {
} SynthEffect;

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
    SynthState state;
    int needconversion;
    SDL_AudioCVT converter;

    synth_frame_cb_t synth_frame_cb;
    void *synth_frame_priv;

    SDL_AudioCVT U8toF32;
    SDL_AudioCVT S16toF32;

    SynthBuffer *buffer;
    unsigned int buffersmem;

    SynthEnvelope *envelope;
    unsigned int envelopesmem;

    SynthPlayer *player;
    unsigned int playersmem;

    SynthEffect *effect;
    unsigned int effectsmem;

    synth_log_cb_t synth_log_cb;
    void *synth_log_priv;
} Synth;

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
        s->converter.len = todo * sizeof(float);
        s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
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
            s->converter.len = todo * sizeof(float);
            s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
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
        s->converter.len = todo * sizeof(float);
        s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
        SDL_ConvertAudio(&(s->converter));
        s->converter.buf = &(s->channelbuffers[1].data[readcursor]);
        SDL_ConvertAudio(&(s->converter));
        /* this is probably slow */
        if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
            for(i = 0; i < todo; i++) {
                ((Sint32 *)stream)[i * 2] =
                    ((Sint32 *)(s->channelbuffers[0].data))[readcursor + i];
                ((Sint32 *)stream)[i * 2 + 1] =
                    ((Sint32 *)(s->channelbuffers[1].data))[readcursor + i];
            }
            /* clear used buffer so the converted data isn't reconverted from
             * garbage possibly producing horrible noises */
            memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint32));
        } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
            for(i = 0; i < todo; i++) {
                ((Sint16 *)stream)[i * 2] =
                    ((Sint16 *)(s->channelbuffers[0].data))[readcursor + i];
                ((Sint16 *)stream)[i * 2 + 1] =
                    ((Sint16 *)(s->channelbuffers[1].data))[readcursor + i];
            }
            memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint16));
        } else { /* 8, very unlikely */
            for(i = 0; i < todo; i++) {
                stream[i * 2] =
                    (char *)(s->channelbuffers[0].data)[readcursor + i];
                stream[i * 2 + 1] =
                    (char *)(s->channelbuffers[1].data)[readcursor + i];
            }
            memset(s->channelbuffers[0].data, 0, todo);
        }
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            todo = length > available ? available : length;
            s->converter.len = todo * sizeof(float);
            s->converter.buf = &(s->channelbuffers[0].data[readcursor]);
            SDL_ConvertAudio(&(s->converter));
            s->converter.buf = &(s->channelbuffers[1].data[readcursor]);
            SDL_ConvertAudio(&(s->converter));
            if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
                for(i = 0; i < todo; i++) {
                    ((Sint32 *)stream)[i * 2] =
                        ((Sint32 *)(s->channelbuffers[0].data))[readcursor + i];
                    ((Sint32 *)stream)[i * 2 + 1] =
                        ((Sint32 *)(s->channelbuffers[1].data))[readcursor + i];
                }
                memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint32));
            } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
                for(i = 0; i < todo; i++) {
                    ((Sint16 *)stream)[i * 2] =
                        ((Sint16 *)(s->channelbuffers[0].data))[readcursor + i];
                    ((Sint16 *)stream)[i * 2 + 1] =
                        ((Sint16 *)(s->channelbuffers[1].data))[readcursor + i];
                }
                memset(s->channelbuffers[0].data, 0, todo);
            } else { /* 8 */
                for(i = 0; i < todo; i++) {
                    stream[i * 2] =
                        (char *)(s->channelbuffers[0].data)[readcursor + i];
                    stream[i * 2 + 1] =
                        (char *)(s->channelbuffers[1].data)[readcursor + i];
                }
                memset(s->channelbuffers[0].data, 0, todo);
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
        s->converter.len = todo * sizeof(float);
        for(i = 0; i < s->channels; i++) {
            s->converter.buf = &(s->channelbuffers[i].data[readcursor]);
            SDL_ConvertAudio(&(s->converter));
        }
        /* this is probably very slow */
        if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    ((Sint32 *)stream)[i * s->channels + j] =
                        ((Sint32 *)(s->channelbuffers[j].data))[readcursor + i];
                }
            }
            memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint32));
        } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    ((Sint16 *)stream)[i * s->channels + j] =
                        ((Sint16 *)(s->channelbuffers[j].data))[readcursor + i];
                }
            }
            memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint16));
        } else { /* 8 */
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    stream[i * s->channels + j] =
                        ((char *)(s->channelbuffers[j].data))[readcursor + i];
                }
            }
            memset(s->channelbuffers[0].data, 0, todo);
        }
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            todo = length > available ? available : length;
            s->converter.len = todo * sizeof(float);
            for(i = 0; i < s->channels; i++) {
                s->converter.buf = &(s->channelbuffers[i].data[readcursor]);
                SDL_ConvertAudio(&(s->converter));
            }
            if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((Sint32 *)stream)[i * s->channels + j] =
                            ((Sint32 *)(s->channelbuffers[j].data))[readcursor + i];
                    }
                }
                memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint32));
            } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((Sint16 *)stream)[i * s->channels + j] =
                            ((Sint16 *)(s->channelbuffers[j].data))[readcursor + i];
                    }
                }
                memset(s->channelbuffers[0].data, 0, todo * sizeof(Sint16));
            } else { /* 8 */
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        stream[i * s->channels + j] =
                            ((char *)(s->channelbuffers[j].data))[readcursor + i];
                    }
                }
                memset(s->channelbuffers[0].data, 0, todo);
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
    /* probably impossible, but there are cases where at least one output
     * buffer is assumed, so I guess make it clear that there must be at least
     * 1. */
    if(desired.channels < 1) {
        fprintf(stderr, "No channels?\n");
        SDL_CloseAudio();
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
    s->envelope = NULL;
    s->envelopesmem = 0;
    s->player = NULL;
    s->playersmem = 0;
    s->effect = NULL;
    s->effectsmem = 0;
    s->underrun = 0;
    s->state = SYNTH_STOPPED;
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

    if(s->buffer != NULL) {
        free(s->buffer);
    }

    if(s->envelope != NULL) {
        free(s->envelope);
    }

    if(s->player != NULL) {
        free(s->player);
    }

    if(s->effect != NULL) {
        free(s->effect);
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
        s->state = SYNTH_STOPPED;
    } else {
        if(s->channelbuffers == NULL) {
            fprintf(stderr, "Audio buffers haven't been set up.  Set fragment "
                            "count first.\n");
            return(-1);
        }
        /* signal to enable */
        s->state = SYNTH_ENABLED;
    }

    return(0);
}

int synth_frame(Synth *s) {
    unsigned int needed;

    if(s->state == SYNTH_ENABLED) {
        /* signaled to start.  Reset everything, fill the buffer up then start
         * the audio, so there's something to be consumed right away. */
        /* Audio is stopped here, so no need to lock */
        s->bufferfilled = 0;
        s->readcursor = 0;
        s->writecursor = 0;
        s->underrun = 0;
        return(s->synth_frame_cb(synth_frame_priv));
        update_samples_needed(s->buffersize);
        s->state == SYNTH_RUNNING;
        SDL_PauseAudio(0);
    } else if(s->state == SYNTH_RUNNING) {
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
    
    if(s->state != SYNTH_STOPPED) {
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

    switch(type) {
        case SYNTH_TYPE_U8:
        case SYNTH_TYPE_S16:
        case SYNTH_TYPE_F32:
            break;
        default:
            fprintf(stderr, "Invalid player volume mode.\n");
            return(-1);
    }

    if(size < 2) {
        fprintf(stderr, "Buffer size too small.\n");
        return(-1);
    }

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
    /* initialize empty excess buffers as empty */
    for(j = i + 1; j < s->buffersmem; j++) {
        s->buffer[j].size = 0;
    }
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

int synth_add_player(Synth *s, unsigned int inBuffer) {
    unsigned int i, j;
    SynthPlayer *temp;

    if(inBuffer < s->channels) {
        fprintf(stderr, "Output buffer can't be used as input.\n");
        return(-1);
    }
    inBuffer -= s->channels;

    if(inBuffer > s->buffersmem ||
       s->buffer[inBuffer].size == 0) {
        fprintf(stderr, "Invalid buffer index.\n");
        return(-1);
    }

    /* first loaded buffer, so do some initial setup */
    if(s->playerssmem == 0) {
        s->player = malloc(sizeof(SynthPlayer));
        if(s->player == NULL) {
            LOG_PRINTF(s, "Failed to allocate buffers memory.\n");
            return(-1);
        }
        s->player[0].playersmem = 1;
        s->player[0].inBuffer = inBuffer;
        s->buffer[inBuffer].ref++; /* add a reference */
        s->player[0].outBuffer = 0; /* A 0th buffer will have to exist at least */
        s->player[0].inPos = 0.0;
        s->player[0].outPos = 0;
        s->player[0].outOp = SYNTH_OUTPUT_ADD;
        s->player[0].volMode = SYNTH_VOLUME_CONSTANT;
        s->player[0].volume = 1.0;
        s->player[0].volBuffer = inBuffer; /* 0 is output only, so this is the only sane
                                    default here.  It won't do anything weird.
                                    */
        s->buffer[inBuffer].ref++;
        s->player[0].volPos = 0;
        s->player[0].volScale = 1.0;
        s->player[0].playMode = SYNTH_MODE_ONCE;
        s->player[0].loopStart = 0;
        s->player[0].loopEnd = s->buffer[inBuffer].size - 1;
        s->player[0].phaseBuffer = inBuffer; /* this would have some weird effect, but
                                      at least it won't fail? */
        s->buffer[inBuffer].ref++;
        s->player[0].phasePos = 0;
        s->player[0].speedMode = SYNTH_SPEED_CONSTANT;
        s->player[0].speedBuffer = inBuffer; /* same */
        s->buffer[inBuffer].ref++;
        s->player[0].speedPos = 0;
        s->player[0].speedScale = 1.0;
        return(0);
    }

    /* find first NULL buffer and assign it */
    for(i = 0; i < s->playersmem; i++) {
        if(s->player[i].inBuffer == 0) {
            s->player[i].inBuffer = inBuffer;
            s->buffer[inBuffer].ref++;
            s->player[i].outBuffer = 0;
            s->player[i].inPos = 0.0;
            s->player[i].outPos = 0;
            s->player[i].outOp = SYNTH_OUTPUT_ADD;
            s->player[i].volMode = SYNTH_VOLUME_CONSTANT;
            s->player[i].volume = 1.0;
            s->player[i].volBuffer = inBuffer;
            s->buffer[inBuffer].ref++;
            s->player[i].volPos = 0;
            s->player[i].volScale = 1.0;
            s->player[i].playMode = SYNTH_MODE_ONCE;
            s->player[i].loopStart = 0;
            s->player[i].loopEnd = s->buffer[inBuffer].size - 1;
            s->player[i].phaseBuffer = inBuffer;
            s->buffer[inBuffer].ref++;
            s->player[i].phasePos = 0;
            s->player[i].speedMode = SYNTH_SPEED_CONSTANT;
            s->player[i].speedBuffer = inBuffer;
            s->buffer[inBuffer].ref++;
            s->player[i].speedScale = 1.0;
            s->player[i].speedPos = 0;
            return(i);
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
    /* initialize empty excess buffers as empty */
    for(j = i + 1; j < s->buffersmem; j++) {
        s->buffer[j].size = 0;
    }
    s->player[i].inBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[i].outBuffer = 0;
    s->player[i].inPos = 0.0;
    s->player[i].outPos = 0;
    s->player[i].outOp = SYNTH_OUTPUT_ADD;
    s->player[i].volMode = SYNTH_VOLUME_CONSTANT;
    s->player[i].volume = 1.0;
    s->player[i].volBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[i].volPos = 0;
    s->player[i].volScale = 1.0;
    s->player[i].playMode = SYNTH_MODE_ONCE;
    s->player[i].loopStart = 0;
    s->player[i].loopEnd = s->buffer[inBuffer].size - 1;
    s->player[i].phaseBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[i].phasePos = 0;
    s->player[i].speedMode = SYNTH_SPEED_CONSTANT;
    s->player[i].speedBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[i].speedPos = 0;
    s->player[i].speedScale = 1.0;
    return(i);
}

int synth_free_player(Synth *s, unsigned int index) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    /* remove a reference */
    if(s->player[index].outBuffer >= s->channels) {
        s->buffer[s->player[index].outBuffer - s->channels].ref--;
    }
    s->buffer[s->player[index].inBuffer].ref--;
    s->buffer[s->player[index].volumeBuffer].ref--;
    s->buffer[s->player[index].phaseBuffer].ref--;
    s->buffer[s->player[index].speedBuffer].ref--;
    s->player[index].inBuffer = 0;

    return(0);
}

int synth_set_player_input_buffer(Synth *s,
                                  unsigned int index,
                                  unsigned int inBuffer) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(inBuffer < s->channels) {
        fprintf(stderr, "Output buffer can't be used as input.\n");
        return(-1);
    }
    inBuffer -= s->channels;

    if(inBuffer > s->buffersmem ||
       s->buffer[inBuffer].size == 0) {
        fprintf(stderr, "Invalid buffer index.\n");
        return(-1);
    }

    s->buffer[s->player[index].inBuffer].ref--;
    s->player[index].inBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[index].inPos = 0.0;

    return(0);
}

int synth_set_player_input_buffer_pos(Synth *s,
                                      unsigned int index,
                                      float inPos) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(inPos >= s->buffer[s->player[index].inBuffer].size) {
        fprintf(stderr, "Player position out of buffer range.\n");
        return(-1);
    }

    s->player[index].inPos = inPos;

    return(0);
}

int synth_set_player_output_buffer(Synth *s,
                                   unsigned int index,
                                   unsigned int outBuffer) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(outBuffer >= s->channels) {
        if(outBuffer - s->channels > s->buffersmem ||
           s->buffer[outBuffer - s->channels].size == 0) {
            fprintf(stderr, "Invalid buffer index.\n");
            return(-1);
        }
        if(s->player[index].outBuffer >= s->channels) {
            s->buffer[s->player[index].outBuffer - s->channels].ref--;
        }
        s->player[index].outBuffer = outBuffer;
        s->buffer[outBuffer - s->channels].ref++;
        s->player[index].outPos = 0;
    } else {
        if(s->player[index].outBuffer >= s->channels) {
            s->buffer[s->player[index].outBuffer - s->channels].ref--;
        }
        s->player[index].outBuffer = outBuffer;
        s->player[index].outPos = 0;
    }

    return(0);
}

int synth_set_player_output_buffer_pos(Synth *s,
                                       unsigned int index,
                                       unsigned int outPos) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(s->player[index].outBuffer < s->channels) {
        if(outPos >= s->buffersize) {
            fprintf(stderr, "Output buffer pos out of range.\n");
            return(-1);
        }
    } else {
        if(outPos >= s->buffer[s->player[index].outBuffer - s->channels].size) {
            fprintf(stderr, "Output buffer pos out of range.\n");
            return(-1);
        }
    }

    s->player[index].outPos = outPos;

    return(0);
}

int synth_set_player_output_mode(Synth *s,
                                 unsigned int index,
                                 SynthOutputOperation outOp) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    switch(outOp) {
        case SYNTH_OUTPUT_REPLACE:
        case SYNTH_OUTPUT_ADD:
            break;
        default:
            fprintf(stderr, "Invalid player output mode.\n");
            return(-1);
    }

    s->player[index].outOp = outOp;

    return(0);
}

int synth_set_player_volume_mode(Synth *s,
                                 unsigned int index,
                                 SynthVolumeMode volMode) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    switch(volMode) {
        case SYNTH_VOLUME_CONSTANT:
        case SYNTH_VOLUME_SOURCE:
            break;
        default:
            fprintf(stderr, "Invalid player volume mode.\n");
            return(-1);
    }

    s->player[index].volMode = volMode;

    return(0);
}

int synth_set_player_volume(Synth *s,
                            unsigned int index,
                            float volume) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    s->player[index].volume = volume;

    return(0);
}

int synth_set_player_volume_source(Synth *s,
                                   unsigned int index,
                                   unsigned int volBuffer) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(volBuffer < s->channels) {
        fprintf(stderr, "Output buffer can't be used as input.\n");
        return(-1);
    }
    volBuffer -= s->channels;

    if(volBuffer > s->buffersmem ||
       s->buffer[volBuffer].size == 0) {
        fprintf(stderr, "Invalid buffer index.\n");
        return(-1);
    }

    s->buffer[s->player[index].volBuffer].ref--;
    s->player[index].volBuffer = volBuffer;
    s->buffer[volBuffer].ref++;
    s->player[index].volPos = 0;

    return(0);
}

int synth_set_player_volume_source_scale(Synth *s,
                                         unsigned int index,
                                         float volScale) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    s->player[index].volScale = volScale;

    return(0);
}

int synth_set_player_mode(Synth *s,
                          unsigned int index,
                          SynthPlayerMode mode) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    switch(mode) {
        case SYNTH_MODE_ONCE:
        case SYNTH_MODE_LOOP:
        case SYNTH_MODE_PINGPONG:
        case SYNTH_MODE_PHASE_SOURCE:
            break;
        default:
            fprintf(stderr, "Invalid player output mode.\n");
            return(-1);
    }

    s->player[index].mode = mode;

    return(0);
}

int synth_set_player_loop_start(Synth *s,
                                unsigned int index,
                                int loopStart) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(loopStart >= s->buffer[s->player[index].inBuffer].size) {
        fprintf(stderr, "Player loop start out of buffer range.\n");
        return(-1);
    }

    if(loopStart >= s->player[index].loopEnd) {
        fprintf(stderr, "Loop start must be before loop end.\n");
        return(-1);
    }
    s->player[index].loopStart = loopStart;

    return(0);
}

int synth_set_player_loop_end(Synth *s,
                              unsigned int index,
                              int loopEnd) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(loopEnd >= s->buffer[s->player[index].inBuffer].size) {
        fprintf(stderr, "Player loop start out of buffer range.\n");
        return(-1);
    }

    if(loopEnd <= s->player[index].loopStart) {
        fprintf(stderr, "Loop end must be after loop start.\n");
        return(-1);
    }
    s->player[index].loopEnd = loopEnd;

    return(0);
}

int synth_set_player_phase_source(Synth *s,
                                   unsigned int index,
                                   unsigned int phaseBuffer) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(phaseBuffer < s->channels) {
        fprintf(stderr, "Output buffer can't be used as input.\n");
        return(-1);
    }
    phaseBuffer -= s->channels;

    if(phaseBuffer > s->buffersmem ||
       s->buffer[phaseBuffer].size == 0) {
        fprintf(stderr, "Invalid buffer index.\n");
        return(-1);
    }

    s->buffer[s->player[index].phaseBuffer].ref--;
    s->player[index].phaseBuffer = phaseBuffer;
    s->buffer[phaseBuffer].ref++;
    s->player[index].phasePos = 0;

    return(0);
}

int synth_set_player_speed_mode(Synth *s,
                                unsigned int index,
                                SynthSpeedMode speedMode) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    switch(speedMode) {
        case SYNTH_SPEED_CONSTANT:
        case SYNTH_SPEED_SOURCE:
            break;
        default:
            fprintf(stderr, "Invalid player speed mode.\n");
            return(-1);
    }

    s->player[index].speedMode = speedMode;

    return(0);
}

int synth_set_player_speed(Synth *s,
                           unsigned int index,
                           float speed) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    s->player[index].speed = speed;

    return(0);
}

int synth_set_player_speed_source(Synth *s,
                                  unsigned int index,
                                  unsigned int speedBuffer) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    if(speedBuffer < s->channels) {
        fprintf(stderr, "Output buffer can't be used as input.\n");
        return(-1);
    }
    speedBuffer -= s->channels;

    if(speedBuffer > s->buffersmem ||
       s->buffer[speedBuffer].size == 0) {
        fprintf(stderr, "Invalid buffer index.\n");
        return(-1);
    }

    s->buffer[s->player[index].speedBuffer].ref--;
    s->player[index].volBuffer = speedBuffer;
    s->buffer[speedBuffer].ref++;
    s->player[index].speedPos = 0;

    return(0);
}

int synth_set_player_speed_source_scale(Synth *s,
                                        unsigned int index,
                                        float speedScale) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    s->player[index].speedScale = speedScale;

    return(0);
}

int synth_run_player(Synth *s,
                     unsigned int index,
                     unsigned int reqSamples) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        fprintf(stderr, "Invalid player index.\n");
        return(-1);
    }

    /* TODO actual player logic */

    return(reqSamples);
}
