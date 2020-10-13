proc populate_piece_values piece rotation pos width height
    local pieceptr

    ; copy values to locals to not mutate caller variables
    move pieceptr piece
    mul  pieceptr PIECEINFO_ROTATIONS
    add  pieceptr rotation

    move pos PIECEINFO_POS:pieceptr
    move width PIECEINFO_WIDTH:pieceptr
    move height PIECEINFO_HEIGHT:pieceptr
ret
 
proc rotate x y piece rotation dir
    local biasptr

    cmp dir 0
    jumpl rotate_ccw
    jumpg rotate_cw
    jump end

    label rotate_ccw
    sub rotation 1
    jumpg no_wrap_max
    jumpz no_wrap_max
    move rotation 3
    label no_wrap_max

    ; for counter clockwise, the pointer is the rotation transitioned to
    move biasptr piece
    mul  biasptr PIECEINFO_ROTATIONS
    add  biasptr rotation

    ; for counter clockwise, the bias is subtracted (undone)
    sub x ROTATION_X_BIAS:biasptr
    sub y ROTATION_Y_BIAS:biasptr
    jump end

    label rotate_cw
    ; for clockwise, the pointer is the rotation transitioned from
    move biasptr piece
    mul  biasptr PIECEINFO_ROTATIONS
    add  biasptr rotation

    add rotation 1
    cmp rotation 4
    jumpl no_wrap_0
    move rotation 0
    label no_wrap_0

    ; for clockwise, the bias is added
    add x ROTATION_X_BIAS:biasptr
    add y ROTATION_Y_BIAS:biasptr
    
    label end
ret

expr COLLIDED_LEFT   1
expr COLLIDED_RIGHT  2
expr COLLIDED_LEFT_RIGHT "COLLIDED_LEFT | COLLIDED_RIGHT"
expr COLLIDED_TOP    4
expr COLLIDED_BOTTOM 8
expr COLLIDED_TOP_BOTTOM "COLLIDED_TOP | COLLIDED_BOTTOM"

proc check_collision collided x y pos width height
    local piecex
    local piecey
    local pitx
    local pity
    local pitpos
    local piecepos
    local pieceval
    local halfwidth
    local halfheight
    local leftx
    local rightx
    local topy
    local bottomy

    move collided 0
    ; determine ranges where a collision is determined to be to a particular
    ; side. this could be made smaller but it wouldn't necessarily be faster
    ; and would be more difficult to understand when it's already janky
    move halfwidth width
    mod  halfwidth 2
    jumpz split_width
    ; odd number.  be greedy here and count the center piece outwards for both
    ; sides.  a bottom collision could result in both being flagged but that
    ; isn't important
    ; assume that integer divisions will be floored down
    move leftx width
    div  leftx 2
    move rightx leftx
    add  leftx 1
    jump find_halfheight
    label split_width
    move leftx width
    div  leftx 2
    move rightx leftx
    label find_halfheight
    move halfheight height
    mod  halfheight 2
    jumpz split_height
    ; as above
    move topy height
    div  topy 2
    move bottomy topy
    add  topy 1
    jump done_halves
    label split_height
    move topy height
    div  topy 2
    move bottomy topy
    label done_halves

    ; check collisions with pit boundaries
    ; check top
    cmp y 0
    jumpg check_pit_bottom
    jumpz check_pit_bottom
    or collided COLLIDED_TOP

    label check_pit_bottom
    move piecey y
    add  piecey height
    cmp  piecey PIT_HEIGHT
    jumpl check_pit_left
    jumpz check_pit_left
    or collided COLLIDED_BOTTOM

    label check_pit_left
    cmp x 0
    jumpg check_pit_right
    jumpz check_pit_right
    or collided COLLIDED_LEFT

    label check_pit_right
    move piecex x
    add  piecex width
    cmp  piecex PIT_WIDTH
    jumpl check_pit
    jumpz check_pit
    or collided COLLIDED_RIGHT

    label check_pit
    ; initial pit position
    move pitpos y
    mul  pitpos PIT_WIDTH
    add  pitpos x

    move piecepos pos
    move piecey 0
    move pity y
    label y
        cmp pity 0
        jumpl skip_y
        cmp pity PIT_HEIGHT
        jumpg end
        jumpz end

        move piecex 0
        move pitx x
        label x
            cmp pitx 0
            jumpl skip_x
            cmp pitx PIT_WIDTH
            jumpg skip_x
            jumpz skip_x

            move pieceval pieces:piecepos
            and  pieceval pit:pitpos
            jumpz no_collision

            ; determine what halves a collision happened on
            ; left side
            cmp piecex leftx
            jumpg check_right
            jumpz check_right
            or collided COLLIDED_LEFT
            label check_right
            cmp piecex rightx
            jumpl check_top
            or collided COLLIDED_RIGHT
            label check_top
            cmp piecey topy
            jumpg check_bottom
            jumpz check_bottom
            or collided COLLIDED_TOP
            label check_bottom
            cmp piecey bottomy
            jumpl no_collision
            or collided COLLIDED_BOTTOM
            label no_collision

            label skip_x
            add piecepos 1
            add pitpos 1
            add pitx 1
            add piecex 1
            cmp piecex width
        jumpl x

        sub pitpos width
        label skip_y
        add pitpos PIT_WIDTH
        add pity 1
        add piecey 1
        cmp piecey height
    jumpl y

    label end
