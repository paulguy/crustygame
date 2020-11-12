proc _text_pad_line_with_space tilemap tilemapptr tilemapx width
    label loop
        cmp tilemapx width
        jumpz done
        move tilemap:tilemapptr SPC
        add tilemapptr 1
        add tilemapx 1
    jump loop
    label done
ret

macro _TEXT_PAD_LINE_WITH_SPACE
    call _text_pad_line_with_space tilemap tilemapptr tilemapx width
endmacro _TEXT_PAD_LINE_WITH_SPACE

proc _text_advance_tilemapptr_line done tilemap tilemapptr tilemapx tilemapy width height stride
    add tilemapy 1
    cmp tilemapy height
    jumpl good
        move done 1
        jump done
    label good
    ; pad the line with spaces
    _TEXT_PAD_LINE_WITH_SPACE
    sub tilemapptr tilemapx
    add tilemapptr stride
    move tilemapx 0
    label done
ret

macro _TEXT_ADVANCE_TILEMAPPTR_LINE DONE
    call _text_advance_tilemapptr_line DONE tilemap tilemapptr tilemapx tilemapy width height stride
    cmp DONE
    jumpn done
endmacro _TEXT_ADVANCE_TILEMAPPTR_LINE

proc _text_advance_tilemapptr_char done tilemap tilemapptr tilemapx tilemapy width height stride
    cmp tilemapx width
    jumpl no_wrap
        _TEXT_ADVANCE_TILEMAPPTR_LINE done
        jump done
    label no_wrap
    add tilemapptr 1
    add tilemapx 1
    label done
ret

macro _TEXT_ADVANCE_TILEMAPPTR_CHAR DONE
    call _text_advance_tilemapptr_char DONE tilemap tilemapptr tilemapx tilemapy width height stride
    cmp DONE
    jumpn done
endmacro _TEXT_ADVANCE_TILEMAPPTR_CHAR

proc _text_advance_stringpos done stringpos length
    add stringpos 1
    cmp stringpos length
    jumpl good
        move done 1
    label good
ret

macro _TEXT_ADVANCE_STRINGPOS DONE
    call _text_advance_stringpos DONE stringpos string:
    cmp DONE
    jumpn done
endmacro _TEXT_ADVANCE_STRINGPOS

proc text_string_to_tilemap tilemap width height stride string
    local done 0
    local tilemapx 0
    local tilemapy 0
    local tilemapptr 0
    local stringpos 0
    local wordstart
    local remain
    local wordlen
    local copyspace

    label do_word
        move copyspace 1
        ; eat any leading spaces or newlines
        label eat_whitespace
            cmp string:stringpos SPC
            jumpz eat_space
            cmp string:stringpos NL
            jumpz eat_newline
        jump done_eat_whitespace
        label eat_space
            cmp copyspace
            jumpz no_copy_space
            move tilemap:tilemapptr SPC
            label no_copy_space

            cmp tilemapx width
            jumpl no_wrap
                move copyspace 0
                _TEXT_ADVANCE_TILEMAPPTR_LINE done
                jump eat_space_continue
            label no_wrap
            add tilemapptr 1
            add tilemapx 1

            label eat_space_continue
            _TEXT_ADVANCE_STRINGPOS done
        jump eat_whitespace
        label eat_newline
            move copyspace 1
            _TEXT_ADVANCE_TILEMAPPTR_LINE done
            _TEXT_ADVANCE_STRINGPOS done
        jump eat_whitespace
        label done_eat_whitespace

        ; find the length of the word that needs to be copied
        move wordstart stringpos
        label find_wordend
            cmp string:stringpos SPC
            jumpz found_wordend
            cmp string:stringpos NL
            jumpz found_wordend
            ; this one is special because the procedure shouldn't just quit at
            ; the end of the string, but consider this the end of the word
            add stringpos 1
            cmp stringpos string:
            jumpz found_wordend
        jump find_wordend
        label found_wordend

        ; if the word doesn't fit and tilemapx is 0, just copy the word
        ; character by character advancing the character each time. if tilemapx
        ; isn't 0, jump to the next line then try the check again. if it still
        ; doesn't fit, it'll just revert to fully copying.

        move wordlen stringpos
        ; reset the string position to the start of the word after it's done
        ; with the value
        move stringpos wordstart
        sub  wordlen wordstart
        label retry_word
        move remain width
        sub  remain tilemapx
        cmp  wordlen remain
        jumpg try_new_line

        label copy_word
            move tilemap:tilemapptr string:stringpos
            _TEXT_ADVANCE_TILEMAPPTR_CHAR done
            _TEXT_ADVANCE_STRINGPOS done
            sub wordlen 1
        jumpg copy_word
        jump do_word_continue

        label try_new_line
            cmp remain width
            jumpz copy_line
            _TEXT_ADVANCE_TILEMAPPTR_LINE done
            jump retry_word

        label copy_line
            move tilemap:tilemapptr string:stringpos
            _TEXT_ADVANCE_TILEMAPPTR_CHAR done
            _TEXT_ADVANCE_STRINGPOS done
            sub remain 1
        jumpg copy_line

        label do_word_continue
    jumpz do_word
    label done
    _TEXT_PAD_LINE_WITH_SPACE
ret

static _TEXT_NUMBERS string "0123456789-"
expr _TEXT_MINUS_INDEX 10

proc text_integer_to_string length string value
    local temp
    local temp2
    local strptr

    move length 1
    ; add an extra space for negative sign
    cmp value 0
    jumpg do_find_digits
    jumpz do_find_digits
    add length 1
    label do_find_digits
    ; find number of digits
    move temp value
    label find_digits
        div temp 10
        jumpz found_digits
        add length 1
    jump find_digits
    label found_digits

    cmp length string:
    jumpg done_put_digits

    move temp value
    move strptr length
    sub strptr 1
    cmp value 0
    jumpg do_put_digits
    jumpz do_put_digits
    move string:0 _TEXT_NUMBERS:_TEXT_MINUS_INDEX
    mul temp -1
    label do_put_digits
    label put_digits
        move temp2 temp
        mod  temp2 10
        move string:strptr _TEXT_NUMBERS:temp2
        sub strptr 1
        div temp 10
        jumpz done_put_digits
    jump put_digits
    label done_put_digits
ret
