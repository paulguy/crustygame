; transcribed from https://musescore.com/user/32664597/scores/6382493
; very awful crusty early version.  Need to make more instruments

expr _SONG_COWDANCE_CHANNELS 2

static cowdance1 string "\
4eEEAAEEqCe.CqEe.EqEEeXqDe.DqDDeXEAAEEqC\
EeAAEECXCEED#EDqEeEDD.DqDeXEAAEEC"
SEQUENCE_MAKE_SEQ cowdance1 4 120
static cowdance2 string "\
wXXXXqXme3AEAEpAEAEmECECpECECmD2A3D2A3D2Ap3D2A3D2A3D2A"
SEQUENCE_MAKE_SEQ cowdance2 4 120

static sineobj
static sawobj

proc song_cowdance_start
    call inst_sine_init
    call inst_saw_init
    SEQUENCE_GET_INST_OBJ sine sineobj
    SEQUENCE_START cowdance1
    SEQUENCE_GET_INST_OBJ saw  sawobj
    SEQUENCE_START cowdance2
ret

proc song_cowdance_run needed
    SEQUENCE_PLAY_LOCALS

    SEQUENCE_PLAY_CHANNEL sine cowdance1 sineobj
    SEQUENCE_PLAY_CHANNEL saw  cowdance2 sawobj

    cmp ended _SONG_COWDANCE_CHANNELS
    jumpl end
        SEQUENCE_START cowdance1
        SEQUENCE_START cowdance2
    label end
ret

proc song_cowdance_end
    SEQUENCE_FREE_INST_OBJ saw  sawobj
    SEQUENCE_FREE_INST_OBJ sine sineobj
    call inst_saw_free
    call inst_sine_free
ret
