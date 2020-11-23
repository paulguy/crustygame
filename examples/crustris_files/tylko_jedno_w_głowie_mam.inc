; transcribed from https://musescore.com/user/32664597/scores/6382493
; very awful crusty early version.  Need to make more instruments

expr _SONG_COWDANCE_CHANNELS 2

static cowdance1_seq string "\
4eEEAAEEqCe.CqEe.EqEEeXqDe.DqDDeXEAAEEqC\
EeAAEECXCEED#EDqEeEDD.DqDeXEAAEEC"
; this sequence has no volume dynamics, and the parameter sequencer always
; fetches the last value after it's reached the end
static cowdance1_volstarts floats "1.0"
static cowdance1_voldiffs floats "0.0"
static cowdance1_volspeeds floats "1.0"
static cowdance1_speedstarts floats "1.0"
static cowdance1_speeddiffs floats "0.0"
static cowdance1_speedspeeds floats "1.0"
SEQUENCE_MAKE_TONE_CHANNEL saw cowdance1_seq 4 120 cowdance1_volstarts cowdance1_voldiffs cowdance1_volspeeds cowdance1_speedstarts cowdance1_speeddiffs cowdance1_speedspeeds

static cowdance2_seq string "\
wXXXXqXe3AEAEAEAEECECECECD2A3D2A3D2A3D2A3D2A3D2A"
static cowdance2_volstarts floats "1.0"
static cowdance2_voldiffs floats "0.0"
static cowdance2_volspeeds floats "1.0"
static cowdance2_speedstarts floats "1.0"
static cowdance2_speeddiffs floats "0.0"
static cowdance2_speedspeeds floats "1.0"
SEQUENCE_MAKE_TONE_CHANNEL saw cowdance2_seq 4 120 cowdance2_volstarts cowdance2_voldiffs cowdance2_volspeeds cowdance2_speedstarts cowdance2_speeddiffs cowdance2_speedspeeds

static sineobj
static sawobj

proc song_cowdance_start
    call inst_sine_init
    call inst_saw_init
    SEQUENCE_CHANNEL_INIT cowdance1_seq 0
    SEQUENCE_CHANNEL_INIT cowdance2_seq 0
ret

proc song_cowdance_run needed
    SEQUENCE_PLAY_LOCALS

    SEQUENCE_CHANNEL_PLAY cowdance1_seq
    SEQUENCE_CHANNEL_PLAY cowdance2_seq
    SEQUENCE_PLAY_CHANNEL sine cowdance1 sineobj
    SEQUENCE_PLAY_CHANNEL saw  cowdance2 sawobj

    cmp ended _SONG_COWDANCE_CHANNELS
    jumpl end
        SEQUENCE_CHANNEL_START cowdance1_seq
        SEQUENCE_CHANNEL_START cowdance2_seq
    label end
ret

proc song_cowdance_end
    SEQUENCE_CHANNEL_FREE cowdance2_seq
    SEQUENCE_CHANNEL_FREE cowdance1_seq
    call inst_saw_free
    call inst_sine_free
ret
