static _fullscreenstr string "fullscreen"
static _videomode     string "#####x#####"
static _videomodestrptr
static _fullscreen 0
static _videomode_error string "Video mode string too large: "

proc set_video_mode videomode
    local i 0
    local ptr

    ; find pointer and determine if it's too big to store
    move ptr _videomode:
    sub  ptr videomode:
    jumpl too_large

    ; copy in to private string
    move _videomodestrptr ptr
    label loop
        move _videomode:ptr videomode:i
        add  ptr 1
        add  i   1
        cmp  i   videomode:
    jumpl loop
    
    ; if not fullscreen, set the mode right away
    cmp _fullscreen 0
    jumpn end
    move gfx_set_video_mode _videomode:_videomodestrptr

    jump end
    label too_large
    move string_out _videomode_error
    move string_out videomode
    move out NL

    label end
ret

proc toggle_fullscreen
    cmp _fullscreen 0
    jumpz fullscreen

    move gfx_set_video_mode _videomode:_videomodestrptr
    move _fullscreen 0
    jump videoend

    label fullscreen
    move gfx_set_video_mode _fullscreenstr
    move _fullscreen 1

    label videoend
ret 
