static _SEQUENCE_NOTES string "CCDDEFFGGAAB"
static _SEQUENCE_SHARP string "#"
static _SEQUENCE_FLAT  string "b"
static _SEQUENCE_REST  string "X"
static _SEQUENCE_OCTAVES string "0123456789"
static _SEQUENCE_DIVS  string "whqestfo"
; Whole, Half, Quarter, Eighth, Sixteenth,
; Thirdtysecond, sixtyFourth, Onetwentyeighth
static _SEQUENCE_TUNINGS floats 12

proc _sequence_get_note note len div octave nextpos seq pos basetime
    local i
    local temppos

    move temppos pos

    label try_find_note
        cmp temppos seq:
        jumpl not_finished
            ; indicate the sequence finished
            move note -1
            jump end
        label not_finished

        move i 0
        label find_note
            cmp seq:temppos _SEQUENCE_NOTES:i
            jumpz found_note
            add i 1
            cmp i _SEQUENCE_NOTES:
        jumpl find_note
        jump not_note
        label found_note
            move len basetime
            div  len div
            move note _SEQUENCE_NOTES:i
            mul  note octave
            add  note 1 ; 0 is rest
            cmp temppos seq:
        jumpz end
            add temppos 1
            cmp seq:temppos _SEQUENCE_SHARP
            jumpz note_sharp
            cmp seq:temppos _SEQUENCE_FLAT
            jumpz note_flat
            move nextpos temppos
        jump end
        label note_sharp
            add temppos 1
            add note 1
            move nextpos temppos
        jump end
        label note_flat
            add temppos 1
            sub note 1
            move nextpos temppos
        jump end
        label not_note

        move i 0
        label find_div
            cmp seq:temppos _SEQUENCE_DIVS:i
            jumpz found_div
            add i 1
            cmp i _SEQUENCE_DIVS:
        jumpl find_div
        jump not_div
        label found_div
            add temppos 1
            move div i
        jump try_find_note
        label not_div
            
        move i 0
        label find_octave
            cmp seq:temppos _SEQUENCE_OCTAVES:i
            jumpz found_octave
            add i 1
            cmp i _SEQUENCE_OCTAVES:
        jumpl find_octave
        jump not_octave
        label found_octave
            add temppos 1
            move octave i
        jump try_find_note
        label not_octave
 
        cmp seq:temppos _SEQUENCE_REST
        jumpn not_rest
            move note 0
            move len basetime
            div  len div
            move nextpos temppos
 
            jump end
        label not_rest

        ; ignore invalid characters
        add temppos 1
    label end
ret

macro MAKE_SEQUENCE SEQ BPM DIVS
    expr _SQUENCE_BASE_NOTE_TIME "(1000 * DIVS / BPM)"
    static _sequence_pos_SEQ
    static _sequence_next_pos_SEQ
    static _sequence_note_SEQ
    static _sequence_octave_SEQ
    static _sequence_notediv_SEQ
    static _sequence_notelen_SEQ
    static _sequence_notetime_SEQ
    static _sequence_notes_missed_SEQ
    static _sequence_consumed_SEQ

    proc sequence_start_SEQ
        move _sequence_consumed_SEQ 0
        move _sequence_pos_SEQ 0
        move _sequence_notediv_SEQ 1
        move _sequence_octave_SEQ 4
        move _sequence_notetime_SEQ get_ticks
        call _sequence_get_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_octave_SEQ _sequence_next_pos_SEQ SEQ _sequence_pos_SEQ _SQUENCE_BASE_NOTE_TIME
        move _sequence_notes_missed_SEQ 0
    ret

    ; note = -1 && remain = 0 -> note off
    ; remain = -1 -> sequence ended
    proc sequence_get_cur_note_SEQ note remain
        local thisTime
        local consumed 0

        cmp _sequence_note_SEQ -1
        jumpg no_need_reset
            move remain -1
            jump end
        label no_need_reset

        move thisTime get_ticks
        move remain thisTime
        sub  remain _sequence_notetime_SEQ

        label do_consume
        label consume
            cmp _sequence_notelen_SEQ remain
            jumpl done_consume
            move consumed 1
            call _sequence_get_note _sequence_note_SEQ _sequence_notelen_SEQ _sequence_notediv_SEQ _sequence_octave_SEQ _sequence_next_pos_SEQ SEQ _sequence_pos_SEQ _SQUENCE_BASE_NOTE_TIME
            cmp _sequence_note_SEQ 0
            jumpl error
            sub remain _sequence_notelen_SEQ
        jump consume
        label done_consume
            cmp consumed
            jumpz do_note
                ;indicate note off, next call will give the next note
                move note -1
                move remain 0
            jump end
            label do_note
                move note _sequence_note_SEQ
        jump end

        label error
            move note -1
            move remain -1
        label end
    ret
