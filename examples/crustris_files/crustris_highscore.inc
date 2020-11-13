static high_score_names string "\
          \
          \
          \
          \
          \
          \
          \
          \
          \
          "
static high_scores ints HIGH_SCORE_COUNT
static high_lines  ints HIGH_SCORE_COUNT
static high_levels ints HIGH_SCORE_COUNT

proc init_high_scores
    local i

    move i INIT_HIGH_SCORE_NAMES:
    sub i 1
    label copy_names
        move high_score_names:i INIT_HIGH_SCORE_NAMES:i
        sub i 1
        jumpl done_copy_names
    jump copy_names
    label done_copy_names

    move i INIT_HIGH_SCORES:
    sub i 1
    label copy_scores
        move high_scores:i INIT_HIGH_SCORES:i
        sub i 1
        jumpl done_copy_scores
    jump copy_scores
    label done_copy_scores

    move i INIT_HIGH_LINES:
    sub i 1
    label copy_lines
        move high_lines:i INIT_HIGH_LINES:i
        sub i 1
        jumpl done_copy_lines
    jump copy_lines
    label done_copy_lines

    move i INIT_HIGH_LEVELS:
    sub i 1
    label copy_levels
        move high_levels:i INIT_HIGH_LEVELS:i
        sub i 1
        jumpl done_copy_levels
    jump copy_levels
    label done_copy_levels
ret

proc load_high_scores
    local ok
    local i

    call savedata_check ok
    cmp ok
    jumpn load
    call init_high_scores
    jump end
    label load

    move savedata_seek 0

    move i high_score_names:
    sub i 1
    label copy_names
        move high_score_names:i savedata_read_char
        sub i 1
        jumpl done_copy_names
    jump copy_names
    label done_copy_names

    move i high_scores:
    sub i 1
    label copy_scores
        move high_scores:i savedata_read_int
        sub i 1
        jumpl done_copy_scores
    jump copy_scores
    label done_copy_scores

    move i high_lines:
    sub i 1
    label copy_lines
        move high_lines:i savedata_read_int
        sub i 1
        jumpl done_copy_lines
    jump copy_lines
    label done_copy_lines

    move i high_levels:
    sub i 1
    label copy_levels
        move high_levels:i savedata_read_int
        sub i 1
        jumpl done_copy_levels
    jump copy_levels
    label done_copy_levels

    label end
ret

proc save_high_scores
    local i
    local sum
    local sumbyte string " "

    move savedata_seek 0

    move i high_score_names:
    sub i 1
    label copy_names
        move savedata_write high_score_names:i
        sub i 1
        jumpl done_copy_names
    jump copy_names
    label done_copy_names

    move i high_scores:
    sub i 1
    label copy_scores
        move savedata_write high_scores:i
        sub i 1
        jumpl done_copy_scores
    jump copy_scores
    label done_copy_scores

    move i high_lines:
    sub i 1
    label copy_lines
        move savedata_write high_lines:i
        sub i 1
        jumpl done_copy_lines
    jump copy_lines
    label done_copy_lines

    move i high_levels:
    sub i 1
    label copy_levels
        move savedata_write high_levels:i
        sub i 1
        jumpl done_copy_levels
    jump copy_levels
    label done_copy_levels

    call savedata_calc_sum sum
    move sumbyte sum

    move savedata_seek SAVEDATA_CHECKSUM_POS
    move savedata_write sumbyte
ret

proc update_high_score_tilemap tilemap_id
    local tilemap ints HIGH_SCORE_TILEMAP_SIZE
    local pos 0
    local namepos
    local namestrpos 0
    local temp
    local i 0

    label clear
        move tilemap:i SPC
        add i 1
        cmp i HIGH_SCORE_TILEMAP_SIZE
    jumpl clear

    move i 0
    label loop
        move namepos 0
        label copy_name
            move tilemap:pos high_score_names:namestrpos
            add pos 1
            add namestrpos 1
            add namepos 1
            cmp namepos HIGH_SCORE_NAME_LENGTH
        jumpl copy_name

        call text_integer_to_string temp tilemap:pos high_levels:i
        add pos HIGH_SCORE_LEVEL_LENGTH
        call text_integer_to_string temp tilemap:pos high_lines:i
        add pos HIGH_SCORE_LINES_LENGTH
        call text_integer_to_string temp tilemap:pos high_scores:i
        add pos HIGH_SCORE_SCORE_LENGTH

        add i 1
        cmp i HIGH_SCORE_COUNT
    jumpl loop

    call gfx_set_tilemap_map tilemap_id tilemap
    call gfx_update_tilemap tilemap_id
ret

