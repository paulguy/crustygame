macro _INST_CALC_VELOCITY UNIQ
    cmp velocity 0
    jumpg faster_UNIQ
    ; slower
    mul velocity -1
    add velocity 1
    div base_env_speed velocity
    div vol velocity
    jump done_velocity_UNIQ
    label faster_UNIQ
    add velocity 1
    mul base_env_speed velocity
    mul vol velocity
    label done_velocity_UNIQ
endmacro _INST_CALC_VELOCITY

expr _INST_TONE_PLAYER_IDX 0
expr _INST_TONE_VOL_PLAYER_IDX 1
expr _INST_TONE_SLOPE_PLAYER_IDX 2
expr _INST_TONE_BIAS_PLAYER_IDX 3
expr _INST_TONE_MODE_IDX 4
expr _INST_TONE_OUTPOS_IDX 5
expr _INST_TONE_NOTE_IDX 6
expr _INST_TONE_OBJSIZE 7

expr _INST_TONE_MODE_OFF      0
expr _INST_TONE_MODE_ATTACK   1
expr _INST_TONE_MODE_RESONATE 2

macro _CRUSTRIS_MAKE_TONE_INST NAME FUNC
    static _inst_NAME_buf
    static _inst_NAME_len
    static _inst_NAME_env_asc
    static _inst_NAME_env_desc
    static _inst_NAME_env_const
    static _inst_NAME_env_len

    proc inst_NAME_init
        local buf
        local temp ints 2

        SEQUENCE_CALC_LENGTH _inst_NAME_len 440
        call FUNC sequence_user_scratch_mem _inst_NAME_len -1 1
        move set_buffer sequence_user_scratch_mem
        move temp:0 AUDIO_TYPE_F64
        move temp:1 _inst_NAME_len
        move audio_add_buffer temp
        move _inst_NAME_buf get_return

        ; make it simple so the length is determined by a ratio of a second
        SEQUENCE_CALC_LENGTH _inst_NAME_env_len 1

        call wave_make_saw_ascending sequence_user_scratch_mem _inst_NAME_env_len 0 1
        move set_buffer sequence_user_scratch_mem
        move temp:0 AUDIO_TYPE_F64
        move temp:1 _inst_NAME_env_len
        move audio_add_buffer temp
        move _inst_NAME_env_asc get_return

        call wave_make_saw_descending sequence_user_scratch_mem _inst_NAME_env_len 0 1
        move set_buffer sequence_user_scratch_mem
        move temp:0 AUDIO_TYPE_F64
        move temp:1 _inst_NAME_env_len
        move audio_add_buffer temp
        move _inst_NAME_env_desc get_return

        call wave_make_dc sequence_user_scratch_mem _inst_NAME_env_len 1
        move set_buffer sequence_user_scratch_mem
        move temp:0 AUDIO_TYPE_F64
        move temp:1 _inst_NAME_env_len
        move audio_add_buffer temp
        move _inst_NAME_env_const get_return
    ret

    proc inst_NAME_free
        move audio_free_buffer _inst_NAME_env_const
        move audio_free_buffer _inst_NAME_env_desc
        move audio_free_buffer _inst_NAME_env_asc
        move audio_free_buffer _inst_NAME_buf
    ret

    proc inst_NAME_obj_init hInst
        local temp
        local volplayer
        local biasplayer
        local slopeplayer
        local player

        move audio_add_player _inst_NAME_env_asc
        move volplayer get_return
        move audio_set_player_mode:volplayer AUDIO_MODE_ONCE
        move audio_set_player_output_mode:volplayer AUDIO_OUTPUT_MODE_REPLACE
        move audio_set_player_output_buffer:volplayer sequence_scratch_volume_buffer
     
        move audio_add_player _inst_NAME_env_const
        move biasplayer get_return
        move audio_set_player_mode:biasplayer AUDIO_MODE_LOOP
        move audio_set_player_output_mode:biasplayer AUDIO_OUTPUT_MODE_ADD
        move audio_set_player_output_buffer:biasplayer sequence_scratch_speed_buffer
     
        move audio_add_player _inst_NAME_env_asc
        move slopeplayer get_return
        move audio_set_player_mode:slopeplayer AUDIO_MODE_ONCE
        move audio_set_player_output_mode:slopeplayer AUDIO_OUTPUT_MODE_REPLACE
        move audio_set_player_output_buffer:slopeplayer sequence_scratch_speed_buffer
     
        move audio_add_player _inst_NAME_buf
        move player get_return
        move audio_set_player_mode:player AUDIO_MODE_LOOP
        move audio_set_player_output_mode:player AUDIO_OUTPUT_MODE_ADD

        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_SLOPE_PLAYER_IDX slopeplayer
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_BIAS_PLAYER_IDX biasplayer
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_VOL_PLAYER_IDX volplayer
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_PLAYER_IDX player
        ; start inactive
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_MODE_IDX _INST_TONE_MODE_OFF
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_NOTE_IDX 0
    ret

    proc inst_NAME_obj_free hInst
        local temp

        SEQUENCE_GET_INST_OBJ_MEM temp temp NAME hInst _INST_TONE_PLAYER_IDX
        move audio_free_player temp
        SEQUENCE_GET_INST_OBJ_MEM temp temp NAME hInst _INST_TONE_VOL_PLAYER_IDX
        move audio_free_player temp
        SEQUENCE_GET_INST_OBJ_MEM temp temp NAME hInst _INST_TONE_BIAS_PLAYER_IDX
        move audio_free_player temp
        SEQUENCE_GET_INST_OBJ_MEM temp temp NAME hInst _INST_TONE_SLOPE_PLAYER_IDX
        move audio_free_player temp
    ret

    ; note 0 is rest/off
    proc inst_NAME_note hInst note velocity remain
        local temp
        local tempf floats 2
        local freq floats 1
        local speed floats 1
        local player
        local volplayer
        local biasplayer
        local slopeplayer
        ; 1/10 second
        local base_env_speed floats "10.0"
        local oldnote
        local vol floats "0.1"

        SEQUENCE_GET_INST_OBJ_MEM temp player NAME hInst _INST_TONE_PLAYER_IDX
        SEQUENCE_GET_INST_OBJ_MEM temp volplayer NAME hInst _INST_TONE_VOL_PLAYER_IDX

        SEQUENCE_GET_INST_OBJ_MEM temp oldnote NAME hInst _INST_TONE_NOTE_IDX
        ; don't do anything if the note didn't change
        cmp oldnote note
        jumpz end
        ; check if the note changed to note off
        cmp note 0
        jumpz note_off
        ; check if the note changed from a note off
        cmp oldnote 0
        jumpz note_on
        ; if the note changed, and it wasn't to or from a rest, then bend it
        jump note_bend

        label note_on
            move audio_set_player_input_buffer_pos:volplayer 0
            move audio_set_player_input_buffer:volplayer _inst_NAME_env_asc

            _INST_CALC_VELOCITY a
            move audio_set_player_speed:volplayer base_env_speed
            move audio_set_player_input_buffer_pos:player 0
            call sequence_get_note_freq freq note
            SEQUENCE_CALC_SPEED tempf speed _inst_NAME_len freq
            move audio_set_player_speed:player speed
            move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_CONSTANT
            move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_SOURCE
            move audio_set_player_volume:player vol

            SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_NOTE_IDX note
            SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_MODE_IDX _INST_TONE_MODE_ATTACK
        jump end

        label note_bend
            SEQUENCE_GET_INST_OBJ_MEM temp biasplayer NAME hInst _INST_TONE_BIAS_PLAYER_IDX
            SEQUENCE_GET_INST_OBJ_MEM temp slopeplayer NAME hInst _INST_TONE_SLOPE_PLAYER_IDX
            ; set note now, then set up a speed bias envelope to transition from
            ; the frequency it was at to the final frequency
            SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_NOTE_IDX note
            call sequence_get_note_freq freq note
            ; set the player speed to the destination speed it should bend to
            SEQUENCE_CALC_SPEED tempf speed _inst_NAME_len freq
            move audio_set_player_speed:player speed
            move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_SOURCE

            ; determine the speed of the speed player to make hte bend the right
            ; length for the note.  Abuse SEQUENCE_CALC_LENGTH, since it just
            ; divides the rate by what's fed in to it, which should be correct
            ; because the desired value is the speed required to complete the full
            ; envelope sample (which is 1 second) in the length of the note
            SEQUENCE_CALC_LENGTH tempf remain
            move audio_set_player_speed:slopeplayer tempf

            call sequence_get_note_freq tempf oldnote
            cmp freq tempf
            jumpg bend_up
            ; bend down
                ; reset to beginning
                move audio_set_player_input_buffer_pos:slopeplayer 0
                ; needs to descend down to 1.0
                move audio_set_player_input_buffer:slopeplayer _inst_NAME_env_desc
                ; find the starting factor to multiply by to start the envelope
                ; on the original frequency
                ; tempf still contains the old frequency
                div tempf freq
                sub tempf 1
                move audio_set_player_volume:slopeplayer tempf
                move tempf 1
                move audio_set_player_volume:biasplayer tempf
        jump end
            label bend_up
                move audio_set_player_input_buffer_pos:slopeplayer 0
                ; needs to ascend up from the bias point to 1.0
                move audio_set_player_input_buffer:slopeplayer _inst_NAME_env_asc
                div tempf freq
                move audio_set_player_volume:biasplayer tempf
                move tempf:1 tempf
                move tempf 1
                sub  tempf tempf:1
                move audio_set_player_volume:slopeplayer tempf
        jump end

        label note_off
            ; don't try to trigger note off to a rest
            cmp oldnote 0
            jumpz end
            SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_NOTE_IDX note
            move audio_set_player_input_buffer:volplayer _inst_NAME_env_desc
            move audio_set_player_input_buffer_pos:volplayer 0

            _INST_CALC_VELOCITY b
            move audio_set_player_speed:volplayer base_env_speed
            move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_SOURCE
            SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_MODE_IDX _INST_TONE_MODE_RESONATE

        label end
    ret

    proc inst_NAME_frame hInst needed
        local temp
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_OUTPOS_IDX 0
    ret

    proc inst_NAME_run hInst request
        local player
        local volplayer
        local slopeplayer
        local biasplayer
        local mode
        local temp
        local outpos
        local todo
        local volgot
        local slopegot

        move todo request

        SEQUENCE_GET_INST_OBJ_MEM temp player NAME hInst _INST_TONE_PLAYER_IDX
        SEQUENCE_GET_INST_OBJ_MEM temp volplayer NAME hInst _INST_TONE_VOL_PLAYER_IDX
        SEQUENCE_GET_INST_OBJ_MEM temp slopeplayer NAME hInst _INST_TONE_SLOPE_PLAYER_IDX
        SEQUENCE_GET_INST_OBJ_MEM temp biasplayer NAME hInst _INST_TONE_BIAS_PLAYER_IDX
        SEQUENCE_GET_INST_OBJ_MEM temp outpos NAME hInst _INST_TONE_OUTPOS_IDX
        move audio_set_player_output_buffer_pos:player outpos

        SEQUENCE_GET_INST_OBJ_MEM temp mode NAME hInst _INST_TONE_MODE_IDX
        cmp mode _INST_TONE_MODE_ATTACK
        jumpz attack
        cmp mode _INST_TONE_MODE_RESONATE
        jumpz resonate
        jump end

        label attack
            ; try to run envelope players
            move audio_set_player_output_buffer_pos:volplayer 0
            move audio_run_player:volplayer todo
            move volgot get_return
            move audio_set_player_output_buffer_pos:slopeplayer 0
            move audio_run_player:slopeplayer todo
            move slopegot get_return
            move audio_set_player_output_buffer_pos:biasplayer 0
            move audio_run_player:biasplayer slopegot

            ; in case the envelope ends on the call boundary
            cmp volgot 0
            jumpn done_vol
                move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_CONSTANT
            label done_vol

            cmp slopegot 0
            jumpn done_slope
                move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_CONSTANT
            label done_slope

            ; setting these resets the position to 0
            move audio_set_player_volume_source:player sequence_scratch_volume_buffer
            move audio_set_player_speed_source:player sequence_scratch_speed_buffer
            label loop_attack
                ; find the minimum, nonzero value, otherwise keep it as todo
                ; because both should be in constant mode
                move temp todo
                cmp volgot 1
                jumpl minslopegot
                move temp volgot
                cmp  temp slopegot
                jumpl minvolgot
                label minslopegot
                    cmp slopegot 1
                    jumpl minvolgot
                    move temp slopegot
                label minvolgot

                move audio_run_player:player temp
                add outpos temp

                ; check if the volume or speed envelopes returned less than todo,
                ; indicating they're done
                cmp volgot todo
                jumpl vol_constant
                jump no_vol_constant
                label vol_constant
                    move audio_set_player_volume_mode:player AUDIO_VOLUME_MODE_CONSTANT
                label no_vol_constant
                sub volgot temp

                cmp slopegot todo
                jumpl slope_constant
                jump no_slope_constant
                label slope_constant
                    move audio_set_player_speed_mode:player AUDIO_SPEED_MODE_CONSTANT
                label no_slope_constant
                sub slopegot temp

                sub todo temp
                cmp todo 0
            jumpg loop_attack
        jump end

        label resonate
            move audio_set_player_output_buffer_pos:volplayer 0
            move audio_run_player:volplayer todo
            move temp get_return
            cmp temp 0
            jumpz note_off
                move audio_set_player_volume_source:player sequence_scratch_volume_buffer
                move audio_run_player:player temp
                add outpos temp
                sub todo temp
                cmp todo 0
                jumpz end
            label note_off
                SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_MODE_IDX _INST_TONE_MODE_OFF

        label end
        SEQUENCE_SET_INST_OBJ_MEM temp NAME hInst _INST_TONE_OUTPOS_IDX outpos
    ret

    SEQUENCE_MAKE_INST NAME _INST_TONE_OBJSIZE
endmacro _CRUSTRIS_MAKE_TONE_INST

_CRUSTRIS_MAKE_TONE_INST sine wave_make_cosine_approx
_CRUSTRIS_MAKE_TONE_INST saw wave_make_saw_descending