ret

proc try_resolve_piece_movement piece rotation x y xmove ymove dir
    local pos
    local oldwidth
    local oldheight
    local width
    local height
    local xbias
    local ybias
    local newrotation
    local widthdiff
    local heightdiff
    local leftgrowth
    local rightgrowth
    local topgrowth
    local bottomgrowth
    local checkx
    local checky
    local collided
    local priority

    ; get the current and desired new piece stats
    call populate_piece_values piece rotation pos oldwidth oldheight
    move newrotation rotation
    call rotate xbias ybias piece newrotation dir
    call populate_piece_values piece newrotation pos width height

    ; try to do the full move
    move checkx x
    add  checkx xmove
    add  checkx xbias
    move checky y
    add  checky ymove
    add  checky ybias
    call check_collision priority checkx checky pos width height
    cmp priority
    jumpz update_all

    ; compare them to see if we need to check for left/right or up/down kicks,
    ; there can only be one or the other or neither for the O piece or if no
    ; rotation happened anyway
    move widthdiff width
    sub  widthdiff oldwidth
    move heightdiff height
    sub  heightdiff oldheight

    cmp widthdiff 0
    jumpg horz_kick
    cmp heightdiff 0
    jumpg vert_kick
    ; no rotation occurred or the piece doesn't occupy additional space so it
    ; doesn't need to kick, still update rotation anyway.
    move rotation newrotation
    jump no_kick

    label horz_kick
        ; piece is "jammed", don't try
        and priority COLLIDED_LEFT_RIGHT
        cmp priority COLLIDED_LEFT_RIGHT
        jumpz no_kick

        move leftgrowth widthdiff
        move rightgrowth widthdiff

        ; apply drop, if any, as well as bias
        move checky y
        add  checky ybias
        add  checky ymove

        ; try searching outwards up to the difference in width until a
        ; collision is found, if so, back off one and use that
        label horz_kick_loop
            cmp leftgrowth 0
            jumpg horz_loop_continue
            jumpz horz_loop_continue
            cmp rightgrowth 0
            jumpg horz_loop_continue
            jumpz horz_loop_continue
            ; all possibilities were tried and nothing was found, don't update
            ; anything, but try to see if the piece will move without rotation
            jump horz_loop_break
            label horz_loop_continue
                and priority COLLIDED_LEFT
                jumpn try_left

            label try_right
                cmp rightgrowth 0
                jumpl continue_right

                ; try moving right incrementally until a collision
                move checkx 0
                sub  checkx rightgrowth
                add  checkx x

                call check_collision collided checkx checky pos width height
                ; subtract it now because if it collided it needs to be updated
                ; anyway, otherwise it doesn't matter
                sub rightgrowth 1
                cmp collided
                jumpz continue_right
                ; check to see if the collision happened on the right side
                and collided COLLIDED_RIGHT
                jumpz continue_right
                ; step back 1 and use that value
                sub checkx 1
                ; check if it's still a collision, indicating there's simply
                ; not enough space to fit.
                call check_collision collided checkx checky pos width height
                cmp collided
                jumpn no_kick
            jump update_all
            label continue_right
                and priority COLLIDED_LEFT
                jumpn horz_kick_loop

            label try_left
                cmp leftgrowth 0
                jumpl continue_left

                ; same as above
                move checkx widthdiff
                mul  checkx -1
                add  checkx leftgrowth
                add  checkx x
                
                call check_collision collided checkx checky pos width height
                ; same as above
                sub leftgrowth 1
                cmp collided
                jumpz continue_left
                and collided COLLIDED_LEFT
                jumpz continue_left

                add checkx 1
                ; check if it's still a collision, indicating there's simply
                ; not enough space to fit.
                call check_collision collided checkx checky pos width height
                cmp collided
                jumpn no_kick
            jump update_all
            label continue_left
                and priority COLLIDED_LEFT
                jumpn try_right
            jump horz_kick_loop

        label horz_loop_break
    jump no_kick

    label vert_kick
        ; piece is "jammed", don't try
        and priority COLLIDED_TOP_BOTTOM
        cmp priority COLLIDED_TOP_BOTTOM
        jumpz no_kick

        move topgrowth heightdiff
        move bottomgrowth heightdiff

        ; apply drop, if any, as well as bias
        move checkx x
        add  checkx xbias
        add  checkx xmove

        ; try searching outwards up to the difference in height until a
        ; collision is found, if so, back off one and use that
        label vert_kick_loop
            cmp topgrowth 0
            jumpg vert_loop_continue
            jumpz vert_loop_continue
            cmp bottomgrowth 0
            jumpg vert_loop_continue
            jumpz vert_loop_continue
            ; all possibilities were tried and nothing was found, don't update
            ; anything, but try to see if the piece will move without rotation
            jump vert_loop_break
            label vert_loop_continue
                and priority COLLIDED_TOP
                jumpn try_top

            label try_bottom
                cmp bottomgrowth 0
                jumpl continue_bottom

                ; try moving bottom incrementally until a collision
                move checky 0
                sub  checky bottomgrowth
                add  checky y

                call check_collision collided checkx checky pos width height
                ; subtract it now because if it collided it needs to be updated
                ; anyway, otherwise it doesn't matter
                sub bottomgrowth 1
                cmp collided
                jumpz continue_bottom
                ; check to see if the collision happened on the bottom side
                and collided COLLIDED_BOTTOM
                jumpz continue_bottom
                ; step back 1 and use that value
                sub checky 1
                ; check if it's still a collision, indicating there's simply
                ; not enough space to fit.
                call check_collision collided checkx checky pos width height
                cmp collided
                jumpn no_kick
            jump update_all
            label continue_bottom
                and priority COLLIDED_TOP
                jumpn vert_kick_loop

            label try_top
                cmp topgrowth 0
                jumpl continue_top

                ; same as above
                move checky heightdiff
                mul  checky -1
                add  checky topgrowth
                add  checky y
                
                call check_collision collided checkx checky pos width height
                ; same as above
                sub topgrowth 1
                cmp collided
                jumpz continue_top
                and collided COLLIDED_TOP
                jumpz continue_top

                add checky 1
                ; check if it's still a collision, indicating there's simply
                ; not enough space to fit.
                call check_collision collided checkx checky pos width height
                cmp collided
                jumpn no_kick
            jump update_all
            label continue_top
                and priority COLLIDED_TOP
                jumpn try_bottom
            jump vert_kick_loop

        label vert_loop_break
 
    label no_kick
        ; recalculate original position, width and height as they may have
        ; changed to a rotated state
        call populate_piece_values piece rotation pos width height

        ; try to move the piece in both requested directions
        move checkx x
        add  checkx xmove
        move checky y
        add  checky ymove
        call check_collision collided checkx checky pos width height
        cmp collided
        jumpz update_pos
        ; if either is 0, don't repeat an unnecessary test
        cmp xmove 0
        jumpz end
        cmp ymove 0
        jumpz end

        ; try to move the piece down only
        cmp ymove 0
        jumpz no_down_only
            move checkx x
            move checky y
            add  checky ymove
            call check_collision collided checkx checky pos width height
            cmp collided
            jumpz update_pos
        label no_down_only

        ; try to move piece only laterally
        cmp xmove 0
        jumpz no_lateral_only
            move checkx x
            add  checkx xmove
            move checky y
            call check_collision collided checkx checky pos width height
            cmp collided
            jumpz update_pos
        label no_lateral_only
    jump end

    label update_all
        move rotation newrotation
    label update_pos
        move x checkx
        move y checky
    label end
