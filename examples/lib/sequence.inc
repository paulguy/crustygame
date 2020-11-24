static _SEQUENCE_NOTES string "CCDDEFFGGAAB"
static _SEQUENCE_SHARP string "#"
static _SEQUENCE_FLAT  string "b"
static _SEQUENCE_REST  string "X"
static _SEQUENCE_OCTAVES string "0123456789"
static _SEQUENCE_DIVS  string "whqestio"
static _SEQUENCE_DIV_VALS ints "1 2 4 8 16 32 64 128"
; Whole, Half, Quarter, Eighth, Sixteenth,
; Thirdtysecond, sIxtyfourth, Onetwentyeighth
static _SEQUENCE_SLUR string "_"
static _SEQUENCE_DOT string "."
static _SEQUENCE_DOT_MUL floats "1.5"
static _SEQUENCE_TUNINGS floats 12
static _sequence_rate

macro SEQUENCE_SET_RATE RATE
    move _sequence_rate RATE
endmacro SEQUENCE_SET_RATE

proc _sequence_get_next_note note len div mul tempo octave slurred seq pos
    local i
    local dotted 0

    move slurred 0

    label try_find_note
        cmp pos seq:
        jumpl not_finished
            ; indicate the sequence finished
            move note -1
            jump end
        label not_finished

        move i 0
        label find_note
            cmp seq:pos _SEQUENCE_NOTES:i
            jumpz found_note
            add i 1
            cmp i _SEQUENCE_NOTES:
        jumpl find_note
        jump not_note
        label found_note
            add pos 1
            move note 12
            mul  note octave
            add  note i
            add  note 1 ; 0 is rest
            cmp pos seq:
        jumpz end_note
            cmp seq:pos _SEQUENCE_SHARP
            jumpz note_sharp
            cmp seq:pos _SEQUENCE_FLAT
            jumpz note_flat
        jump end_note
        label note_sharp
            add pos 1
            add note 1
        jump end_note
        label note_flat
            add pos 1
            sub note 1
        jump end_note
        label not_note

        move i 0
        label find_mul
            cmp seq:pos _SEQUENCE_DIVS:i
            jumpz found_mul
            add i 1
            cmp i _SEQUENCE_DIVS:
        jumpl find_mul
        jump not_mul
        label found_mul
            add pos 1
            move mul _SEQUENCE_DIV_VALS:i
        jump try_find_note
        label not_mul
            
        move i 0
        label find_octave
            cmp seq:pos _SEQUENCE_OCTAVES:i
            jumpz found_octave
            add i 1
            cmp i _SEQUENCE_OCTAVES:
        jumpl find_octave
        jump not_octave
        label found_octave
            add pos 1
            move octave i
        jump try_find_note
        label not_octave
 
        cmp seq:pos _SEQUENCE_REST
        jumpn not_rest
            add pos 1
            move note 0
            div  len div
        jump end_note
        label not_rest

        cmp seq:pos _SEQUENCE_SLUR
        jumpn not_slurred
            add pos 1
            move slurred 1
        jump try_find_note
        label not_slurred

        cmp seq:pos _SEQUENCE_DOT
        jumpn not_dotted
            add pos 1
            move dotted 1
        jump try_find_note
        label not_dotted

        ; ignore invalid characters
        add pos 1
    jump try_find_note
    label end_note
        move len _sequence_rate
        mul  len 60
        div  len tempo
        ; this is a length and not rate so the divisions increase the length of a
        ; note and the multiplying of the notes shortens them
        mul  len div
        div  len mul
        cmp dotted
        jumpz end
        mul len _SEQUENCE_DOT_MUL
    label end
ret

