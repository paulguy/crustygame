#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL.h>

#include "synth.h"

#define DEFAULT_RATE (48000)
/* try to determine a sane size which is roughly half a frame long at 60 FPS. 48000 / 120 = 400, nearest power of two is 512, user can set more fragments if they need */
#define DEFAULT_FRAGMENT_SIZE (512)

#define LOG_PRINTF(SYNTH, FMT, ...) \
    (SYNTH)->synth_log_cb((SYNTH)->synth_log_priv, \
    FMT, \
    ##__VA_ARGS__)

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))

typedef struct {
    float *data;
    unsigned int size;
    unsigned int ref;
} SynthBuffer;

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

    SynthPlayerMode mode;
    unsigned int loopStart;
    unsigned int loopEnd;
    unsigned int phaseBuffer;
    unsigned int phasePos;

    SynthSpeedMode speedMode;
    float speed;
    unsigned int speedBuffer;
    unsigned int speedPos;
} SynthPlayer;
/*
typedef struct {
} SynthEnvelope;

typedef struct {
} SynthEffect;
*/
typedef struct Synth_t {
    SDL_AudioDeviceID audiodev;
    unsigned int rate;
    unsigned int fragmentsize;
    unsigned int fragments;
    unsigned int channels;
    SynthBuffer *channelbuffer;
    unsigned int readcursor;
    unsigned int writecursor;
    unsigned int bufferfilled;
    unsigned int buffersize;
    int underrun;
    SynthState state;
    SDL_AudioCVT converter;
    Uint8 silence;

    synth_frame_cb_t synth_frame_cb;
    void *synth_frame_priv;

    SDL_AudioCVT U8toF32;
    SDL_AudioCVT S16toF32;

    SynthBuffer *buffer;
    unsigned int buffersmem;

    SynthPlayer *player;
    unsigned int playersmem;
/*
    SynthEffect *effect;
    unsigned int effectsmem;
*/
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
    if(s->writecursor == s->buffersize) {
        s->writecursor = 0;
    }
    s->bufferfilled += added;
}