ret

proc update_pit_map piece rotation x y
    local piecex
    local pos
    local width
    local height
    local pitpos

    call populate_piece_values piece rotation pos width height

    ; initial pit position
    move pitpos y
    mul  pitpos PIT_WIDTH
    add  pitpos x

    label y
        move piecex width

        label x
            cmp pieces:pos
            jumpz no_update
                move pit:pitpos piece
            label no_update

            add pos 1
            add pitpos 1
            sub piecex 1
        jumpg x

        ; advance to the next row of the pit
        sub pitpos width
        add pitpos PIT_WIDTH
        sub height 1
    jumpg y
ret

proc update_pit_tilemaps x y w h
    local i
    local j
    local startptr
    local ptr
    local pitval

    move startptr y
    mul  startptr PIT_WIDTH
    add  startptr x

    move ptr startptr
    move j h
    label y
        move i w
        label x
            move pitval pit:ptr
            mul  pitval TILEMAP_WIDTH
            move pit_map0_tiles:ptr PIECE_TILEMAP:pitval
            move pit_map0_colormod:ptr PIECE_COLORMOD:pitval
            add  pitval 1
            move pit_map1_tiles:ptr PIECE_TILEMAP:pitval
            move pit_map1_colormod:ptr PIECE_COLORMOD:pitval
            add  pitval 1
            move pit_map2_tiles:ptr PIECE_TILEMAP:pitval
            move pit_map2_colormod:ptr PIECE_COLORMOD:pitval
            sub  pitval 2

            add ptr 1
            sub i 1
        jumpg x
        sub ptr w
        add ptr PIT_WIDTH
        sub j 1
    jumpg y

    ; set tiles and attributes then update
    call gfx_set_tilemap_map3 pit_tilemap_id0 pit_map0_tiles:startptr x y PIT_WIDTH w h
    call gfx_set_tilemap_attr_colormod3 pit_tilemap_id0 pit_map0_colormod:startptr x y PIT_WIDTH w h
    call gfx_update_tilemap2 pit_tilemap_id0 x y w h
    call gfx_set_tilemap_map3 pit_tilemap_id1 pit_map1_tiles:startptr x y PIT_WIDTH w h
    call gfx_set_tilemap_attr_colormod3 pit_tilemap_id1 pit_map1_colormod:startptr x y PIT_WIDTH w h
    call gfx_update_tilemap2 pit_tilemap_id1 x y w h
    call gfx_set_tilemap_map3 pit_tilemap_id2 pit_map2_tiles:startptr x y PIT_WIDTH w h
    call gfx_set_tilemap_attr_colormod3 pit_tilemap_id2 pit_map2_colormod:startptr x y PIT_WIDTH w h
    call gfx_update_tilemap2 pit_tilemap_id2 x y w h
