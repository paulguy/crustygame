expr _OBJECTS_MAX 100
static _objects_id     ints _OBJECTS_MAX
static _objects_x      ints _OBJECTS_MAX
static _objects_y      ints _OBJECTS_MAX
static _objects_xscale ints _OBJECTS_MAX
static _objects_yscale ints _OBJECTS_MAX
static _objects_screen_width
static _objects_screen_height

static objects_global_scale floats "0.0 0.0"
static objects_global_offset ints 2

macro OBJECTS_RESCALE_POS XPOS YPOS
    mul XPOS objects_global_scale
    mul YPOS objects_global_scale
    add XPOS objects_global_offset:0
    add YPOS objects_global_offset:1
endmacro OBJECTS_RESCALE_POS

proc objects_setup
    local i _OBJECTS_MAX

    label loop
        sub i 1
        move _objects_id:i -1
        cmp i 0
    jumpg loop
ret

proc objects_update_global_scale
    local tempf floats "0.0 0.0 0.0 0.0"
    local temp ints 2

    move tempf:0 gfx_get_width
    move tempf:1 gfx_get_height
    div  tempf:0 _objects_screen_width
    div  tempf:1 _objects_screen_height
    cmp  tempf:0 tempf:1
    jumpl scale_taller
    jumpg scale_wider
    ; scale equal, keep offset 0
    move objects_global_scale:0 tempf:0
    move objects_global_scale:1 tempf:0
    label scale_taller
    ; center vertically
    move objects_global_scale:0 tempf:0
    move objects_global_scale:1 tempf:0
    mul  tempf:0 _objects_screen_height
    move objects_global_offset:1 gfx_get_height
    sub  objects_global_offset:1 tempf:0
    div  objects_global_offset:1 2
    move objects_global_offset:0 0
    jump scale_done
    label scale_wider
    ; center horizontally
    move objects_global_scale:0 tempf:1
    move objects_global_scale:1 tempf:1
    mul  tempf:1 _objects_screen_width
    move objects_global_offset:0 gfx_get_width
    sub  objects_global_offset:0 tempf:1
    div  objects_global_offset:0 2
    move objects_global_offset:1 0
    label scale_done
ret

macro OBJECTS_SET_SCREEN_SIZE WIDTH HEIGHT
    move _objects_screen_width WIDTH
    move _objects_screen_height HEIGHT
    call objects_update_global_scale
endmacro OBJECTS_SET_SCREEN_SIZE

proc objects_get_free obj_id layer_id
    move obj_id _OBJECTS_MAX

    label loop
        sub obj_id 1
        ; leave the loop if obj_id goes negative
        jumpl notfound
        cmp _objects_id:obj_id 0
        jumpl found
        cmp obj_id 0
    jumpg loop
    jumpz loop
    label found
    move _objects_id:obj_id layer_id
    move _objects_x:obj_id 0
    move _objects_y:obj_id 0
    move _objects_xscale:obj_id 1
    move _objects_yscale:obj_id 1
    label notfound
ret

macro OBJECTS_FREE OBJ_ID
    move _objects_id:OBJ_ID -1
endmacro OBJECTS_FREE

proc _objects_update_scale obj
    local pos ints 2
    local scale floats "0.0 0.0"
    local id

    move pos:0 _objects_x:obj
    move pos:1 _objects_y:obj
    OBJECTS_RESCALE_POS pos:0 pos:1
    move id    _objects_id:obj

    move gfx_set_layer_pos:id pos
    move scale:0 _objects_xscale:obj
    mul  scale:0 objects_global_scale:0
    move scale:1 _objects_yscale:obj
    mul  scale:1 objects_global_scale:1
    move gfx_set_layer_scale:id scale
ret

proc objects_update_scales
    local i _OBJECTS_MAX

    label loop
        sub i 1
        jumpl done
        cmp _objects_id:i 0
        jumpl no_object
        call _objects_update_scale i
        label no_object
    jump loop
    label done
ret

macro OBJECTS_SET_POS OBJ X Y
    move _objects_x:OBJ X
    move _objects_y:OBJ Y
    call _objects_update_scale OBJ
endmacro OBJECTS_SET_POS

macro OBJECTS_SET_SCALE OBJ XSCALE YSCALE
    move _objects_xscale:OBJ XSCALE
    move _objects_yscale:OBJ YSCALE
    call _objects_update_scale OBJ
endmacro OBJECTS_SET_SCALE

macro OBJECTS_SET_POS_AND_SCALE OBJ XPOS YPOS XSCALE YSCALE
    move _objects_x:OBJ XPOS
    move _objects_y:OBJ YPOS
    move _objects_xscale:OBJ XSCALE
    move _objects_yscale:OBJ YSCALE
    call _objects_update_scale OBJ
endmacro OBJECTS_SET_POS_AND_SCALE