proc draw_high_score_table titlestr
    local temp ints 5
    local y 0
    local i 0

    CENTER temp:0 SCREEN_TILES_WIDTH titlestr:
    mul    temp:0 FONT_SCALED_SIDE
    move temp:1 titlestr:
    mul  temp:1 FONT_SCALED_SIDE
    call draw_text characters_obj temp:0 HIGH_SCORE_TITLE_POS temp:1 FONT_SCALED_SIDE titlestr 1

    move gfx_set_layer_colormod:labels_layer0_id C_WHT
    OBJECTS_SET_POS labels0_obj HIGH_SCORE_NAME_POS HIGH_SCORE_HEADING_POS
    move temp:0 0
    move temp:1 NAME_POS
    mul  temp:1 FONT_SIDE
    move gfx_set_layer_scroll_pos:labels_layer0_id temp
    move temp:0 NAME_TEXT:
    mul  temp:0 FONT_SIDE
    move temp:1 FONT_SIDE
    move gfx_set_layer_window:labels_layer0_id temp
    move gfx_draw_layer labels_layer0_id
    OBJECTS_SET_POS labels0_obj HIGH_SCORE_LEVEL_POS HIGH_SCORE_HEADING_POS
    move temp:0 0
    move temp:1 LEVEL_POS
    mul  temp:1 FONT_SIDE
    move gfx_set_layer_scroll_pos:labels_layer0_id temp
    move temp:0 LEVEL_TEXT:
    mul  temp:0 FONT_SIDE
    move temp:1 FONT_SIDE
    move gfx_set_layer_window:labels_layer0_id temp
    move gfx_draw_layer labels_layer0_id
    OBJECTS_SET_POS labels0_obj HIGH_SCORE_LINES_POS HIGH_SCORE_HEADING_POS
    move temp:0 0
    move temp:1 LINES_POS
    mul  temp:1 FONT_SIDE
    move gfx_set_layer_scroll_pos:labels_layer0_id temp
    move temp:0 LINES_TEXT:
    mul  temp:0 FONT_SIDE
    move temp:1 FONT_SIDE
    move gfx_set_layer_window:labels_layer0_id temp
    move gfx_draw_layer labels_layer0_id
    OBJECTS_SET_POS labels0_obj HIGH_SCORE_SCORE_POS HIGH_SCORE_HEADING_POS
    move temp:0 0
    move temp:1 SCORE_POS
    mul  temp:1 FONT_SIDE
    move gfx_set_layer_scroll_pos:labels_layer0_id temp
    move temp:0 SCORE_TEXT:
    mul  temp:0 FONT_SIDE
    move temp:1 FONT_SIDE
    move gfx_set_layer_window:labels_layer0_id temp
    move gfx_draw_layer labels_layer0_id

    label loop
        move y i
        mul  y HIGH_SCORE_SPACING
        add  y HIGH_SCORE_START_POS
        move temp:3 i
        mul  temp:3 FONT_SIDE

        OBJECTS_SET_POS high_score_obj HIGH_SCORE_NAME_POS y
        move temp:0 HIGH_SCORE_NAME_PXLENGTH
        ; need to unscale because pxlength is in scaled screen dimensions
        div  temp:0 FONT_SCALE
        move temp:1 FONT_SIDE
        move gfx_set_layer_window:high_score_layer_id temp
        move temp:4 0
        move temp:2 0
        move gfx_set_layer_scroll_pos:high_score_layer_id temp:2
        move gfx_draw_layer high_score_layer_id

        OBJECTS_SET_POS high_score_obj HIGH_SCORE_LEVEL_POS y
        move temp:0 HIGH_SCORE_LEVEL_PXLENGTH
        div  temp:0 FONT_SCALE
        move temp:1 FONT_SIDE
        move gfx_set_layer_window:high_score_layer_id temp
        add  temp:4 HIGH_SCORE_NAME_LENGTH
        move temp:2 temp:4
        mul  temp:2 FONT_SIDE
        move gfx_set_layer_scroll_pos:high_score_layer_id temp:2
        move gfx_draw_layer high_score_layer_id

        OBJECTS_SET_POS high_score_obj HIGH_SCORE_LINES_POS y
        move temp:0 HIGH_SCORE_LINES_PXLENGTH
        div  temp:0 FONT_SCALE
        move temp:1 FONT_SIDE
        move gfx_set_layer_window:high_score_layer_id temp
        add  temp:4 HIGH_SCORE_LEVEL_LENGTH
        move temp:2 temp:4
        mul  temp:2 FONT_SIDE
        move gfx_set_layer_scroll_pos:high_score_layer_id temp:2
        move gfx_draw_layer high_score_layer_id

        OBJECTS_SET_POS high_score_obj HIGH_SCORE_SCORE_POS y
        move temp:0 HIGH_SCORE_SCORE_PXLENGTH
        div  temp:0 FONT_SCALE
        move temp:1 FONT_SIDE
        move gfx_set_layer_window:high_score_layer_id temp
        add  temp:4 HIGH_SCORE_LINES_LENGTH
        move temp:2 temp:4
        mul  temp:2 FONT_SIDE
        move gfx_set_layer_scroll_pos:high_score_layer_id temp:2
        move gfx_draw_layer high_score_layer_id

        add i 1
        cmp i HIGH_SCORE_COUNT
    jumpl loop
ret

proc check_high_score pos pscore
    move pos 0

    label loop
        cmp pscore high_scores:pos
        jumpg done
        add pos 1
        cmp pos HIGH_SCORE_COUNT
    jumpl loop
    label done
ret

proc bump_high_score pos
    local i
    local j
    local k
    local srcpos
    local destpos

    move j HIGH_SCORE_COUNT
    sub  j 1
    move i HIGH_SCORE_COUNT
    sub  i 2

    label loop
        cmp j pos
        jumpz done

        move srcpos i
        mul  srcpos HIGH_SCORE_NAME_LENGTH
        move destpos j
        mul  destpos HIGH_SCORE_NAME_LENGTH
        move k HIGH_SCORE_NAME_LENGTH
        label loop2
            move high_score_names:destpos high_score_names:srcpos
            add srcpos 1
            add destpos 1
            sub k 1
        jumpg loop2
        move high_lines:j  high_lines:i
        move high_levels:j high_levels:i
        move high_scores:j high_scores:i

        sub i 1
        sub j 1
    jump loop
    label done

    move destpos pos
    mul  destpos HIGH_SCORE_NAME_LENGTH
    move k HIGH_SCORE_NAME_LENGTH
    label loop3
        move high_score_names:destpos SPC
        add destpos 1
        sub k 1
    jumpg loop3
ret


