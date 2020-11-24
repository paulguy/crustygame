expr _INST_MAX_INST_OBJS 10
; pretty big but it's 1 second at a crazy high sample rate so it should be
; relatively reasonable.  Better than a bunch of things having their own
; buffers
expr SEQUENCE_USER_SCRATCH_MEM_SIZE 192000

static _inst_scratch_mem floats SEQUENCE_USER_SCRATCH_MEM_SIZE
static _inst_scratch_buffer

static _inst_env_len
static _inst_env_asc
static _inst_env_desc
static _inst_env_const

; some state is needed for this specific instrument
macro INST_TONE_PLAY_LOCALS
    local volstart
    local voldiff
    local volspeed
    local speedstart
    local speeddiff
    local speedspeed
endmacro INST_TONE_PLAY_LOCALS

; set up some buffers that are generally useful
proc inst_init
    local buf
    local temp ints 2

    ; make it simple so the length is determined by a ratio of a second
    SEQUENCE_CALC_LENGTH _inst_env_len 1

    call audio_add_empty_buffer _inst_env_len
    move _inst_scratch_buffer get_return

    call wave_make_saw_ascending _inst_scratch_mem _inst_env_len 0 1
    move set_buffer _inst_scratch_mem
    move temp:0 AUDIO_TYPE_F64
    move temp:1 _inst_env_len
    move audio_add_buffer temp
    move _inst_env_asc get_return

    call wave_make_saw_descending _inst_scratch_mem _inst_env_len 0 1
    move set_buffer _inst_scratch_mem
    move temp:0 AUDIO_TYPE_F64
    move temp:1 _inst_env_len
    move audio_add_buffer temp
    move _inst_env_desc get_return

    call wave_make_dc _inst_scratch_mem _inst_env_len 1
    move set_buffer _inst_scratch_mem
    move temp:0 AUDIO_TYPE_F64
    move temp:1 _inst_env_len
    move audio_add_buffer temp
    move _inst_env_const get_return
ret

proc inst_free
    move audio_free_buffer _inst_env_const
    move audio_free_buffer _inst_env_desc
    move audio_free_buffer _inst_env_asc
ret

expr _INST_USED_IDX 0

static _inst_temp_setget

; HANDLE should always be a multiple of the integer member size, so to get the
; offset in to the start of the object in the float data memory, just recale
; from the size of an intmem object to a floatmem object
macro _INST_MAKE_SETGET NAME INTMEM MEMBERSIZE MEMORYSIZE FLOATMEM FLOATSIZE
    macro _INST_NAME_SET_INT HANDLE OFFSET VAL
        move _inst_temp_setget HANDLE
        add  _inst_temp_setget OFFSET
        move INTMEM:_inst_temp_setget VAL
    endmacro _INST_NAME_SET_INT

    macro _INST_NAME_GET_INT RESULT HANDLE OFFSET
        move _inst_temp_setget HANDLE
        add  _inst_temp_setget OFFSET
        move RESULT INTMEM:_inst_temp_setget
    endmacro _INST_NAME_GET_INT

    macro _INST_NAME_SET_FLOAT HANDLE OFFSET VAL
        move _inst_temp_setget HANDLE
        div  _inst_temp_setget MEMBERSIZE
        mul  _inst_temp_setget FLOATSIZE
        add  _inst_temp_setget OFFSET
        move FLOATMEM:_inst_temp_setget VAL
    endmacro _INST_NAME_SET_FLOAT

    macro _INST_NAME_GET_FLOAT RESULT HANDLE OFFSET
        move _inst_temp_setget HANDLE
        div  _inst_temp_setget MEMBERSIZE
        mul  _inst_temp_setget FLOATSIZE
        add  _inst_temp_setget OFFSET
        move RESULT FLOATMEM:_inst_temp_setget
    endmacro _INST_NAME_GET_FLOAT

    macro _INST_NAME_FIND RESULT
        move RESULT MEMORYSIZE
        label loop
            sub RESULT MEMBERSIZE
            jumpl end
            cmp INTMEM:RESULT 0
            jumpz found
        jump loop
        label found
        ; mark used
        move INTMEM:RESULT 1
    endmacro _INST_NAME_FIND