macro SEQUENCE_MAKE_SEQ SEQ DIV TEMPO
    static _sequence_start_notediv_SEQ DIV
    static _sequence_start_tempo_SEQ TEMPO
    static _sequence_notediv_SEQ
    static _sequence_tempo_SEQ
    static _sequence_notemul_SEQ
    static _sequence_pos_SEQ
    static _sequence_note_SEQ
    static _sequence_octave_SEQ
    static _sequence_slurred_SEQ
    static _sequence_notelen_SEQ
    static _sequence_notetime_SEQ
    static _sequence_time_SEQ
    static _sequence_notes_missed_SEQ
    static _sequence_consumed_SEQ

    proc _sequence_start_SEQ
        move _sequence_notediv_SEQ _sequence_start_notediv_SEQ
        move _sequence_tempo_SEQ _sequence_start_tempo_SEQ
        move _sequence_notemul_SEQ 1
        move _sequence_consumed_SEQ 0
        move _sequence_pos_SEQ 0
        move _sequence_octave_SEQ 4
        move _sequence_time_SEQ 0
        move _sequence_notetime_SEQ 0
        call _sequence_get_next_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_notemul_SEQ _sequence_tempo_SEQ _sequence_octave_SEQ _sequence_slurred_SEQ SEQ _sequence_pos_SEQ
        move _sequence_notes_missed_SEQ 0
    ret

    ; note = 0 -> note off/rest
    ; remain = -1 -> sequence ended
    proc _sequence_get_cur_note_SEQ note remain
        cmp _sequence_note_SEQ 0
        jumpl finished

        ; get amount of time remaining for this note
        move remain _sequence_notelen_SEQ
        add  remain _sequence_notetime_SEQ
        sub  remain _sequence_time_SEQ

        ; if the note is shorter than the amount of time which has passed,
        ; indicate note off first then the next call should return the next
        ; note
        cmp remain 0
        jumpz next_note
        ; just return the note
            move note _sequence_note_SEQ
        jump end

        label next_note
            ; advance the time
            add _sequence_notetime_SEQ _sequence_notelen_SEQ

            ; get the next note information
            call _sequence_get_next_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_notemul_SEQ _sequence_tempo_SEQ _sequence_octave_SEQ _sequence_slurred_SEQ SEQ _sequence_pos_SEQ

            ; see if the sequence has ended
            cmp _sequence_note_SEQ 0
        jumpl finished

            ; in case of a slurred note, don't pass the note off evend, just
            ; pass the next note
            cmp _sequence_slurred_SEQ
        jumpn pass
            ; if it's a rest, just pass it on as well
            cmp _sequence_note_SEQ 0
        jumpz pass
            ; otherwise, pass a 0 length rest to signal note off before the
            ; next note on
            move note 0
            move remain 0
        jump end

        label pass
            move note _sequence_note_SEQ
            move remain _sequence_notelen_SEQ
        jump end

        label finished
            move note 0
            move remain -1
        label end
    ret
endmacro SEQUENCE_MAKE_SEQ

; some syntactical sugar to cover up how this actually works
macro SEQUENCE_START SEQ
    call _sequence_start_SEQ
endmacro SEQUENCE_START

macro SEQUENCE_GET_CUR_NOTE SEQ NOTE REMAIN
    call _sequence_get_cur_note_SEQ NOTE REMAIN
endmacro SEQUENCE_GET_CUR_NOTE 

macro SEQUENCE_ADVANCE_TIME SEQ SAMPLES
    add _sequence_time_SEQ SAMPLES
endmacro SEQUENCE_ADVANCE_TIME

macro SEQUENCE_SET_TEMPO SEQ TEMPO
    move _sequence_tempo_SEQ TEMPO
endmacro SEQUENCE_SET_TEMPO

macro SEQUENCE_SET_DIVISIONS SEQ DIV
    move _sequence_notediv_SEQ DIV
endmacro SEQUENCE_SET_DIVISIONS

macro SEQUENCE_MAKE_PARAM NAME
    static _sequence_param_pos_NAME

    proc _sequence_param_start_NAME
        move _sequence_param_pos_NAME 0
    ret

    proc _sequence_param_ended_NAME ended
        move ended 0
        cmp _sequence_param_pos_NAME NAME:
        jumpl end
        move ended 1
        label end
    ret
        
    proc _sequence_param_next_NAME param
        local ended

        call _sequence_param_ended_NAME ended
        cmp ended
        jumpz get
        move param NAME:
        sub param 1
        move param NAME:param
        jump end
        label get
        move param NAME:_sequence_param_pos_NAME
        label end
    ret