static unsigned int get_samples_available(Synth *s) {
    if(s->readcursor == s->writecursor) {
        if(s->bufferfilled == s->buffersize) {
            return(s->buffersize);
        } else {
            return(0);
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

    if(available == 0) {
        s->underrun = 1;
        return;
    }

    if(s->channels == 1) {
        todo = MIN(length, available);
        /* convert in-place, because it can only be shrunken from 32 bits to
         * 16 bits, or just left as-is as 32 bits. */
        s->converter.len = todo * sizeof(float);
        s->converter.buf = (Uint8 *)&(s->channelbuffer[0].data[s->readcursor]);
        /* ignore return value because the documentation indicates the only
         * fail state is that buf is NULL, which it won't be. */
        SDL_ConvertAudio(&(s->converter));
        /* copy what has been converted */
        memcpy(stream,
               &(s->channelbuffer[0].data[s->readcursor]),
               todo * SDL_AUDIO_BITSIZE(s->converter.dst_format) / 8);
        memset(&(s->channelbuffer[0].data[s->readcursor]),
               s->silence,
               todo * SDL_AUDIO_BITSIZE(s->converter.dst_format) / 8);
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            /* more to do, so do the same thing again */
            available = get_samples_available(s);
            todo = MIN(length, available);
            s->converter.len = todo * sizeof(float);
            s->converter.buf = (Uint8 *)&(s->channelbuffer[0].data[s->readcursor]);
            SDL_ConvertAudio(&(s->converter));
            memcpy(stream,
                   &(s->channelbuffer[0].data[s->readcursor]),
                   todo * SDL_AUDIO_BITSIZE(s->converter.dst_format) / 8);
            memset(&(s->channelbuffer[0].data[s->readcursor]),
                   s->silence,
                   todo * SDL_AUDIO_BITSIZE(s->converter.dst_format) / 8);
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                /* SDL audio requested more, but there is no more,
                 * underrun. */
                s->underrun = 1;
            }
        }
    } else if(s->channels == 2) { /* hopefully faster stereo code path */
        /* much like mono, just do it to both channels and zipper them in to
         * the output */
        todo = MIN(length, available);
        s->converter.len = todo * sizeof(float);
        s->converter.buf = (Uint8 *)&(s->channelbuffer[0].data[s->readcursor]);
        SDL_ConvertAudio(&(s->converter));
        s->converter.buf = (Uint8 *)&(s->channelbuffer[1].data[s->readcursor]);
        SDL_ConvertAudio(&(s->converter));
        /* this is probably slow */
        if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
            for(i = 0; i < todo; i++) {
                ((Sint32 *)stream)[i * 2] =
                    ((Sint32 *)(s->channelbuffer[0].data))[s->readcursor + i];
                ((Sint32 *)stream)[i * 2 + 1] =
                    ((Sint32 *)(s->channelbuffer[1].data))[s->readcursor + i];
            }
            /* clear used buffer so the converted data isn't reconverted from
             * garbage possibly producing horrible noises */
            memset(&(s->channelbuffer[0].data[s->readcursor]),
                   s->silence,
                   todo * sizeof(Sint32));
            memset(&(s->channelbuffer[1].data[s->readcursor]),
                   s->silence,
                   todo * sizeof(Sint32));
        } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
            for(i = 0; i < todo; i++) {
                ((Sint16 *)stream)[i * 2] =
                    ((Sint16 *)(s->channelbuffer[0].data))[s->readcursor + i];
                ((Sint16 *)stream)[i * 2 + 1] =
                    ((Sint16 *)(s->channelbuffer[1].data))[s->readcursor + i];
            }
            memset(&(s->channelbuffer[0].data[s->readcursor]),
                   s->silence,
                   todo * sizeof(Sint16));
            memset(&(s->channelbuffer[1].data[s->readcursor]),
                   s->silence,
                   todo * sizeof(Sint16));
        } else { /* 8, very unlikely */
            for(i = 0; i < todo; i++) {
                stream[i * 2] =
                    ((char *)(s->channelbuffer[0].data))[s->readcursor + i];
                stream[i * 2 + 1] =
                    ((char *)(s->channelbuffer[1].data))[s->readcursor + i];
            }
            memset(&(s->channelbuffer[0].data[s->readcursor]),
                   s->silence,
                   todo);
            memset(&(s->channelbuffer[1].data[s->readcursor]),
                   s->silence,
                   todo);
        }
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            todo = MIN(length, available);
            s->converter.len = todo * sizeof(float);
            s->converter.buf = (Uint8 *)&(s->channelbuffer[0].data[s->readcursor]);
            SDL_ConvertAudio(&(s->converter));
            s->converter.buf = (Uint8 *)&(s->channelbuffer[1].data[s->readcursor]);
            SDL_ConvertAudio(&(s->converter));
            if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
                for(i = 0; i < todo; i++) {
                    ((Sint32 *)stream)[i * 2] =
                        ((Sint32 *)(s->channelbuffer[0].data))[s->readcursor + i];
                    ((Sint32 *)stream)[i * 2 + 1] =
                        ((Sint32 *)(s->channelbuffer[1].data))[s->readcursor + i];
                }
                memset(&(s->channelbuffer[0].data[s->readcursor]),
                       s->silence,
                       todo * sizeof(Sint32));
                memset(&(s->channelbuffer[1].data[s->readcursor]),
                       s->silence,
                       todo * sizeof(Sint32));
            } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
                for(i = 0; i < todo; i++) {
                    ((Sint16 *)stream)[i * 2] =
                        ((Sint16 *)(s->channelbuffer[0].data))[s->readcursor + i];
                    ((Sint16 *)stream)[i * 2 + 1] =
                        ((Sint16 *)(s->channelbuffer[1].data))[s->readcursor + i];
                }
                memset(&(s->channelbuffer[0].data[s->readcursor]),
                       s->silence,
                       todo * sizeof(Sint16));
                memset(&(s->channelbuffer[1].data[s->readcursor]),
                       s->silence,
                       todo * sizeof(Sint16));
            } else { /* 8 */
                for(i = 0; i < todo; i++) {
                    stream[i * 2] =
                        ((char *)(s->channelbuffer[0].data))[s->readcursor + i];
                    stream[i * 2 + 1] =
                        ((char *)(s->channelbuffer[1].data))[s->readcursor + i];
                }
                memset(&(s->channelbuffer[0].data[s->readcursor]),
                       s->silence,
                       todo);
                memset(&(s->channelbuffer[1].data[s->readcursor]),
                       s->silence,
                       todo);
            }
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                s->underrun = 1;
            }
        }
    } else { /* unlikely case it's multichannel surround ... */
        /* much like stereo, but use a loop because i don't feel like making
         * a bunch of unrolled versions of this unless surround sound becomes
         * something frequently used with this.. */
        todo = MIN(length, available);
        s->converter.len = todo * sizeof(float);
        for(i = 0; i < s->channels; i++) {
            s->converter.buf = (Uint8 *)&(s->channelbuffer[i].data[s->readcursor]);
            SDL_ConvertAudio(&(s->converter));
        }
        /* this is probably very slow */
        if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    ((Sint32 *)stream)[i * s->channels + j] =
                        ((Sint32 *)(s->channelbuffer[j].data))[s->readcursor + i];
                }
            }
            for(i = 0; i < s->channels; i++) {
                memset(&(s->channelbuffer[i].data[s->readcursor]),
                       s->silence,
                       todo * sizeof(Sint32));
            }
        } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    ((Sint16 *)stream)[i * s->channels + j] =
                        ((Sint16 *)(s->channelbuffer[j].data))[s->readcursor + i];
                }
            }
            for(i = 0; i < s->channels; i++) {
                memset(&(s->channelbuffer[i].data[s->readcursor]),
                       s->silence,
                       todo * sizeof(Sint16));
            }
        } else { /* 8 */
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    stream[i * s->channels + j] =
                        ((char *)(s->channelbuffer[j].data))[s->readcursor + i];
                }
            }
            for(i = 0; i < s->channels; i++) {
                memset(&(s->channelbuffer[i].data[s->readcursor]),
                       s->silence,
                       todo);
            }
        }
        update_samples_available(s, todo);
        length -= todo;
        if(length > 0) {
            available = get_samples_available(s);
            todo = MIN(length, available);
            s->converter.len = todo * sizeof(float);
            for(i = 0; i < s->channels; i++) {
                s->converter.buf = (Uint8 *)&(s->channelbuffer[i].data[s->readcursor]);
                SDL_ConvertAudio(&(s->converter));
            }
            if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 32) {
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((Sint32 *)stream)[i * s->channels + j] =
                            ((Sint32 *)(s->channelbuffer[j].data))[s->readcursor + i];
                    }
                }
                for(i = 0; i < s->channels; i++) {
                    memset(&(s->channelbuffer[i].data[s->readcursor]),
                           s->silence,
                           todo * sizeof(Sint32));
                }
            } else if(SDL_AUDIO_BITSIZE(s->converter.dst_format) == 16) {
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((Sint16 *)stream)[i * s->channels + j] =
                            ((Sint16 *)(s->channelbuffer[j].data))[s->readcursor + i];
                    }
                }
                for(i = 0; i < s->channels; i++) {
                    memset(&(s->channelbuffer[i].data[s->readcursor]),
                           s->silence,
                           todo * sizeof(Sint16));
                }
            } else { /* 8 */
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        stream[i * s->channels + j] =
                            ((char *)(s->channelbuffer[j].data))[s->readcursor + i];
                    }
                }
                for(i = 0; i < s->channels; i++) {
                    memset(&(s->channelbuffer[i].data[s->readcursor]),
                           s->silence,
                           todo);
                }
            }
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                s->underrun = 1;
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
    s->audiodev = SDL_OpenAudioDevice(NULL,
                                      0,
                                      &desired,
                                      &obtained,
                                      SDL_AUDIO_ALLOW_ANY_CHANGE);
    if(s->audiodev < 2) {
        LOG_PRINTF(s, "Failed to open SDL audio.\n");
        free(s);
        return(NULL);
    }

    /* probably impossible, but there are cases where at least one output
     * buffer is assumed, so I guess make it clear that there must be at least
     * 1. */
    if(desired.channels < 1) {
        LOG_PRINTF(s, "No channels?\n");
        SDL_CloseAudioDevice(s->audiodev);
        free(s);
        return(NULL);
    }

    if(SDL_AUDIO_BITSIZE(obtained.format) != 32 &&
       SDL_AUDIO_BITSIZE(obtained.format) != 16 &&
       SDL_AUDIO_BITSIZE(obtained.format) != 8) {
        LOG_PRINTF(s, "Unsupported format size: %d.\n",
                        SDL_AUDIO_BITSIZE(obtained.format));
        SDL_CloseAudioDevice(s->audiodev);
        free(s);
        return(NULL);
    }

    /* just use the obtained spec for frequency but try to convert the format.
     * Specify mono because the buffers are separate until the end. */
    if(SDL_BuildAudioCVT(&(s->converter),
                         desired.format,
                         1,
                         obtained.freq,
                         obtained.format,
                         1,
                         obtained.freq) < 0) {
        LOG_PRINTF(s, "Can't create audio output converter.\n");
        SDL_CloseAudioDevice(s->audiodev);
        free(s);
        return(NULL);
    }

    /* create converters now for allowing import later */
    if(SDL_BuildAudioCVT(&(s->U8toF32),
                         AUDIO_U8,
                         1,
                         obtained.freq,
                         AUDIO_F32SYS,
                         1,
                         obtained.freq) < 0) {
        LOG_PRINTF(s, "Failed to build U8 import converter.\n");
        SDL_CloseAudioDevice(s->audiodev);
        free(s);
        return(NULL);
    }
    if(SDL_BuildAudioCVT(&(s->S16toF32),
                         AUDIO_S16SYS,
                         1,
                         obtained.freq,
                         AUDIO_F32SYS,
                         1,
                         obtained.freq) < 0) {
        LOG_PRINTF(s, "Failed to build S16 import converter.\n");
        SDL_CloseAudioDevice(s->audiodev);
        free(s);
        return(NULL);
    }

    s->rate = obtained.freq;
    s->fragmentsize = obtained.samples;
    s->fragments = 0;
    s->channels = obtained.channels;
    s->silence = obtained.silence;
    /* Won't know what size to allocate to them until the user has set a number of fragments */
    s->channelbuffer = NULL;
    s->buffer = NULL;
    s->buffersmem = 0;
    s->player = NULL;
    s->playersmem = 0;