endmacro _INST_MAKE_SETGET

; ========== Tone Instrument ==========

;<used idx> 0
expr _INST_TONE_LENGTH_IDX 1
expr _INST_TONE_PLAYER_IDX 2
expr _INST_TONE_SPEEDSLOPE_PLAYER_IDX 3
expr _INST_TONE_SPEEDBIAS_PLAYER_IDX 4
expr _INST_TONE_OUTPOS_IDX 6
expr _INST_TONE_NOTE_IDX 7
expr _INST_TONE_OBJSIZE 8

expr _INST_TONE_OBJMEM_SIZE "_INST_MAX_INST_OBJS * _INST_TONE_OBJSIZE"

static _inst_tone_objmem ints _INST_TONE_OBJMEM_SIZE

; no float members, so just specify a filler.  No code will be generated from
; it so it shouldn't produce any errors.
_INST_MAKE_SETGET TONE _inst_tone_objmem _INST_TONE_OBJSIZE _INST_TONE_OBJMEM_SIZE none 0

proc inst_tone_init hInst outbuffer inbuffer length
    local temp ints 2
    local speedbiasplayer
    local speedslopeplayer
    local player

    _INST_TONE_FIND hInst

    move audio_add_player _inst_env_const
    move freqbiasplayer get_return
    move audio_set_player_mode:freqbiasplayer AUDIO_MODE_LOOP
    move audio_set_player_output_mode:freqbiasplayer AUDIO_OUTPUT_MODE_ADD
    move audio_set_player_output_buffer:freqbiasplayer _inst_scratch_buffer

    move audio_add_player _inst_env_asc
    move freqslopeplayer get_return
    move audio_set_player_mode:freqslopeplayer AUDIO_MODE_ONCE
    move audio_set_player_output_mode:freqslopeplayer AUDIO_OUTPUT_MODE_REPLACE
    move audio_set_player_output_buffer:freqslopeplayer _inst_scratch_buffer

    move audio_add_player inbuffer
    move player get_return
    move audio_set_player_mode:player AUDIO_MODE_LOOP
    move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_ADD
    move audio_set_player_speed_source:player _inst_scratch_buffer
    move audio_set_player_output_buffer:player outbuffer

    _INST_TONE_SET_INT hInst _INST_TONE_LENGTH_IDX length
    _INST_TONE_SET_INT hInst _INST_TONE_PLAYER_IDX player
    _INST_TONE_SET_INT hInst _INST_TONE_SPEEDSLOPE_PLAYER_IDX speedslopeplayer
    _INST_TONE_SET_INT hInst _INST_TONE_SPEEDBIAS_PLAYER_IDX speedbiasplayer
    _INST_TONE_SET_INT hInst _INST_TONE_MODE_IDX _INST_TONE_MODE_OFF
    _INST_TONE_SET_INT hInst _INST_TONE_NOTE_IDX 0

    ; _INST_*_FIND needs an end label to quit out on failure
    label end
ret

proc inst_tone_free hInst
    local temp

    _INST_TONE_GET_INT temp hInst _INST_TONE_PLAYER_IDX
    move audio_free_player temp
    _INST_TONE_GET_INT temp hInst _INST_TONE_ENVSLOPE_PLAYER_IDX
    move audio_free_player temp
    _INST_TONE_GET_INT temp hInst _INST_TONE_ENVBIAS_PLAYER_IDX
    move audio_free_player temp
    _INST_TONE_GET_INT temp hInst _INST_TONE_SPEEDSLOPE_PLAYER_IDX
    move audio_free_player temp
    _INST_TONE_GET_INT temp hInst _INST_TONE_SPEEDBIAS_PLAYER_IDX
    move audio_free_player temp
    _INST_TONE_SET_INT hInst _INST_USED_IDX 0
