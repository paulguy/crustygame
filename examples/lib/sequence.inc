; pretty big but it's 1 second at a crazy high sample rate so it should be
; relatively reasonable.  Better than a bunch of things having their own
; buffers
expr SEQUENCE_USER_SCRATCH_MEM_SIZE 192000

static _SEQUENCE_NOTES string "CCDDEFFGGAAB"
static _SEQUENCE_SHARP string "#"
static _SEQUENCE_FLAT  string "b"
static _SEQUENCE_REST  string "X"
static _SEQUENCE_OCTAVES string "0123456789"
static _SEQUENCE_DIVS  string "whqestio"
static _SEQUENCE_DIV_VALS ints "1 2 4 8 16 32 64 128"
; Whole, Half, Quarter, Eighth, Sixteenth,
; Thirdtysecond, sIxtyfourth, Onetwentyeighth
static _SEQUENCE_PIANO string "p"
static _SEQUENCE_MEZZO string "m"
static _SEQUENCE_FORTE string "f"
static _SEQUENCE_SLUR string "_"
static _SEQUENCE_DOT string "."
static _SEQUENCE_DOT_MUL floats "1.5"
static _SEQUENCE_TUNINGS floats 12
static _sequence_rate
static sequence_user_scratch_mem floats SEQUENCE_USER_SCRATCH_MEM_SIZE
static sequence_scratch_volume_buffer -1
static sequence_scratch_speed_buffer -1

proc sequence_set_rate rate
    move _sequence_rate rate

    cmp sequence_scratch_volume_buffer 0
    jumpl no_free
    move audio_free_buffer sequence_scratch_volume_buffer
    move audio_free_buffer sequence_scratch_speed_buffer
    label no_free

    ; make the buffer 1 second, should be plenty
    move audio_add_empty_buffer rate
    move sequence_scratch_volume_buffer get_return
    move audio_add_empty_buffer rate
    move sequence_scratch_speed_buffer get_return
ret

proc _sequence_get_next_note note len div mul tempo octave velocity slurred seq pos
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

        cmp seq:pos _SEQUENCE_PIANO
        jumpn not_piano
            add pos 1
            sub velocity 1
        jump try_find_note
        label not_piano

        cmp seq:pos _SEQUENCE_MEZZO
        jumpn not_mezzo
            add pos 1
            move velocity 0
        jump try_find_note
        label not_mezzo

        cmp seq:pos _SEQUENCE_FORTE
        jumpn not_forte
            add pos 1
            add velocity 1
        jump try_find_note
        label not_forte

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
    static _sequence_velocity_SEQ
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
        move _sequence_velocity_SEQ 0
        move _sequence_time_SEQ 0
        move _sequence_notetime_SEQ 0
        call _sequence_get_next_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_notemul_SEQ _sequence_tempo_SEQ _sequence_octave_SEQ _sequence_velocity_SEQ _sequence_slurred_SEQ SEQ _sequence_pos_SEQ
        move _sequence_notes_missed_SEQ 0
    ret

    ; note = 0 -> note off/rest
    ; remain = -1 -> sequence ended
    proc _sequence_get_cur_note_SEQ note velocity remain
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
            move velocity _sequence_velocity_SEQ
        jump end

        label next_note
            ; advance the time
            add _sequence_notetime_SEQ _sequence_notelen_SEQ

            ; get the next note information
            call _sequence_get_next_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_notemul_SEQ _sequence_tempo_SEQ _sequence_octave_SEQ _sequence_velocity_SEQ _sequence_slurred_SEQ SEQ _sequence_pos_SEQ

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
            move velocity _sequence_velocity_SEQ
            move remain 0
        jump end

        label pass
            move note _sequence_note_SEQ
            move velocity _sequence_velocity_SEQ
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

macro SEQUENCE_GET_CUR_NOTE SEQ NOTE VELOCITY REMAIN
    call _sequence_get_cur_note_SEQ NOTE VELOCITY REMAIN
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

expr _SEQUENCE_MAX_INST_OBJS 10

macro SEQUENCE_MAKE_INST INSTNAME OBJSIZE
    expr _SEQUENCE_ISIZE_INSTNAME "OBJSIZE + 1"
    expr _SEQUENCE_IMEM_INSTNAME "_SEQUENCE_MAX_INST_OBJS * _SEQUENCE_ISIZE_INSTNAME"

    static _sequence_inst_objbuffer_INSTNAME ints _SEQUENCE_IMEM_INSTNAME

    proc sequence_inst_init_INSTNAME hInst
        ; find free slot
        move hInst _SEQUENCE_IMEM_INSTNAME
        label loop
            sub hInst _SEQUENCE_ISIZE_INSTNAME
            jumpl end
            cmp _sequence_inst_objbuffer_INSTNAME:hInst 0
            jumpz found
        jump loop
        label found
        ; mark used
        move _sequence_inst_objbuffer_INSTNAME:hInst 1
        add hInst 1
        call inst_INSTNAME_obj_init hInst
        label end
    ret

    proc sequence_inst_free_INSTNAME hInst
        call inst_INSTNAME_obj_free hInst
        sub hInst 1
        move _sequence_inst_objbuffer_INSTNAME:hInst 0
    ret

    macro _SEQUENCE_PLAY_INSTNAME_CHANNEL SQ OBJ
        move tempneeded needed
        call inst_INSTNAME_frame OBJ tempneeded
        label need_more_SQ
            SEQUENCE_GET_CUR_NOTE SQ note velocity remain
            cmp remain 0 ; end of sequence
            jumpl ended_SQ
            call inst_INSTNAME_note OBJ note velocity remain

            move request remain
            cmp request tempneeded
            jumpl no_set_needed_SQ
            move request tempneeded
            label no_set_needed_SQ

            call inst_INSTNAME_run OBJ request
            SEQUENCE_ADVANCE_TIME SQ request

            sub tempneeded request
        jumpg need_more_SQ
        jump end_SQ

        label ended_SQ
            add ended 1
        label end_SQ
    endmacro _SEQUENCE_PLAY_INSTNAME_CHANNEL
endmacro SEQUENCE_MAKE_INST

macro SEQUENCE_GET_INST_OBJ INST HANDLE
    call sequence_inst_init_INST HANDLE
endmacro SEQUENCE_GET_INST_OBJ

macro SEQUENCE_FREE_INST_OBJ INST HANDLE
    call sequence_inst_free_INST HANDLE
endmacro SEQUENCE_FREE_INST_OBJ

macro SEQUENCE_GET_INST_OBJ_MEM TEMP RESULT NAME HANDLE OFFSET
    move TEMP HANDLE
    add  TEMP OFFSET
    move RESULT _sequence_inst_objbuffer_NAME:TEMP
endmacro SEQUENCE_GET_INST_OBJ_MEM

macro SEQUENCE_SET_INST_OBJ_MEM TEMP NAME HANDLE OFFSET VAL
    move TEMP HANDLE
    add  TEMP OFFSET
    move _sequence_inst_objbuffer_NAME:TEMP VAL
endmacro SEQUENCE_SET_INST_OBJ_MEM

; _SEQUENCE_PLAY_*_CHANNEL needs some local variables to work
macro SEQUENCE_PLAY_LOCALS
    local tempneeded
    local temp
    local request
    local note
    local velocity
    local remain
    local freq floats 1
    local speed floats 1
    local ended 0
endmacro SEQUENCE_PLAY_LOCALS

macro SEQUENCE_PLAY_CHANNEL INST SQ OBJ
    _SEQUENCE_PLAY_INST_CHANNEL SQ OBJ
endmacro SEQUENCE_PLAY_CHANNEL
