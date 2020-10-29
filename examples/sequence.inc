static _SEQUENCE_NOTES string "CCDDEFFGGAAB"
static _SEQUENCE_SHARP string "#"
static _SEQUENCE_FLAT  string "b"
static _SEQUENCE_REST  string "X"
static _SEQUENCE_OCTAVES string "0123456789"
static _SEQUENCE_DIVS  string "whqestfo"
; Whole, Half, Quarter, Eighth, Sixteenth,
; Thirdtysecond, sixtyFourth, Onetwentyeighth
static _SEQUENCE_TUNINGS floats 12
static _sequence_rate

macro SEQUENCE_SET_RATE RATE
    move _sequence_rate RATE
endmacro SEQUENCE_SET_RATE

macro _SEQUENCE_CALC_LENGTH RESULT DIV MUL TEMPO
    move RESULT _sequence_rate
    mul  RESULT 60
    div  RESULT TEMPO
    ; this is a length and not rate so the divisions increase the length of a
    ; nove and the multiplying of the notes shortens them
    mul  RESULT DIV
    div  RESULT MUL
endmacro _SEQUENCE_CALC_LENGTH

proc _sequence_get_next_note note len div mul tempo octave seq pos
    local i

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
            _SEQUENCE_CALC_LENGTH len div mul tempo
            move note 12
            mul  note octave
            add  note i
            add  note 1 ; 0 is rest
            cmp pos seq:
        jumpz end
            cmp seq:pos _SEQUENCE_SHARP
            jumpz note_sharp
            cmp seq:pos _SEQUENCE_FLAT
            jumpz note_flat
        jump end
        label note_sharp
            add note 1
        jump end
        label note_flat
            sub note 1
        jump end
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
            move mul i
            add  mul 1
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
            _SEQUENCE_CALC_LENGTH len div mul tempo
            div  len div
 
            jump end
        label not_rest

        ; ignore invalid characters
        add pos 1
    label end
ret

macro MAKE_SEQUENCE SEQ DIV TEMPO
    static _sequence_start_notediv_SEQ DIV
    static _sequence_start_tempo_SEQ TEMPO
    static _sequence_notediv_SEQ
    static _sequence_tempo_SEQ
    static _sequence_notemul_SEQ
    static _sequence_pos_SEQ
    static _sequence_note_SEQ
    static _sequence_octave_SEQ
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
        call _sequence_get_next_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_notemul_SEQ _sequence_tempo_SEQ _sequence_octave_SEQ SEQ _sequence_pos_SEQ
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
        jumpl next_note
        jumpz next_note
        ; just return the note
            move note _sequence_note_SEQ
        jump end

        label next_note
            ; advance the time
            add _sequence_notetime_SEQ _sequence_notelen_SEQ
            move remain _sequence_notelen_SEQ

            ; get the next note information
            call _sequence_get_next_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_notemul_SEQ _sequence_tempo_SEQ _sequence_octave_SEQ SEQ _sequence_pos_SEQ

            ; see if the sequence has ended
            cmp _sequence_note_SEQ 0
        jumpl finished
            move note 0
            move remain 0
        jump end

        label finished
            move note 0
            move remain -1
        label end
    ret
endmacro MAKE_SEQUENCE

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
    call _sequence_clamp_val _SEQUENCE_TUNINGS:0 octave
    ; C#
    move _SEQUENCE_TUNINGS:1 afour
    mul  _SEQUENCE_TUNINGS:1 5
    call _sequence_clamp_val _SEQUENCE_TUNINGS:1 octave
    ; D
    move _SEQUENCE_TUNINGS:2 afour
    mul  _SEQUENCE_TUNINGS:2 21
    call _sequence_clamp_val _SEQUENCE_TUNINGS:2 octave
    ; D#
    move _SEQUENCE_TUNINGS:3 afour
    mul  _SEQUENCE_TUNINGS:3 11
    call _sequence_clamp_val _SEQUENCE_TUNINGS:3 octave
    ; E
    move _SEQUENCE_TUNINGS:4 afour
    mul  _SEQUENCE_TUNINGS:4 3
    call _sequence_clamp_val _SEQUENCE_TUNINGS:4 octave
    ; F
    move _SEQUENCE_TUNINGS:5 afour
    mul  _SEQUENCE_TUNINGS:5 13
    call _sequence_clamp_val _SEQUENCE_TUNINGS:5 octave
    ; F#
    move _SEQUENCE_TUNINGS:6 afour
    mul  _SEQUENCE_TUNINGS:6 27
    call _sequence_clamp_val _SEQUENCE_TUNINGS:6 octave
    ; G
    move _SEQUENCE_TUNINGS:7 afour
    mul  _SEQUENCE_TUNINGS:7 7
    call _sequence_clamp_val _SEQUENCE_TUNINGS:7 octave
    ; G#
    move _SEQUENCE_TUNINGS:8 afour
    mul  _SEQUENCE_TUNINGS:8 15
    call _sequence_clamp_val _SEQUENCE_TUNINGS:8 octave
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
        move temp 4
        sub  temp octave
        mul  temp 2
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

static _sequence_sample_mem floats SEQUENCE_SAMPLE_MEM_SIZE
static _sequence_sample_mem_pos 0

proc sequence_clear_sample_mem
    move _sequence_sample_mem_pos 0
ret

proc sequence_get_sample_mem mempos size
    local temp

    move temp _sequence_sample_mem_pos
    add  temp size
    cmp  temp _sequence_sample_mem:
    jumpl good
    move mempos -1
    jump end
    label good

    move mempos _sequence_sample_mem_pos
    add  _sequence_sample_mem_pos size
    label end
ret

macro SEQUENCE_SET_SAMPLE_MEM_BUFFER MEMPOS
    move set_buffer _sequence_sample_mem:MEMPOS
endmacro SEQUENCE_SET_SAMPLE_MEM_BUFFER

macro SEQUENCE_CALL_WITH_SAMPLE_MEM_BUFFER PROC PRIV MEMPOS SIZE
    call PROC PRIV _sequence_sample_mem:MEMPOS SIZE
endmacro SEQUENCE_CALL_WITH_SAMPLE_MEM_BUFFER