ret

; note 0 is rest/off
proc inst_tone_note hInst note remain detune speeddiff speedspeed
    local temp
    local oldnote
    local player
    local speedslopeplayer
    local speedbiasplayer
 
    local tempf floats 2
    local freq floats 1
    local speed floats 1

    _INST_TONE_GET_INT oldnote hInst _INST_TONE_NOTE_IDX
    _INST_TONE_SET_INT hInst _INST_TONE_NOTE_IDX note

    ; check if the note changed to note off
    cmp note 0
    jumpz end
    ; check if the note changed from a note off
    cmp oldnote 0
    jumpz note_on
    ; retrigger note on if the note changed
    cmp note oldnote
    jumpn note_on
    ; if the note didn't change, just apply any bend
    jump check_bend

    label note_on
        _INST_TONE_SET_INT hInst _INST_TONE_MODE_IDX _INST_TONE_MODE_ATTACK
        _INST_TONE_GET_INT player hInst _INST_TONE_PLAYER_IDX

        ; get the speed
        call sequence_get_note_freq freq note
        _INST_TONE_GET_INT temp hInst _INST_TONE_LENGTH_IDX
        SEQUENCE_CALC_SPEED tempf speed temp freq
        ; apply any base detune
        mul  speed detune
        ; all bends are starting detuned and bending _to_ the final note.
        ; It is done this way because it seems more useful to describe the note
        ; being bent to in the music, and it's simpler because the mode can
        ; just be set to constant once the envelope is up without having to
        ; store any state on what the speed should be
        move audio_set_player_speed:player speed
 
    label check_bend
        cmp speeddiff 0
        jumpz constant

        move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_SOURCE
        _INST_TONE_GET_INT speedbiasplayer hInst _INST_TONE_SPEEDBIAS_PLAYER_IDX
        _INST_TONE_GET_INT speedslopeplayer hInst _INST_TONE_SPEEDSLOPE_PLAYER_IDX
        move audio_set_player_speed:speedslopeplayer speedspeed

        cmp speeddiff 0
        jumpg bend_up
        ; bend down
            mul speeddiff -1
            ; needs to descend down to 1.0
            move audio_set_player_input_buffer:speedslopeplayer _inst_env_desc
            move audio_set_player_volume:speedslopeplayer speeddiff
            ; bias ends on 1.0
            move tempf 1
            move audio_set_player_volume:speedbiasplayer tempf
    jump end
        label bend_up
            ; needs to ascend up from the bias point to 1.0
            move audio_set_player_input_buffer:speedslopeplayer _inst_env_asc
            move tempf 1
            sub  tempf speeddiff
            move audio_set_player_volume:speedbiasplayer tempf
            move audio_set_player_volume:speedslopeplayer speeddiff
    jump end
    label constant
        move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_CONSTANT
    label end
ret

proc inst_tone_frame hInst needed
    ; don't reset input position, since the phase should be preserved
    _INST_TONE_SET_INT hInst _INST_TONE_OUTPOS_IDX 0
ret

