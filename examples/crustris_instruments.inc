expr _INST_SINE_PLAYER_IDX 0
expr _INST_SINE_NOTE_IDX 1
expr _INST_SINE_OUTPOS_IDX 2
expr _INST_SINE_OBJSIZE 3

static _inst_sine_buf
static _inst_sine_len

proc _inst_sine_make_sample priv mem size
ret

proc inst_sine_init
    local buf
    local temp ints 2

    SEQUENCE_CALC_LENGTH _inst_sine_len 440
    call wave_make_cosine_approx sequence_user_scratch_mem _inst_sine_len -1 1

    move set_buffer sequence_user_scratch_mem
    move temp:0 AUDIO_TYPE_F64
    move temp:1 _inst_sine_len
    move audio_add_buffer temp
    move _inst_sine_buf get_return
ret

proc inst_sine_free
    move audio_free_buffer _inst_sine_buf
ret

proc inst_sine_obj_init hInst
    local vol floats "0.2"
    local temp
    local player

    ; start inactive
    move audio_add_player _inst_sine_buf
    move player get_return
    move audio_set_player_mode:player AUDIO_MODE_LOOP
    move audio_set_player_volume:player vol

    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_NOTE_IDX 0
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_PLAYER_IDX player
ret

proc inst_sine_obj_free hInst
    local temp

    SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_PLAYER_IDX
    move audio_free_player temp
ret

; note 0 is rest/off
proc inst_sine_note hInst note
    local temp
    SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_NOTE_IDX
    cmp temp note
    jumpz continue_note
        SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_PLAYER_IDX
        move audio_set_player_input_buffer_pos:temp 0
        SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_NOTE_IDX note
    label continue_note
ret

proc inst_sine_frame hInst needed
    local temp
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_OUTPOS_IDX 0
    SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_PLAYER_IDX
    move audio_set_player_output_buffer_pos:temp 0
ret

proc inst_sine_run hInst request
    local player
    local freq floats 1
    local speed floats 1
    local note
    local temp
    local outpos

    SEQUENCE_GET_INST_OBJ_MEM temp outpos sine hInst _INST_SINE_OUTPOS_IDX
    ; check if note is on
    SEQUENCE_GET_INST_OBJ_MEM temp note sine hInst _INST_SINE_NOTE_IDX
    cmp note 1
    jumpl end

    SEQUENCE_GET_INST_OBJ_MEM temp player sine hInst _INST_SINE_PLAYER_IDX

    call sequence_get_note_freq freq note
    SEQUENCE_CALC_SPEED temp speed _inst_sine_len freq
    move audio_set_player_speed:player speed
    move audio_set_player_output_buffer_pos:player outpos
    move audio_run_player:player request

    label end
    add outpos request
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_OUTPOS_IDX outpos
ret

SEQUENCE_MAKE_INST sine _INST_SINE_OBJSIZE
