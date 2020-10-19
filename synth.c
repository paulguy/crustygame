#include <stdlib.h>
#include <string.h>
#include <SDL2.h>

#define DEFAULT_RATE (48000)
/* try to determine a sane size which is roughly half a frame long at 60 FPS. 48000 / 120 = 400, nearest power of two is 512, user can set more fragments if they need */
#define DEFAULT_FRAGMENT_SIZE (512)

#define SYNTH_STOPPED (0)
#define SYNTH_ENABLED (1)
#define SYNTH_RUNNING (2)

typedef int (*synth_frame_cb_t)(void *priv);

typedef enum {
    SYNTH_TYPE_INT,
    SYNTH_TYPE_FLOAT
} SynthType;

typedef struct {
    unsigned int rate;
    unsigned int fragmentsize;
    unsigned int fragments;
    SynthType type;
    unsigned int channels;
    SynthBuffer *channelbuffers;
    unsigned int readcursor;
    unsigned int writecursor;
    unsigned int bufferfilled;
    unsigned int buffersize;
    int underrun;
    unsigned int enable;
    Uint8 silence;

    synth_frame_cb_t synth_frame_cb;
    void *synth_frame_priv;

    SynthBuffer *buffer;
    unsigned int buffersmem;
} Synth;

typedef struct {
    SynthType type;
    Sint16 *intdata;
    float *floatdata;
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

void synth_audio_cb(void *userdata, Uint8 *stream, int len) {
    Synth *s = (Synth *)userdata;
    unsigned int i, j;
    unsigned int available = get_samples_available(s);
    unsigned int todo;
    unsigned int length = len;
    if(s->type == SYNTH_TYPE_INT) {
        length /= sizeof(Sint16);
    } else {
        length /= sizeof(float);
    }
    length /= s->channels;

    if(channels == 1) { /* simple copy for mono */
        if(s->type == SYNTH_TYPE_INT) {
            memcpy(stream, s->channelbuffers[0].intdata, len);
        } else {
            memcpy(stream, s->channelbuffers[0].floatdata, len);
        }
    } else if(channels == 2) { /* hopefully fast stereo code path */
        /* zipper the channel buffers together */
        todo = length > available ? available : length;
        if(s->type == SYNTH_TYPE_INT) {
            for(i = 0; i < todo; i++) {
                ((* Sint16)stream)[i * 2] =
                    s->channelbuffers[0].intdata[i];
                ((* Sint16)stream)[i * 2 + 1] =
                    s->channelbuffers[1].intdata[i];
            }
        } else {
            for(i = 0; i < todo; i++) {
                ((* float)stream)[i * 2] =
                    s->channelbuffers[0].floatdata[i];
                ((* float)stream)[i * 2 + 1] =
                    s->channelbuffers[1].floatdata[i];
            }
        }
        update_samples_available(s, todo);
        length =- todo;
        /* the buffer will not have been emptied if it wrapped around */
        if(length > 0) {
            available = get_samples_available(s);
            todo = length > available ? available : length;
            if(s->type == SYNTH_TYPE_INT) {
                for(i = 0; i < todo; i++) {
                    ((* Sint16)stream)[i * 2] =
                        s->channelbuffers[0].intdata[i];
                    ((* Sint16)stream)[i * 2 + 1] =
                        s->channelbuffers[1].intdata[i];
                }
            } else {
                for(i = 0; i < todo; i++) {
                    ((* float)stream)[i * 2] =
                        s->channelbuffers[0].floatdata[i];
                    ((* float)stream)[i * 2 + 1] =
                        s->channelbuffers[1].floatdata[i];
                }
            }
            update_samples_available(s, todo);
            length -= todo;
            if(length > 0) {
                /* buffer consumed, and requested buffer not filled */
                s->underrun = 1;
            }
        }
    } else { /* unlikely case it's multichannel surround ... */
        /* zipper the channel buffers together */
        todo = length > available ? available : length;
        if(s->type == SYNTH_TYPE_INT) {
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    ((* Sint16)stream)[i * s->channels + j] =
                        s->channelbuffers[j].intdata[i];
                }
            }
        } else {
            for(i = 0; i < todo; i++) {
                for(j = 0; j < s->channels; j++) {
                    ((* float)stream)[i * s->channels + j] =
                        s->channelbuffers[j].floatdata[i];
                }
            }
        }
        update_samples_available(s, todo);
        length =- todo;
        /* the buffer will not have been emptied if it wrapped around */
        if(length > 0) {
            available = get_samples_available(s);
            todo = length > available ? available : length;
            update_samples_available(s, todo);
            if(s->type == SYNTH_TYPE_INT) {
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((* Sint16)stream)[i * s->channels + j] =
                            s->channelbuffers[j].intdata[i];
                    }
                }
            } else {
                for(i = 0; i < todo; i++) {
                    for(j = 0; j < s->channels; j++) {
                        ((* float)stream)[i * s->channels + j] =
                            s->channelbuffers[j].floatdata[i];
                    }
                }
            }
            length -= todo;
            if(length > 0) {
                /* buffer consumed, and requested buffer not filled */
                s->underrun = 1;
            }
        }
    }
}