proc inst_tone_run hInst request
    local temp
    local note
    local player
    local speedslopeplayer
    local speedbiasplayer
    local outpos
    local remain
    local speedgot

    _INST_TONE_GET_INT note hInst _INST_TONE_NOTE_IDX
    cmp note 0
    jumpz off

    _INST_TONE_GET_INT player hInst _INST_TONE_PLAYER_IDX
    _INST_TONE_GET_INT speedslopeplayer hInst _INST_TONE_SPEEDSLOPE_PLAYER_IDX
    _INST_TONE_GET_INT speedbiasplayer hInst _INST_TONE_SPEEDBIAS_PLAYER_IDX
    _INST_TONE_GET_INT outpos hInst _INST_TONE_OUTPOS_IDX

    move remain request
    label loop
        ; always try to run the speedslope player.  if nothing was gotten from
        ; it, just set the mode and pretend it got the whole thing
        move audio_player_run:speedslopeplayer remain
        move speedgot get_return
        cmp speedgot 0
        jumpg gotspeed
            move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_CONSTANT
            move speedgot remain
        jump done_speed
        label gotspeed
        move audio_player_run:speedbiasplayer speedgot
        label done_speed

        move audio_player_run:player speedgot

        sub remain speedgot
        cmp remain 0
    jumpg loop
    _INST_TONE_SET_INT outpos hInst _INST_TONE_OUTPOS_IDX outpos

    label off
ret

; ========== Simple Envelope Effect ==========

;<used idx> 0
expr _INST_FX_ENV_PLAYER_IDX 1
expr _INST_FX_ENV_ENVSLOPE_PLAYER_IDX 2
expr _INST_FX_ENV_ENVBIAS_PLAYER_IDX 3
expr _INST_FX_ENV_ENDVOL_WHOLE_IDX 4
expr _INST_FX_ENV_ENDVOL_FRAC_IDX 5
expr _INST_FX_ENV_OUTPOS_IDX 6
expr _INST_FX_ENV_OBJSIZE 7

expr _INST_FX_ENV_ENDVOL_IDX 0
expr _INST_FX_ENV_FLOATSIZE 1

expr _INST_FX_ENV_OBJMEM_SIZE "_INST_MAX_INST_OBJS * _INST_FX_ENV_OBJSIZE"
expr _INST_FX_ENV_FLOATS_SIZE "_INST_MAX_INST_OBJS * _INST_FX_ENV_FLOATSIZE"

static _inst_fx_env_objmem ints _INST_FX_ENV_OBJMEM_SIZE
static _inst_fx_env_floats floats _INST_FX_ENV_FLOATS_SIZE

_INST_MAKE_SETGET FX_ENV _inst_fx_env_objmem _INST_FX_ENV_OBJSIZE _INST_FX_ENV_OBJMEM_SIZE _inst_fx_env_floats _INST_FX_ENV_FLOATSIZE

proc effect_env_init hInst outbuffer inbuffer
    local temp
    local player
    local envslopeplayer
    local envbiasplayer

    _INST_FX_ENV_FIND hInst

    move audio_add_player _inst_env_asc
    move envslopeplayer get_return
    move audio_set_player_mode:player AUDIO_MODE_ONCE
    move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_REPLACE
    move audio_set_player_output_buffer:player _inst_scratch_buffer

    move audio_add_player _inst_env_const
    move envbiasplayer get_return
    move audio_set_player_mode:player AUDIO_MODE_LOOP
    move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_ADD
    move audio_set_player_output_buffer:player _inst_scratch_buffer

    move audio_add_player inbuffer
    move player get_return
    move audio_set_player_mode:player AUDIO_MODE_ONCE
    move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_ADD
    move ausio_set_player_volume_source:player _inst_scratch_buffer
    move audio_set_player_output_buffer:player outbuffer

    _INST_FX_ENV_SET_INT hInst _INST_FX_ENV_PLAYER_IDX player
    _INST_FX_ENV_SET_INT hInst _INST_FX_ENV_ENVSLOPE_PLAYER_IDX envslopeplayer
    _INST_FX_ENV_SET_INT hInst _INST_FX_ENV_ENVBIAS_PLAYER_IDX envbiasplayer

    ; _INST_*_FIND needs an end label to quit out on failure
    label end
ret

