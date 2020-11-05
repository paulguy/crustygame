expr _INST_SINE_PLAYER_IDX 0
expr _INST_SINE_MODE_IDX 1
expr _INST_SINE_OUTPOS_IDX 2
expr _INST_SINE_ENVLEFT_IDX 3
expr _INST_SINE_OBJSIZE 4

expr _INST_SINE_MODE_OFF      0
expr _INST_SINE_MODE_ATTACK   1
expr _INST_SINE_MODE_RESONATE 2

static _inst_sine_buf
static _inst_sine_len
static _inst_sine_env_buf
static _inst_sine_env_buf2
static _inst_sine_env_len

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

    SEQUENCE_CALC_LENGTH _inst_sine_env_len 10
    call wave_make_saw_ascending sequence_user_scratch_mem _inst_sine_env_len 0 1
    move set_buffer sequence_user_scratch_mem
    move temp:0 AUDIO_TYPE_F64
    move temp:1 _inst_sine_env_len
    move audio_add_buffer temp
    move _inst_sine_env_buf get_return

    call wave_make_saw_descending sequence_user_scratch_mem _inst_sine_env_len 0 1
    move set_buffer sequence_user_scratch_mem
    move temp:0 AUDIO_TYPE_F64
    move temp:1 _inst_sine_env_len
    move audio_add_buffer temp
    move _inst_sine_env_buf2 get_return
ret

proc inst_sine_free
    move audio_free_buffer _inst_sine_env_buf
    move audio_free_buffer _inst_sine_buf
ret

proc inst_sine_obj_init hInst
    local vol floats "0.2"
    local temp
    local player

    move audio_add_player _inst_sine_buf
    move player get_return
    move audio_set_player_mode:player AUDIO_MODE_LOOP
    move audio_set_player_volume:player vol

    ; start inactive
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_MODE_IDX _INST_SINE_MODE_OFF
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
    local freq floats 1
    local speed floats 1
    local player

    SEQUENCE_GET_INST_OBJ_MEM temp player sine hInst _INST_SINE_PLAYER_IDX
    ; check for note off
    cmp note 0
    jumpz note_off

    ; don't retrigger if a note is already playing without being triggered off
    SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_MODE_IDX
    cmp temp _INST_SINE_MODE_ATTACK
    jumpz end
        SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_PLAYER_IDX
        move audio_set_player_input_buffer_pos:temp 0
        call sequence_get_note_freq freq note
        SEQUENCE_CALC_SPEED temp speed _inst_sine_len freq
        move audio_set_player_speed:player speed
        move audio_set_player_volume_source:player _inst_sine_env_buf
        move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_SOURCE

        SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_MODE_IDX _INST_SINE_MODE_ATTACK
        SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_ENVLEFT_IDX _inst_sine_env_len
    jump end

    label note_off
        move audio_set_player_volume_source:player _inst_sine_env_buf2
        move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_SOURCE

        SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_MODE_IDX _INST_SINE_MODE_RESONATE
        SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_ENVLEFT_IDX _inst_sine_env_len
    label end
ret

proc inst_sine_frame hInst needed
    local temp
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_OUTPOS_IDX 0
    SEQUENCE_GET_INST_OBJ_MEM temp temp sine hInst _INST_SINE_PLAYER_IDX
    move audio_set_player_output_buffer_pos:temp 0
ret

proc inst_sine_run hInst request
    local player
    local mode
    local temp
    local outpos
    local envleft
    local todo

    move todo request

    SEQUENCE_GET_INST_OBJ_MEM temp player sine hInst _INST_SINE_PLAYER_IDX
    SEQUENCE_GET_INST_OBJ_MEM temp outpos sine hInst _INST_SINE_OUTPOS_IDX
    SEQUENCE_GET_INST_OBJ_MEM temp envleft sine hInst _INST_SINE_ENVLEFT_IDX

    SEQUENCE_GET_INST_OBJ_MEM temp mode sine hInst _INST_SINE_MODE_IDX
    cmp mode _INST_SINE_MODE_ATTACK
    jumpz attack
    cmp mode _INST_SINE_MODE_RESONATE
    jumpz resonate
    jump end

    label attack
        ; if 0 or less, set constant
        cmp envleft 1
        jumpl constant
            ; if there's more envelope left than there is requested, just run
            cmp envleft todo
            jumpg run
            ; otherwise, run out the rest of the envelope
            move audio_set_player_output_buffer_pos:player outpos
            move audio_run_player:player envleft
            sub todo envleft
            add outpos envleft
            move envleft 0
        label constant
            move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_CONSTANT
    jump run

    label resonate
        ; if 0 or less, no more to do
        cmp envleft 1
        jumpl done
            ; if there's more envelope left than there is requested, just run
            cmp envleft todo
            jumpg run
            ; otherwise, run out the rest of the envelope
            move audio_set_player_output_buffer_pos:player outpos
            move audio_run_player:player envleft
            sub todo envleft
            add outpos envleft
            move envleft 0
        label done
            SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_MODE_IDX _INST_SINE_MODE_OFF
        jump end
 
    label run
        move audio_set_player_output_buffer_pos:player outpos
        move audio_run_player:player todo

    label end
    add outpos todo
    sub envleft todo
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_OUTPOS_IDX outpos
    SEQUENCE_SET_INST_OBJ_MEM temp sine hInst _INST_SINE_ENVLEFT_IDX envleft
ret

SEQUENCE_MAKE_INST sine _INST_SINE_OBJSIZE
