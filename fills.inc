proc fill buffer w h stride val
    local ptr 0
    local x 0
    local y

    move y h
    label y
        move x w
        label x
            move buffer:ptr val
            add ptr 1
            sub x 1
        jumpg x
        sub ptr w
        add ptr stride
        sub y 1
    jumpg y
ret

proc fill_with_border buffer width height stride topleft top topright left center right bottomleft bottom bottomright
    local temp
    local innerWidth
    local innerHeight
    ; make a border and filled background
    move innerWidth width
    sub  innerWidth 2
    move innerHeight height
    sub  innerHeight 2

    ; top row
    move buffer:0 topleft
    call fill buffer:1 innerWidth 1 innerWidth top
    move temp width
    sub  temp 1
    move buffer:temp topright
    ; left edge
    call fill buffer:stride 1 innerHeight stride left
    ; fill
    move temp stride
    add  temp 1
    call fill buffer:temp innerWidth innerHeight stride center
    ; right edge
    move temp stride
    mul  temp 2
    sub  temp 1
    call fill buffer:temp 1 innerHeight stride right
    ; bottom row
    move temp stride
    mul  temp innerHeight
    add  temp stride
    move buffer:temp bottomleft
    add  temp 1
    call fill buffer:temp innerWidth 1 innerWidth bottom
    add  temp innerWidth
    move buffer:temp bottomright
ret