proc effect_env_free hInst
    local temp

    _INST_FX_ENV_GET_INT temp hInst _INST_FX_ENV_PLAYER_IDX
    move audio_free_player temp
    _INST_FX_ENV_GET_INT temp hInst _INST_FX_ENV_ENVSLOPE_PLAYER_IDX
    move audio_free_player temp
    _INST_FX_ENV_GET_INT temp hInst _INST_FX_ENV_ENVBIAS_PLAYER_IDX
    move audio_free_player temp
    _INST_FX_ENV_SET_INT hInst _INST_USED_IDX 0
ret

proc effect_env_param hInst start end speed
    local tempf floats 1
    local diff floats 1
    local player
    local envslopeplayer
    local envbiasplayer

    _INST_FX_ENV_GET_INT player hInst _INST_FX_ENV_PLAYER_IDX
    cmp start end
    jumpl ascending
    jumpg descending
    ; constant volume
    move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_CONSTANT
    move audio_set_player_volume:player start
    jumpz end
    label descending
        move audio_set_player_input_buffer:envslopeplayer _inst_env_desc
        move diff start
        sub  diff end
        move audio_set_player_volume:envslopeplayer diff
        move audio_set_player_volume:envbiasplayer  end
    jump slope
    label ascending
        move audio_set_player_input_buffer:envslopeplayer _inst_env_asc
        move diff end
        sub  diff start
        move audio_set_player_volume:envslopeplayer diff
        move audio_set_player_volume:envbiasplayer  start
    label slope
        _INST_FX_ENV_GET_INT envslopeplayer hInst _INST_FX_ENV_ENVSLOPE_PLAYER_IDX
        _INST_FX_ENV_GET_INT envbiasplayer hInst _INST_FX_ENV_ENVBIAS_PLAYER_IDX

        ; set player to source from the envplayer, and have the bias and slope
        ; dictate the volume level.  Store the end volume level for later so
        ; when the envelope is consumed, it can just be used as a constant
        move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_SOURCE
        move audio_set_player_volume_source:player _inst_scratch_buffer
        move tempf 1
        move audio_set_player_volume:player tempf
        move audio_set_player_speed:envslopeplayer speed
        _INST_FX_EMV_SET_FLOAT hInst _INST_FX_ENV_ENDVOL_IDX temp

    label end
ret

proc effect_env_frame hInst
    local player

    ; reset output position and input position, the assumption is that on each
    ; frame, an intermediate buffer is reused, otherwise this procedure can
    ; just be used as a generic position reset
    _INST_FX_ENV_SET_INT hInst _INST_FX_ENV_OUTPOS_IDX 0
    _INST_FX_ENV_GET_INT player hInst _INST_FX_ENV_PLAYER_IDX
    move audio_set_input_buffer_pos:player 0
ret

proc effect_env_run hInst request
    local tempf floats 1
    local player
    local envslopeplayer
    local envbiasplayer
    local outpos
    local slopegot
    local remain

    _INST_FX_ENV_GET_INT envslopeplayer hInst _INST_FX_ENV_ENVSLOPE_PLAYER_IDX
    _INST_FX_ENV_GET_INT envbiasplayer hInst _INST_FX_ENV_ENVBIAS_PLAYER_IDX
    _INST_FX_ENV_GET_INT player hInst _INST_FX_ENV_PLAYER_IDX
    _INST_FX_ENV_GET_INT outpos hInst _INST_FX_ENV_OUTPOS_IDX

    move audio_set_player_output_pos:player outpos

    move remain request

    move audio_run_player:envslopeplayer remain
    move slopegot get_return

    cmp slopegot 0
    jumpz constant
        move audio_run_player:envbiasplayer slopegot
        move audio_run_player:player slopegot
        add outpos slopegot
        sub remain slopegot

        cmp remain 0
        jumpz end
    label constant
        move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_CONSTANT
        _INST_FX_ENV_GET_FLOAT tempf hInst _INST_FX_ENV_ENDVOL_IDX
        move audio_set_player_volume:player tempf
        move audio_run_player:player remain
        add outpos remain

    label end
    _INST_FX_ENV_SET_INT hInst _INST_FX_ENV_OUTPOS_IDX outpos