endmacro SEQUENCE_MAKE_PARAM

macro SEQUENCE_PARAM_START NAME
    call _sequence_param_start_NAME
endmacro SEQUENCE_PARAM_START

macro SEQUENCE_PARAM_ENDED NAME ENDED
    call _sequence_param_ended_NAME ENDED
endmacro SEQUENCE_PARAM_ENDED

macro SEQUENCE_PARAM_NEXT NAME PARAM
    call _sequence_param_next_NAME PARAM
endmacro SEQUENCE_PARAM_NEXT

macro SEQUENCE_PARAM_NEXT
proc _sequence_clamp_val val max
    label find_val
        cmp val max
        jumpl done
        div val 2
    jump find_val
    label done
ret

; based on data from https://en.wikipedia.org/wiki/Harmonic_series_(music)
proc sequence_tune afour
    local octave floats 1

    move octave afour
    mul  octave 2

    ; C
    move _SEQUENCE_TUNINGS:0 afour
    mul  _SEQUENCE_TUNINGS:0 19
    call _sequence_clamp_val _SEQUENCE_TUNINGS:0 afour
    ; C#
    move _SEQUENCE_TUNINGS:1 afour
    mul  _SEQUENCE_TUNINGS:1 5
    call _sequence_clamp_val _SEQUENCE_TUNINGS:1 afour
    ; D
    move _SEQUENCE_TUNINGS:2 afour
    mul  _SEQUENCE_TUNINGS:2 21
    call _sequence_clamp_val _SEQUENCE_TUNINGS:2 afour
    ; D#
    move _SEQUENCE_TUNINGS:3 afour
    mul  _SEQUENCE_TUNINGS:3 11
    call _sequence_clamp_val _SEQUENCE_TUNINGS:3 afour
    ; E
    move _SEQUENCE_TUNINGS:4 afour
    mul  _SEQUENCE_TUNINGS:4 3
    call _sequence_clamp_val _SEQUENCE_TUNINGS:4 afour
    ; F
    move _SEQUENCE_TUNINGS:5 afour
    mul  _SEQUENCE_TUNINGS:5 13
    call _sequence_clamp_val _SEQUENCE_TUNINGS:5 afour
    ; F#
    move _SEQUENCE_TUNINGS:6 afour
    mul  _SEQUENCE_TUNINGS:6 27
    call _sequence_clamp_val _SEQUENCE_TUNINGS:6 afour
    ; G
    move _SEQUENCE_TUNINGS:7 afour
    mul  _SEQUENCE_TUNINGS:7 7
    call _sequence_clamp_val _SEQUENCE_TUNINGS:7 afour
    ; G#
    move _SEQUENCE_TUNINGS:8 afour
    mul  _SEQUENCE_TUNINGS:8 15
    call _sequence_clamp_val _SEQUENCE_TUNINGS:8 afour
    ; A
    move _SEQUENCE_TUNINGS:9 afour
    ; A#
    move _SEQUENCE_TUNINGS:10 afour
    mul  _SEQUENCE_TUNINGS:10 17
    call _sequence_clamp_val _SEQUENCE_TUNINGS:10 octave
    ; B
    move _SEQUENCE_TUNINGS:11 afour
    mul  _SEQUENCE_TUNINGS:11 9
    call _sequence_clamp_val _SEQUENCE_TUNINGS:11 octave
ret

proc sequence_get_note_freq freq note
    local temp
    local octave

    move temp note
    sub  temp 1
    move octave temp
    mod  temp _SEQUENCE_TUNINGS:
    div  octave _SEQUENCE_TUNINGS:
    move freq _SEQUENCE_TUNINGS:temp

    cmp octave 4
    jumpl lower
    jumpg higher
    jump done
    label lower
        move temp octave
        sub  temp 4
        mul  temp -2
        div  freq temp
    jump done
    label higher
        move temp octave
        sub  temp 4
        mul  temp 2
        mul  freq temp

    label done