ret

proc update_pit_tilemaps_from_piece piece rotation x y
    local pos
    local width
    local height

    call populate_piece_values piece rotation pos width height
    call update_pit_tilemaps x y width height
ret

proc drop_line line
    local block
    local curline
    local pitpos
    local abovepitpos

    move curline line

    ; start at the end of the line
    move pitpos line
    mul  pitpos PIT_WIDTH
    add  pitpos PIT_WIDTH
    sub  pitpos 1
    ; end of line above to copy from
    move abovepitpos pitpos
    sub  abovepitpos PIT_WIDTH

    label line
        move block PIT_WIDTH
        label block
            cmp abovepitpos 0
            jumpl set_zero
            move pit:pitpos pit:abovepitpos
            jump done_set
            label set_zero
            move pit:pitpos 0
            label done_set

            sub pitpos 1
            sub abovepitpos 1
            sub block 1
        jumpg block

        sub curline 1
    jumpg line
    jumpz line
ret

proc check_lines lines y height
    local pitpos
    local line
    local maxline
    local block
    local blocks

    move lines 0

    move maxline y
    add  maxline height

    move pitpos y
    mul  pitpos PIT_WIDTH

    move line y
    label line
        move block PIT_WIDTH
        move blocks 0
        label block
            cmp pit:pitpos
            jumpz noblock
            add blocks 1
            label noblock

            add pitpos 1
            sub block 1
        jumpg block
        cmp PIT_WIDTH blocks
        jumpg noclear
        call drop_line line
        add lines 1
        label noclear

        add line 1
        cmp line maxline
    jumpl line
ret