ret

; ========== ADSR Generator ==========

;<used idx> 0
expr _INST_ADSR_ENVSLOPE_PLAYER_IDX 1
expr _INST_ADSR_ENVBIAS_PLAYER_IDX 2
expr _INST_ADSR_MODE_IDX 3
expr _INST_ADSR_OBJSIZE 4

expr _INST_ADSR_DECAYRATE_IDX 0
expr _INST_ADSR_SUSTAINLEVEL_IDX 1
expr _INST_ADSR_FLOATSIZE 2

expr _INST_ADSR_OBJMEM_SIZE "_INST_MAX_INST_OBJS * _INST_ADSR_OBJSIZE"
expr _INST_ADSR_FLOATS_SIZE "_INST_MAX_INST_OBJS * _INST_ADSR_FLOATSIZE"

static _inst_adsr_objmem ints _INST_ADSR_OBJMEM_SIZE
static _inst_adsr_floats floats _INST_ADSR_FLOATS_SIZE

_INST_MAKE_SETGET ADSR _inst_adsr_objmem _INST_ADSR_OBJSIZE _INST_ADSR_OBJMEM_SIZE _inst_adsr_floats _INST_ADSR_FLOATSIZE

expr _INST_ADSR_MODE_ATTACK 0
expr _INST_ADSR_MODE_DECAY 1
expr _INST_ADSR_MODE_SUSTAIN 2
expr _INST_ADSR_MODE_RESONATE 3

proc gen_adsr_init hInst outbuffer
    local temp
    local envslopeplayer
    local envbiasplayer

    _INST_ADSR_FIND hInst

    move audio_add_player _inst_env_asc
    move envslopeplayer get_return
    move audio_set_player_mode:player AUDIO_MODE_ONCE
    move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_REPLACE
    move audio_set_player_output_buffer:player outbuffer

    move audio_add_player _inst_env_const
    move envbiasplayer get_return
    move audio_set_player_mode:player AUDIO_MODE_LOOP
    move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_ADD
    move audio_set_player_output_buffer:player outbuffer

    _INST_ADSR_SET_INT hInst _INST_ADSR_PLAYER_IDX player
    _INST_ADSR_SET_INT hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX envslopeplayer
    _INST_ADSR_SET_INT hInst _INST_ADSR_ENVBIAS_PLAYER_IDX envbiasplayer
    ; meaningless but safe defaults
    call get_adsr_start hInst 1 1 1

    ; _INST_*_FIND needs an end label to quit out on failure
    label end
ret

proc gen_adsr_free hInst
    local temp

    _INST_ADSR_GET_INT temp hInst _INST_ADSR_PLAYER_IDX
    move audio_free_player temp
    _INST_ADSR_GET_INT temp hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX
    move audio_free_player temp
    _INST_ADSR_GET_INT temp hInst _INST_ADSR_ENVBIAS_PLAYER_IDX
    move audio_free_player temp
    _INST_ADSR_SET_INT hInst _INST_USED_IDX 0
ret

proc gen_adsr_start hInst attackrate decayrate sustainlevel
    local envslopeplayer
    local envbiasplayer

    _INST_ADSR_GET_INT envslopeplayer hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX
    _INST_ADSR_GET_INT envbiasplayer hInst _INST_ADSR_ENVBIAS_PLAYER_IDX

    move audio_set_player_input_buffer:envslopeplayer _inst_env_asc
    move audio_set_player_speed:envslopeplayer attackrate

    ; reset the positions to 0 since this will be at the start of a note, so
    ; the output can be used as the input to a note's volume easily.
    _INST_ADSR_GET_INT temp hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX
    move audio_set_player_output_buffer_pos:temp 0
    _INST_ADSR_GET_INT temp hInst _INST_ADSR_ENVBIAS_PLAYER_IDX
    move audio_set_player_output_buffer_pos:temp 0