ret

macro SEQUENCE_CALC_LENGTH RESULT FREQ
    move RESULT _sequence_rate
    div  RESULT FREQ
endmacro SEQUENCE_CALC_LENGTH

macro SEQUENCE_CALC_SPEED TEMP RESULT LEN FREQ
    move TEMP _sequence_rate
    div  TEMP LEN
    move RESULT FREQ
    div  RESULT TEMP
endmacro SEQUENCE_CALC_SPEED

; _SEQUENCE_PLAY_*_CHANNEL needs some local variables to work
macro SEQUENCE_PLAY_LOCALS
    local tempneeded
    local temp
    local request
    local note
    local remain
    local freq floats 1
    local speed floats 1
    local ended 0
endmacro SEQUENCE_PLAY_LOCALS

macro SEQUENCE_MAKE_TONE_CHANNEL INSTNAME SQ DIV TEMPO VOLSTARTS VOLDIFFS VOLSPEEDS DETUNES SPEEDDIFFS SPEEDSPEEDS
    static _sequence_channel_obj_SQ

    SEQUENCE_MAKE_SEQ SQ DIV TEMPO
    SEQUENCE_MAKE_PARAM VOLSTARTS
    SEQUENCE_MAKE_PARAM VOLDIFFS
    SEQUENCE_MAKE_PARAM VOLSPEEDS
    SEQUENCE_MAKE_PARAM DETUNES
    SEQUENCE_MAKE_PARAM SPEEDDIFFS
    SEQUENCE_MAKE_PARAM SPEEDSPEEDS

    macro _SEQUENCE_CHANNEL_START_SQ
        SEQUENCE_START SQ
        SEQUENCE_PARAM_START VOLSTARTS
        SEQUENCE_PARAM_START VOLDIFFS
        SEQUENCE_PARAM_START VOLSPEEDS
        SEQUENCE_PARAM_START DETUNES
        SEQUENCE_PARAM_START SPEEDDIFFS
        SEQUENCE_PARAM_START SPEEDSPEEDS
    endmacro _SEQUENCE_CHANNEL_START_SQ

    macro _SEQUENCE_CHANNEL_INIT_SQ OUTBUFFER
        SEQUENCE_GET_INST_OBJ INSTNAME _sequence_channel_obj_SQ OUTBUFFER
        _SEQUENCE_CHANNEL_START_SQ
    endmacro _SEQUENCE_CHANNEL_INIT_SQ

    macro _SEQUENCE_CHANNEL_PLAY_SQ
        _SEQUENCE_PLAY_INST_INSTNAME OBJ SQ VOLSTARTS VOLDIFFS VOLSPEEDS SPEEDSTARTS SPEEDDIFFS SPEEDSPEEDS
    endmacro _SEQUENCE_CHANNEL_PLAY_SQ

    macro _SEQUENCE_CHANNEL_FREE_SQ
        SEQUENCE_FREE_INST_OBJ INSTNAME _sequence_channel_obj_SQ
    endmacro _SEQUENCE_CHANNEL_FREE_SQ
endmacro SEQUENCE_MAKE_TONE_CHANNEL

macro SEQUENCE_CHANNEL_START NAME
    _SEQUENCE_CHANNEL_START_NAME
endmacro SEQUENCE_CHANNEL_START

macro SEQUENCE_CHANNEL_INIT NAME OUTBUFFER
    _SEQUENCE_CHANNEL_INIT_NAME OUTBUFFER
endmacro SEQUENCE_CHANNEL_INIT

macro SEQUENCE_CHANNEL_PLAY NAME
    _SEQUENCE_CHANNEL_PLAY_NAME
endmacro SEQUENCE_CHANNEL_PLAY

macro SEQUENCE_CHANNEL_FREE NAME
    _SEQUENCE_CHANNEL_FREE_NAME
endmacro SEQUENCE_CHANNEL_FREE
