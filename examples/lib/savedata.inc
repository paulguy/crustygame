proc savedata_check ok
    local i
    local sum 0

    move ok 1

    move savedata_seek 0

    move i SAVEDATA_SIZE

    label loop
        cmp i 0
        jumpz done_loop
        add sum savedata_read_char
        cmp sum 256
        jumpl no_wrap
        sub sum 256
        label no_wrap
        sub i 1
    jump loop
    label done_loop

    cmp sum 255
    jumpz good
    move ok 0
    label good
ret

proc savedata_calc_sum sum
    local i
    local temp 0

    move savedata_seek 0

    move i SAVEDATA_SIZE
    sub  i 1

    label loop
        cmp i 0
        jumpz done_loop
        add temp savedata_read_char
        cmp temp 256
        jumpl no_wrap
        sub temp 256
        label no_wrap
        sub i 1
    jump loop
    label done_loop

    move sum 255
    sub  sum temp
ret

