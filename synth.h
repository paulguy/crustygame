typedef int (*synth_frame_cb_t)(void *priv);
typedef void (*synth_log_cb_t)(void *priv, const char *fmt, ...);

/* most common formats */
typedef enum {
    SYNTH_TYPE_U8,
    SYNTH_TYPE_S16,
    SYNTH_TYPE_F32
} SynthImportType;

typedef struct Synth;

unsigned int synth_get_samples_needed(Synth *s);
Synth *synth_new(synth_frame_cb_t synth_frame_cb,
                 void *synth_frame_priv,
                 synth_log_cb_t synth_log_cb,
                 void *synth_log_priv);
void synth_free(Synth *s);
unsigned int synth_get_rate(Synth *s);
unsigned int synth_get_channels(Synth *s);
unsigned int synth_get_fragment_size(Synth *s);
int synth_has_underrun(Synth *s);
int audio_set_enabled(Synth *s, int enabled);
int synth_frame(Synth *s);
int synth_set_fragments(Synth *s,
                        unsigned int fragments);
int synth_add_buffer(Synth *s,
                     SynthImportType type,
                     void *data,
                     unsigned int size);
int synth_free_buffer(Synth *s, unsigned int index);