/*
    s->effect = NULL;
    s->effectsmem = 0;
*/
    s->underrun = 0;
    s->state = SYNTH_STOPPED;
    s->synth_frame_cb = synth_frame_cb;
    s->synth_frame_priv = synth_frame_priv;

    return(s);
}

void synth_free(Synth *s) {
    unsigned int i;

    SDL_LockAudioDevice(s->audiodev);
    SDL_CloseAudioDevice(s->audiodev);

    if(s->channelbuffer != NULL) {
        for(i = 0; i < s->channels; i++) {
            if(s->channelbuffer[i].data != NULL) {
                free(s->channelbuffer[i].data);
            }
        }
        free(s->channelbuffer);
    }

    if(s->buffer != NULL) {
        free(s->buffer);
    }

    if(s->player != NULL) {
        free(s->player);
    }
/*
    if(s->effect != NULL) {
        free(s->effect);
    }
*/
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

int synth_set_enabled(Synth *s, int enabled) {
    if(enabled == 0) {
        SDL_PauseAudioDevice(s->audiodev, 1);
        s->state = SYNTH_STOPPED;
    } else {
        if(s->channelbuffer == NULL) {
            LOG_PRINTF(s, "Audio buffers haven't been set up.  Set fragment "
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
        if(s->synth_frame_cb(s->synth_frame_priv) < 0) {
            return(-1);
        }
        update_samples_needed(s, s->buffersize);
        s->state = SYNTH_RUNNING;
        SDL_PauseAudioDevice(s->audiodev, 0);
    } else if(s->state == SYNTH_RUNNING) {
        needed = synth_get_samples_needed(s);
        if(needed > 0) {
            SDL_LockAudioDevice(s->audiodev);
            /* call this again to avoid racing */
            needed = synth_get_samples_needed(s);
            if(s->synth_frame_cb(s->synth_frame_priv) < 0) {
                return(-1);
            }
            update_samples_needed(s, needed);
            needed = synth_get_samples_needed(s);
            /* get_samples_needed() returns only the remaining contiguous
             * buffer, so it may need to be called twice */
            if(needed > 0) {
                if(s->synth_frame_cb(s->synth_frame_priv) < 0) {
                    return(-1);
                }
                update_samples_needed(s, needed);
            }
            SDL_UnlockAudioDevice(s->audiodev);
        }
    }

    return(0);
}

int synth_set_fragments(Synth *s,
                        unsigned int fragments) {
    int i;

    if(fragments == 0) {
        return(-1);
    }
    
    if(s->state != SYNTH_STOPPED) {
        LOG_PRINTF(s, "Synth must be stopped before changing fragment size.\n");
        return(-1);
    }

    if(s->channelbuffer != NULL) {
        if(s->fragments != fragments) {
            for(i = 0; i < s->channels; i++) {
                free(s->channelbuffer[i].data);
            }
            free(s->channelbuffer);
            s->channelbuffer = NULL;
        } else {
            /* nothing to do */
            return(0);
        }
    }

    if(s->channelbuffer == NULL) {
        s->channelbuffer = malloc(sizeof(SynthBuffer) * s->channels);
        if(s->channelbuffer == NULL) {
            LOG_PRINTF(s, "Failed to allocate channel buffers.\n");
            return(-1);
        }
    }

    s->fragments = fragments;
    s->buffersize = s->fragmentsize * fragments;
    for(i = 0; i < s->channels; i++) {
        s->channelbuffer[i].size = s->buffersize;
        s->channelbuffer[i].data =
            malloc(sizeof(float) * s->buffersize);
        if(s->channelbuffer[i].data == NULL) {
            LOG_PRINTF(s, "Failed to allocate channel buffer memory.\n");
            for(i -= 1; i >= 0; i--) {
                free(s->channelbuffer[i].data);
            }
            free(s->channelbuffer);
            s->fragments = 0;
            return(-1);
        }
        memset(s->channelbuffer[i].data,
               s->silence,
               sizeof(float) * s->buffersize);
    }

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
        case SYNTH_TYPE_F64:
            break;
        default:
            LOG_PRINTF(s, "Invalid buffer type.\n");
            return(-1);
    }

    /* so loop start and loop end can have valid values. */
    if(size < 2) {
        LOG_PRINTF(s, "Buffer size too small.\n");
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
        s->buffer[0].size = size;
        s->buffer[0].data = malloc(size * sizeof(float));
        if(s->buffer[0].data == NULL) {
            LOG_PRINTF(s, "Failed to allocate buffer data memory.\n");
            return(-1);
        }
        if(data != NULL) {
            if(type == SYNTH_TYPE_U8) {
                memcpy(s->buffer[0].data, data, size * sizeof(Uint8));
                s->U8toF32.buf = (Uint8 *)s->buffer[0].data;
                s->U8toF32.len = size;
                SDL_ConvertAudio(&(s->U8toF32));
            } else if(type == SYNTH_TYPE_S16) {
                memcpy(s->buffer[0].data, data, size * sizeof(Sint16));
                s->S16toF32.buf = (Uint8 *)s->buffer[0].data;
                s->S16toF32.len = size * sizeof(Sint16);
                SDL_ConvertAudio(&(s->S16toF32));
            } else if(type == SYNTH_TYPE_F32) {
                memcpy(s->buffer[0].data, data, size * sizeof(float));
            } else { /* F64 */
                /* SDL has no conversion facilities to accept F64, so just do
                 * a cast of each value in a loop and hope it goes OK. */
                for(j = 0; j < size; j++) {
                    s->buffer[0].data[j] = (float)(((double *)data)[j]);
                }
            }
        } else {
            memset(s->buffer[0].data, s->silence, size * sizeof(float));
        }
        s->buffer[0].ref = 0;
        return(s->channels);
    }

    /* find first NULL buffer and assign it */
    for(i = 0; i < s->buffersmem; i++) {
        if(s->buffer[i].size == 0) {
            s->buffer[i].size = size;
            s->buffer[i].data = malloc(size * sizeof(float));
            if(s->buffer[i].data == NULL) {
                LOG_PRINTF(s, "Failed to allocate buffer data memory.\n");
                return(-1);
            }
            if(data != NULL) {
                if(type == SYNTH_TYPE_U8) {
                    memcpy(s->buffer[i].data, data, size * sizeof(Uint8));
                    s->U8toF32.buf = (Uint8 *)s->buffer[i].data;
                    s->U8toF32.len = size;
                    SDL_ConvertAudio(&(s->U8toF32));
                } else if(type == SYNTH_TYPE_S16) {
                    memcpy(s->buffer[i].data, data, size * sizeof(Sint16));
                    s->S16toF32.buf = (Uint8 *)s->buffer[i].data;
                    s->S16toF32.len = size * sizeof(Sint16);
                    SDL_ConvertAudio(&(s->S16toF32));
                } else if(type == SYNTH_TYPE_F32) {
                    memcpy(s->buffer[i].data, data, size * sizeof(float));
                } else { /* F64 */
                    for(j = 0; j < size; j++) {
                        s->buffer[i].data[j] = (float)(((double *)data)[j]);
                    }
                }
            } else {
                memset(s->buffer[i].data, s->silence, size * sizeof(float));
            }
            s->buffer[i].ref = 0;
            return(s->channels + i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(s->buffer,
                   sizeof(SynthBuffer) * s->buffersmem * 2);
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
    s->buffer[i].size = size;
    s->buffer[i].data = malloc(size * sizeof(float));
    if(s->buffer[i].data == NULL) {
        LOG_PRINTF(s, "Failed to allocate buffer data memory.\n");
        return(-1);
    }
    if(data != NULL) {
        if(type == SYNTH_TYPE_U8) {
            memcpy(s->buffer[i].data, data, size * sizeof(Uint8));
            s->U8toF32.buf = (Uint8 *)s->buffer[i].data;
            s->U8toF32.len = size;
            SDL_ConvertAudio(&(s->U8toF32));
        } else if(type == SYNTH_TYPE_S16) {
            memcpy(s->buffer[i].data, data, size * sizeof(Sint16));
            s->S16toF32.buf = (Uint8 *)s->buffer[i].data;
            s->S16toF32.len = size * sizeof(Sint16);
            SDL_ConvertAudio(&(s->S16toF32));
        } else if(type == SYNTH_TYPE_F32) {
            memcpy(s->buffer[i].data, data, size * sizeof(float));
        } else { /* F64 */
            for(j = 0; j < size; j++) {
                s->buffer[i].data[j] = (float)(((double *)data)[j]);
            }
        }
    } else {
        memset(s->buffer[i].data, s->silence, size * sizeof(float));
    }
    s->buffer[i].ref = 0;
    return(s->channels + i);
}

int synth_free_buffer(Synth *s, unsigned int index) {
    index -= s->channels;

    if(index > s->buffersmem ||
       s->buffer[index].size == 0 ||
       s->buffer[index].ref != 0) {
        LOG_PRINTF(s, "Invalid buffer index or buffer in use.\n");
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
        LOG_PRINTF(s, "Output buffer can't be used as input.\n");
        return(-1);
    }
    inBuffer -= s->channels;

    if(inBuffer > s->buffersmem ||
       s->buffer[inBuffer].size == 0) {
        LOG_PRINTF(s, "Invalid buffer index.\n");
        return(-1);
    }

    /* first loaded buffer, so do some initial setup */
    if(s->playersmem == 0) {
        s->player = malloc(sizeof(SynthPlayer));
        if(s->player == NULL) {
            LOG_PRINTF(s, "Failed to allocate buffers memory.\n");
            return(-1);
        }
        s->playersmem = 1;
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
        s->player[0].mode = SYNTH_MODE_ONCE;
        s->player[0].loopStart = 0;
        s->player[0].loopEnd = s->buffer[inBuffer].size - 1;
        s->player[0].phaseBuffer = inBuffer; /* this would have some weird effect, but
                                      at least it won't fail? */
        s->buffer[inBuffer].ref++;
        s->player[0].phasePos = 0;
        s->player[0].speedMode = SYNTH_SPEED_CONSTANT;
        s->player[0].speed = 1.0;
        s->player[0].speedBuffer = inBuffer; /* same */
        s->buffer[inBuffer].ref++;
        s->player[0].speedPos = 0;
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
            s->player[i].mode = SYNTH_MODE_ONCE;
            s->player[i].loopStart = 0;
            s->player[i].loopEnd = s->buffer[inBuffer].size - 1;
            s->player[i].phaseBuffer = inBuffer;
            s->buffer[inBuffer].ref++;
            s->player[i].phasePos = 0;
            s->player[i].speedMode = SYNTH_SPEED_CONSTANT;
            s->player[i].speed = 1.0;
            s->player[i].speedBuffer = inBuffer;
            s->buffer[inBuffer].ref++;
            s->player[i].speedPos = 0;
            return(i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(s->player,
                   sizeof(SynthPlayer) * s->playersmem * 2);
    if(temp == NULL) {
        LOG_PRINTF(s, "Failed to allocate buffers memory.\n");
        return(-1);
    }
    s->player = temp;
    s->playersmem *= 2;
    /* initialize empty excess buffers as empty */
    for(j = i + 1; j < s->playersmem; j++) {
        s->player[j].inBuffer = 0;
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
    s->player[i].mode = SYNTH_MODE_ONCE;
    s->player[i].loopStart = 0;
    s->player[i].loopEnd = s->buffer[inBuffer].size - 1;
    s->player[i].phaseBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[i].phasePos = 0;
    s->player[i].speedMode = SYNTH_SPEED_CONSTANT;
    s->player[i].speed = 1.0;
    s->player[i].speedBuffer = inBuffer;
    s->buffer[inBuffer].ref++;
    s->player[i].speedPos = 0;
    return(i);
}

int synth_free_player(Synth *s, unsigned int index) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    /* remove a reference */
    if(s->player[index].outBuffer >= s->channels) {
        s->buffer[s->player[index].outBuffer - s->channels].ref--;
    }
    s->buffer[s->player[index].inBuffer].ref--;
    s->buffer[s->player[index].volBuffer].ref--;
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(inBuffer < s->channels) {
        LOG_PRINTF(s, "Output buffer can't be used as input.\n");
        return(-1);
    }
    inBuffer -= s->channels;

    if(inBuffer > s->buffersmem ||
       s->buffer[inBuffer].size == 0) {
        LOG_PRINTF(s, "Invalid buffer index.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(outBuffer >= s->channels) {
        if(outBuffer - s->channels > s->buffersmem ||
           s->buffer[outBuffer - s->channels].size == 0) {
            LOG_PRINTF(s, "Invalid buffer index.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    s->player[index].outPos = outPos;

    return(0);
}

int synth_set_player_output_mode(Synth *s,
                                 unsigned int index,
                                 SynthOutputOperation outOp) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    switch(outOp) {
        case SYNTH_OUTPUT_REPLACE:
        case SYNTH_OUTPUT_ADD:
            break;
        default:
            LOG_PRINTF(s, "Invalid player output mode.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    switch(volMode) {
        case SYNTH_VOLUME_CONSTANT:
        case SYNTH_VOLUME_SOURCE:
            break;
        default:
            LOG_PRINTF(s, "Invalid player volume mode.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(volBuffer < s->channels) {
        LOG_PRINTF(s, "Output buffer can't be used as input.\n");
        return(-1);
    }
    volBuffer -= s->channels;

    if(volBuffer > s->buffersmem ||
       s->buffer[volBuffer].size == 0) {
        LOG_PRINTF(s, "Invalid buffer index.\n");
        return(-1);
    }

    s->buffer[s->player[index].volBuffer].ref--;
    s->player[index].volBuffer = volBuffer;
    s->buffer[volBuffer].ref++;
    s->player[index].volPos = 0;

    return(0);
}

int synth_set_player_mode(Synth *s,
                          unsigned int index,
                          SynthPlayerMode mode) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    switch(mode) {
        case SYNTH_MODE_ONCE:
        case SYNTH_MODE_LOOP:
        case SYNTH_MODE_PINGPONG:
        case SYNTH_MODE_PHASE_SOURCE:
            break;
        default:
            LOG_PRINTF(s, "Invalid player output mode.\n");
            return(-1);
    }

    s->player[index].mode = mode;

    return(0);
}

int synth_set_player_loop_start(Synth *s,
                                unsigned int index,
                                unsigned int loopStart) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(loopStart >= s->buffer[s->player[index].inBuffer].size) {
        LOG_PRINTF(s, "Player loop start out of buffer range.\n");
        return(-1);
    }

    if(loopStart >= s->player[index].loopEnd) {
        LOG_PRINTF(s, "Loop start must be before loop end.\n");
        return(-1);
    }
    s->player[index].loopStart = loopStart;

    return(0);
}

int synth_set_player_loop_end(Synth *s,
                              unsigned int index,
                              unsigned int loopEnd) {
    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(loopEnd >= s->buffer[s->player[index].inBuffer].size) {
        LOG_PRINTF(s, "Player loop start out of buffer range.\n");
        return(-1);
    }

    if(loopEnd <= s->player[index].loopStart) {
        LOG_PRINTF(s, "Loop end must be after loop start.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(phaseBuffer < s->channels) {
        LOG_PRINTF(s, "Output buffer can't be used as input.\n");
        return(-1);
    }
    phaseBuffer -= s->channels;

    if(phaseBuffer > s->buffersmem ||
       s->buffer[phaseBuffer].size == 0) {
        LOG_PRINTF(s, "Invalid buffer index.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    switch(speedMode) {
        case SYNTH_SPEED_CONSTANT:
        case SYNTH_SPEED_SOURCE:
            break;
        default:
            LOG_PRINTF(s, "Invalid player speed mode.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
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
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }

    if(speedBuffer < s->channels) {
        LOG_PRINTF(s, "Output buffer can't be used as input.\n");
        return(-1);
    }
    speedBuffer -= s->channels;

    if(speedBuffer > s->buffersmem ||
       s->buffer[speedBuffer].size == 0) {
        LOG_PRINTF(s, "Invalid buffer index.\n");
        return(-1);
    }

    s->buffer[s->player[index].speedBuffer].ref--;
    s->player[index].speedBuffer = speedBuffer;
    s->buffer[speedBuffer].ref++;
    s->player[index].speedPos = 0;

    return(0);
}

/* another heckin' chonky overcomplicated function.  My approach here is to
 * try to figure out as many conditions and values ahead of time to keep the
 * loops tight and small and hopefully that'll help the compiler figure out
 * how to make them faster? */
int synth_run_player(Synth *s,
                     unsigned int index,
                     unsigned int reqSamples) {
    unsigned int samples;
    unsigned int todo;
    SynthPlayer *p;
    SynthBuffer *i;
    float *o;
    unsigned int os;
    SynthBuffer *sp;
    SynthBuffer *v;
    SynthBuffer *ph;
    float loopLen;
    float lastInPos;

    if(index > s->playersmem ||
       s->player[index].inBuffer == 0) {
        LOG_PRINTF(s, "Invalid player index.\n");
        return(-1);
    }
    p = &(s->player[index]);
    i = &(s->buffer[p->inBuffer]);
    if(p->outBuffer < s->channels) {
        o = &(s->channelbuffer[p->outBuffer].data[s->writecursor]);
        os = synth_get_samples_needed(s);
        if(p->outPos >= os) {
            return(0);
        }
    } else {
        o = s->buffer[s->player[index].outBuffer - s->channels].data;
        os = s->buffer[s->player[index].outBuffer - s->channels].size;
    }

    /* TODO actual player logic */
    samples = 0;
    todo = reqSamples;
    if(p->mode == SYNTH_MODE_ONCE &&
       p->speedMode == SYNTH_SPEED_CONSTANT) {
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            todo = MIN(todo, ((float)(i->size) - p->inPos) /
                             p->speed);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                p->inPos += p->speed;
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            todo = MIN(todo, ((float)(i->size) - p->inPos) /
                             p->speed);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                p->inPos += p->speed;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            todo = MIN(todo, ((float)(i->size) - p->inPos) /
                             p->speed);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                p->inPos += p->speed;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            todo = MIN(todo, ((float)(i->size) - p->inPos) /
                             p->speed);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                p->inPos += p->speed;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else if(p->mode == SYNTH_MODE_ONCE &&
              p->speedMode == SYNTH_SPEED_SOURCE) {
        sp = &(s->buffer[p->speedBuffer]);
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                p->inPos += sp->data[p->speedPos] * p->speed;
                if(p->inPos >= i->size) {
                    break;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                p->inPos += sp->data[p->speedPos] * p->speed;
                if(p->inPos >= i->size) {
                    break;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                p->inPos += sp->data[p->speedPos] * p->speed;
                if(p->inPos >= i->size) {
                    break;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                p->inPos += sp->data[p->speedPos] * p->speed;
                if(p->inPos >= i->size) {
                    break;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else if(p->mode == SYNTH_MODE_LOOP &&
              p->speedMode == SYNTH_SPEED_CONSTANT) {
        loopLen = p->loopEnd - p->loopStart;
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                /* can't do this without branching that I know of, not sure
                 * if it matters.. make sure it only triggers on transition. */
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else if(p->mode == SYNTH_MODE_LOOP &&
              p->speedMode == SYNTH_SPEED_SOURCE) {
        loopLen = p->loopEnd - p->loopStart;
        sp = &(s->buffer[p->speedBuffer]);
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else if(p->mode == SYNTH_MODE_PINGPONG &&
              p->speedMode == SYNTH_SPEED_CONSTANT) {
        loopLen = p->loopEnd - p->loopStart;
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos = fmodf(p->inPos + p->speed, i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else if(p->mode == SYNTH_MODE_PINGPONG &&
              p->speedMode == SYNTH_SPEED_SOURCE) {
        loopLen = p->loopEnd - p->loopStart;
        sp = &(s->buffer[p->speedBuffer]);
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)p->inPos] * v->data[p->volPos] * p->volume;
                p->outPos++;
                lastInPos = p->inPos;
                p->inPos =
                    fmodf(p->inPos +
                          (sp->data[p->speedPos] * p->speed),
                          i->size);
                if(lastInPos <= p->loopEnd &&
                   p->inPos > p->loopEnd) {
                    p->inPos -= loopLen;
                    p->speed = -(p->speed);
                } else if(lastInPos >= p->loopStart &&
                          p->inPos < p->loopStart) {
                    p->inPos += loopLen;
                    p->speed = -(p->speed);
                }
                p->speedPos = (p->speedPos + 1) % sp->size;
                p->volPos = (p->volPos + 1) % v->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else if(p->mode == SYNTH_MODE_PHASE_SOURCE) {
        ph = &(s->buffer[p->phaseBuffer]);
        if(p->volMode == SYNTH_VOLUME_CONSTANT &&
           p->outOp == SYNTH_OUTPUT_REPLACE) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)fabsf(ph->data[p->phasePos] * i->size) % i->size] *
                    p->volume;
                p->outPos++;
                p->phasePos = (p->phasePos + 1) % ph->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_CONSTANT &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)fabsf(ph->data[p->phasePos] * i->size) % i->size] *
                    p->volume;
                p->outPos++;
                p->phasePos = (p->phasePos + 1) % ph->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_REPLACE) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] =
                    i->data[(int)fabsf(ph->data[p->phasePos] * i->size) % i->size] *
                    v->data[p->volPos] * p->volume;
                p->outPos++;
                p->volPos = (p->volPos + 1) % v->size;
                p->phasePos = (p->phasePos + 1) % ph->size;
            }
        } else if(p->volMode == SYNTH_VOLUME_SOURCE &&
                  p->outOp == SYNTH_OUTPUT_ADD) {
            v = &(s->buffer[p->volBuffer]);
            todo = MIN(todo, os - p->outPos);
            for(samples = 0; samples < todo; samples++) {
                o[p->outPos] +=
                    i->data[(int)fabsf(ph->data[p->phasePos] * i->size) % i->size] *
                    v->data[p->volPos] * p->volume;
                p->outPos++;
                p->volPos = (p->volPos + 1) % v->size;
                p->phasePos = (p->phasePos + 1) % ph->size;
            }
        } else {
            LOG_PRINTF(s, "Invalid output mode.\n");
            return(-1);
        }
    } else {
        LOG_PRINTF(s, "Invalid player mode.\n");
        return(-1);
    }

    return(samples);
}
