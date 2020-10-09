expr _TIMERS_MAX 20

static _timers_rate ints _TIMERS_MAX
static _timers_next ints _TIMERS_MAX

proc timers_clear
    local i _TIMERS_MAX

    label loop
        sub i 1
        jumpl done
        move _timers_rate:i -1
    jump loop
    label done
ret

macro TIMERS_SET_RATE ID RATE
    move _timers_rate:ID RATE
    move _timers_next:ID get_ticks
    add  _timers_next:ID RATE
endmacro TIMERS_SET_RATE

proc timers_get_free id rate
    move id _TIMERS_MAX

    label loop
        sub id 1
        jumpl end
        cmp _timers_rate:id 0
        jumpl found
    jump loop
    label found
    TIMERS_SET_RATE id rate
    label end
ret

macro TIMERS_FREE ID
    move _timers_rate:ID -1
endmacro TIMERS_FREE

macro TIMERS_RESET ID
    move _timers_next:ID get_ticks
    add  _timers_next:ID _timer_rate:ID
endmacro TIMERS_RESET

proc timers_check elapsed timer_id
    local temp

    move elapsed 0

    ; if the timer hasn't passed yet, just end
    move temp get_ticks
    sub  temp _timers_next:timer_id
    jumpl end
    ; find how many timers have been missed
    move elapsed temp
    div  elapsed _timers_rate:timer_id
    add  elapsed 1
    ; update the timer
    move temp elapsed
    mul  temp _timers_rate:timer_id
    add  _timers_next:timer_id temp
    label end
ret
