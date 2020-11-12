proc wave_make_dc buffer length val
    local temp 0

    label genwave
        cmp temp length
        jumpz donewave
        move buffer:temp val
        add temp 1
    jump genwave
    label donewave
ret

proc wave_make_square buffer length low high
    local temp 0
    local half

    move half length
    div  half 2

    label genwave1
        cmp temp half
        jumpz genwave2
        move buffer:temp high
        add temp 1
    jump genwave1
    label genwave2
        cmp temp length
        jumpz donewave
        move buffer:temp low
        add temp 1
    jump genwave2
    label donewave
ret

proc wave_make_triangle buffer length low high
    local temp 0
    local quarter
    local threequarter
    local slope floats 1
    local val   floats 1

    move quarter length
    div  quarter 4
    move threequarter quarter
    mul  threequarter 3

    move val low
    add  val high
    div  val 2

    move slope high
    sub  slope low
    div  slope length
    mul  slope 2

    label genwave1
        cmp temp quarter
        jumpz genwave2
        move buffer:temp val
        add val slope
        add temp 1
    jump genwave1
    label genwave2
        cmp temp threequarter
        jumpz genwave3
        move buffer:temp val
        sub val slope
        add temp 1
    jump genwave2
    label genwave3
        cmp temp length
        jumpz donewave
        move buffer:temp val
        add val slope
        add temp 1
    jump genwave3
    label donewave
ret

proc wave_make_saw_descending buffer length low high
    local temp 0
    local slope floats 1
    local val   floats 1

    move val high

    move slope high
    sub  slope low
    div  slope length

    label genwave
        cmp temp length
        jumpz donewave
        move buffer:temp val
        sub val slope
        add temp 1
    jump genwave
    label donewave
ret

proc wave_make_saw_ascending buffer length low high
    local temp 0
    local slope floats 1
    local val   floats 1

    move val low

    move slope high
    sub  slope low
    div  slope length

    label genwave
        cmp temp length
        jumpz donewave
        move buffer:temp val
        add val slope
        add temp 1
    jump genwave
    label donewave
ret

; i know this isn't really cosine but just some lame piecewise power function
; dealie but it produces a smoothish waveform and if you want something better
; you can just make something better.
proc wave_make_cosine_approx buffer length low high
    local temp
    local quarter
    local half
    local threequarter
    local slope floats 1
    local val floats 1
    local result floats 1
    local bias floats 1
    local scale floats 1

    move quarter length
    div  quarter 4
    move half length
    div  half 2
    move threequarter quarter
    mul  threequarter 3

    move scale high
    sub  scale low
    div  scale 2

    move bias high
    add  bias low
    div  bias 2

    move slope 1
    div  slope length
    mul  slope 4

    move val 1
    label genwave1
        cmp temp quarter
        jumpz genwave2
        move buffer:temp val
        mul  buffer:temp buffer:temp
        mul  buffer:temp -1
        add  buffer:temp 1
        mul  buffer:temp scale
        add  buffer:temp bias
        sub val slope
        add temp 1
    jump genwave1
    label genwave2
        cmp temp half
        jumpz genwave3
        move buffer:temp val
        mul  buffer:temp buffer:temp
        mul  buffer:temp -1
        add  buffer:temp 1
        mul  buffer:temp scale
        add  buffer:temp bias
        add val slope
        add temp 1
    jump genwave2
    label genwave3
        cmp temp threequarter
        jumpz genwave4
        move buffer:temp val
        mul  buffer:temp buffer:temp
        sub  buffer:temp 1
        mul  buffer:temp scale
        add  buffer:temp bias
        sub val slope
        add temp 1
    jump genwave3
    label genwave4
        cmp temp length
        jumpz done
        move buffer:temp val
        mul  buffer:temp buffer:temp
        sub  buffer:temp 1
        mul  buffer:temp scale
        add  buffer:temp bias
        add val slope
        add temp 1
    jump genwave4
    label done
ret