endmacro MAKE_SEQUENCE

; some syntactical sugar to cover up how this actually works
macro SEQUENCE_START SEQ
    call sequence_start_SEQ
endmacro SEQUENCE_START

macro SEQUENCE_GET_CUR_NOTE SEQ NOTE REMAIN
    call sequence_get_cur_note_SEQ NOTE REMAIN
endmacro SEQUENCE_GET_CUR_NOTE 

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

    ; A
    move _SEQUENCE_TUNINGS:0 afour
    ; A#
    move _SEQUENCE_TUNINGS:1 afour
    mul  _SEQUENCE_TUNINGS:1 17
    call _sequence_clamp_val _SEQUENCE_TUNINGS:1 octave
    ; B
    move _SEQUENCE_TUNINGS:2 afour
    mul  _SEQUENCE_TUNINGS:2 9
    call _sequence_clamp_val _SEQUENCE_TUNINGS:2 octave
    ; C
    move _SEQUENCE_TUNINGS:3 afour
    mul  _SEQUENCE_TUNINGS:3 19
    call _sequence_clamp_val _SEQUENCE_TUNINGS:3 octave
    ; C#
    move _SEQUENCE_TUNINGS:4 afour
    mul  _SEQUENCE_TUNINGS:4 5
    call _sequence_clamp_val _SEQUENCE_TUNINGS:4 octave
    ; D
    move _SEQUENCE_TUNINGS:5 afour
    mul  _SEQUENCE_TUNINGS:5 21
    call _sequence_clamp_val _SEQUENCE_TUNINGS:5 octave
    ; D#
    move _SEQUENCE_TUNINGS:6 afour
    mul  _SEQUENCE_TUNINGS:6 11
    call _sequence_clamp_val _SEQUENCE_TUNINGS:6 octave
    ; E
    move _SEQUENCE_TUNINGS:7 afour
    mul  _SEQUENCE_TUNINGS:7 3
    call _sequence_clamp_val _SEQUENCE_TUNINGS:7 octave
    ; F
    move _SEQUENCE_TUNINGS:8 afour
    mul  _SEQUENCE_TUNINGS:8 13
    call _sequence_clamp_val _SEQUENCE_TUNINGS:8 octave
    ; F#
    move _SEQUENCE_TUNINGS:9 afour
    mul  _SEQUENCE_TUNINGS:9 27
    call _sequence_clamp_val _SEQUENCE_TUNINGS:9 octave
    ; G
    move _SEQUENCE_TUNINGS:10 afour
    mul  _SEQUENCE_TUNINGS:10 7
    call _sequence_clamp_val _SEQUENCE_TUNINGS:10 octave
    ; G#
    move _SEQUENCE_TUNINGS:11 afour
    mul  _SEQUENCE_TUNINGS:11 15
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
    jumpz done
    label lower
        move temp 4
        sub  temp octave
        div  freq temp
    jump done
    label higher
        move temp octave
        sub  temp 4
        mul  freq temp

    label done
ret

macro SEQUENCE_GET_FREQ_PERIOD RESULT RATE FREQ
    move RESULT RATE
    div  RESULT FREQ
endmacro SEQUENCE_GET_FREQ_PERIOD

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

macro SEQUENCE_CALL_WITH_SAMPLE_MEM_BUFFER MEMPOS SIZE PROC
    call PROC _sequence_sample_mem:MEMPOS SIZE
endmacro SEQUENCE_CALL_WITH_SAMPLE_MEM_BUFFER
