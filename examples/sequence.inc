static _SEQUENCE_NOTES string "AABCCDDEFFGG"
static _SEQUENCE_SHARP string "#"
static _SEQUENCE_FLAT  string "b"
static _SEQUENCE_REST  string "X"
static _SEQUENCE_OCTAVES string "0123456789"
static _SEQUENCE_DIVS  string "whqestfo"
; Whole, Half, Quarter, Eighth, Sixteenth,
; Thirdtysecond, sixtyFourth, Onetwentyeighth

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
        move _sequence_octave_SEQ 3
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