Synth *synth_new(synth_frame_cb_t synth_frame_cb,
                 void *synth_frame_priv) {
    SDL_AudioSpec desired, obtained;
    Synth *s;

    s = malloc(sizeof(Synth));
    if(s == NULL) {
        fprintf(stderr, "Failed to allocate synth.\n");
        return(NULL);
    }

    desired.freq = DEFAULT_RATE;
    desired.format = AUDIO_S16SYS;
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

    /* hopefully not too restrictive */
    if(obtained.format == AUDIO_S16SYS) {
        s->type = SYNTH_TYPE_INT;
    } else if(obtained.format == AUDIO_F32SYS) {
        s->type = SYNTH_TYPE_FLOAT;
    } else {
        fprintf(stderr, "Unusable SDL audio format: %04hX\n", obtained.format);
        SDL_CloseAudio();
        free(s);
        return(NULL);
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
    s->silence = obtained.silence;

    return(s);
}

void synth_free(Synth *s) {
    unsigned int i;

    if(s->buffer != NULL) {
        free(s->buffer);
    }

    if(s->channelbuffers != NULL) {
        for(i = 0; i < s->channels; i++) {
            if(s->channelbuffers[i].intdata != NULL) {
                free(s->channelbuffers[i].intdata);
            }
            if(s->channelbuffers[i].floatdata != NULL) {
                free(s->channelbuffers[i].floatdata);
            }
        }
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
            needed = synth_get_samples_needed(s);
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
            s->channelbuffers[i].intdata = NULL;
            s->channelbuffers[i].floatdata = NULL;
            s->channelbuffers[i].size = 0;
        }
    }

    s->buffersize = s->fragmentsize * fragments;
    for(i = 0; i < s->channels; i++) {
        if(s->type == SYNTH_TYPE_INT) {
            if(s->channelbuffer[i].intdata != NULL) {
                free(s->channelbuffer[i].intdata);
            }
            s->channelbuffer[i].intdata =
                malloc(sizeof(Sint16) * s->buffersize);
            if(s->channelbuffer[i].intdata == NULL) {
                fprintf(stderr, "Failed to allocate channel buffer memory.\n");
                for(i -= 1; i >= 0; i--) {
                    free(s->channelbuffer[i].intdata);
                    s->channelbuffer[i].intdata = NULL;
                }
                return(-1);
            }
            /* make sure buffers are silent to avoid risk of damaging ears or
             * equipment, especially if a script is expecting stereo but gets
             * a surround device. */
            memset(s->channelbuffer[i].intdata, s->silence, sizeof(Sint16) * s->buffersize);
        } else {
            if(s->channelbuffer[i].floatdata != NULL) {
                free(s->channelbuffer[i].floatdata);
            }
            s->channelbuffer[i].floatdata =
                malloc(sizeof(float) * s->buffersize);
            if(s->channelbuffer[i].floatdata == NULL) {
                fprintf(stderr, "Failed to allocate channel buffer memory.\n");
                for(i -= 1; i >= 0; i--) {
                    free(s->channelbuffer[i].floatdata);
                    s->channelbuffer[i].floatdata = NULL;
                }
                return(-1);
            }
            memset(s->channelbuffer[i].intdata, s->silence, sizeof(Sint16) * s->buffersize);
        }

        s->channelbuffer[i].size = s->buffersize;
    }

    return(0);
}

int synth_add_buffer(Synth *s,
                     SynthType type,
                     void *data,
                     unsigned int size) {
    unsigned int i, j;
    SynthBuffer *temp;

    /* first loaded buffer, so do some initial setup */
    if(s->buffersmem == 0) {
        s->buffer = malloc(sizeof(SynthBuffer));
        if(s->buffer == NULL) {
            LOG_PRINTF(ll, "Failed to buffers memory.\n");
            return(-1);
        }
        s->buffersmem = 1;
        s->buffer[0].type = type;
        s->buffer[0].size = size;
        if(data == NULL) {
            if(type == SYNTH_TYPE_INT) {
                s->buffer[0].intdata = (Sint16 *)data;
            } else {
                s->buffer[0].floatdata = (float *)data;
            }
        }
        s->buffer[0].ref = 0;
        return(s->channels);
    }

    /* find first NULL buffer and assign it */
    for(i = 0; i < s->buffersmem; i++) {
        if(s->buffer[i].size == 0) {
            s->buffer[i].type = type;
            s->buffer[i].size = size;
            if(type == SYNTH_TYPE_INT) {
                s->buffer[i].intdata = (Sint16 *)data;
            } else {
                s->buffer[i].floatdata = (float *)data;
            }
            s->buffer[i].ref = 0;
            return(s->channels + i);
        }
    }

    /* expand buffer if there's no free slots */
    temp = realloc(s->buffer,
                   sizeof(SyntbBuffer) * s->buffersmem * 2);
    if(temp == NULL) {
        LOG_PRINTF(ll, "Failed to allocate buffers memory.\n");
        return(-1);
    }
    s->buffer = temp;
    s->buffersmem *= 2;
    s->buffer[i].type = type;
    s->buffer[i].size = size;
    if(type == SYNTH_TYPE_INT) {
        s->buffer[i].intdata = (Sint16 *)data;
    } else {
        s->buffer[i].floatdata = (float *)data;
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

    s->buffer[index].size = 0;

    return(0);
}