ret

    _INST_ADSR_SET_FLOAT hInst _INST_ADSR_DECAYRATE_IDX decayrate
    _INST_ADSR_SET_FLOAT hInst _INST_ADSR_SUSTAINLEVEL_IDX sustainlevel
    _INST_ADSR_SET_INT hInst _INST_ADSR_RESONATERATE_IDX _INST_ADSR_MODE_ATTACK
ret

proc gen_adsr_resonate hInst resonaterate
    local envslopeplayer
    local envbiasplayer
    local sustainlevel

    _INST_ADSR_GET_INT envslopeplayer hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX
    _INST_ADSR_GET_INT envbiasplayer hInst _INST_ADSR_ENVBIAS_PLAYER_IDX
    _INST_ADSR_GET_FLOAT sustainlevel hInst _INST_ADSR_SUSTAINLEVEL_IDX

    move audio_set_player_input_buffer:envslopeplayer _inst_env_desc
    move audio_set_player_volume:envslopeplayer sustainlevel
    move audio_set_player_speed:envslopeplayer resonaterate

    _INST_ADSR_SET_INT hInst _INST_ADSR_RESONATERATE_IDX _INST_ADSR_MODE_RESONATE
ret

proc gen_adsr_frame hInst
    local temp

    ; just set the output positions to 0, because this will be called as a
    ; generator to determine how much of a note is to be played
    _INST_ADSR_GET_INT temp hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX
    move audio_set_player_output_buffer_pos:temp 0
    _INST_ADSR_GET_INT temp hInst _INST_ADSR_ENVBIAS_PLAYER_IDX
    move audio_set_player_output_buffer_pos:temp 0
ret

proc gen_adsr_run hInst request returned
    local tempf floats 2
    local envslopeplayer
    local envbiasplayer
    local mode
    local slopegot
    local remain

    _INST_ADSR_GET_INT mode hInst _INST_ADSR_MODE_IDX

    move remain request
    label loop
        cmp remain 0
        jumpz end

        cmp mode _INST_ADSR_MODE_SUSTAIN
        jumpz sustain
            _INST_ADSR_GET_INT envslopeplayer hInst _INST_ADSR_ENVSLOPE_PLAYER_IDX
            move audio_run_player:envslopeplayer request
            move slopegot get_return
            cmp slopegot 0
            jumpz next

            sub remain slopegot

            cmp mode _INST_ADSR_MODE_DECAY
            jumpn no_bias
                move audio_run_player:envbiasplayer slopegot
            label no_bias
        jump loop
        label next
            cmp mode _INST_ADSR_MODE_ATTACK
            jumpz decay
            cmp mode _INST_ADSR_MODE_DECAY
            jumpz sustain
            jump end ; resonate ended
            label sustain
                _INST_ADSR_SET_INT hInst _INST_ADSR_MODE_IDX _INST_ADSR_MODE_SUSTAIN
                move mode _INST_ADSR_MODE_SUSTAIN
            jump sustain
            label decay
                move audio_set_player_input_buffer:envslopeplayer _inst_env_desc
                _INST_ADSR_GET_FLOAT tempf hInst _INST_ADSR_DECAYRATE_IDX
                move audio_set_player_speed:envslopeplayer tempf
                _INST_ADSR_GET_FLOAT tempf hInst _INST_ADSR_SUSTAINLEVEL_IDX
                move tempf:1 1
                sub  tempf:1 tempf
                move audio_set_player_volume:envslopeplayer tempf:1
                move audio_set_player_volume:envbiasplayer tempf

                _INST_ADSR_SET_INT hInst _INST_ADSR_MODE_IDX _INST_ADSR_MODE_DECAY
                move mode _INST_ADSR_MODE_DECAY
    jump loop
    label sustain
        move audio_run_player:envbiasplayer remain
        move remain 0
    label end
        move returned request
        sub  returned remain
ret
