expr _MAX_TIMERS 20

static _timer_rate ints _MAX_TIMERS
static _timer_next ints _MAX_TIMERS

proc set_timer_rate timer_id rate
    move _timer_rate:timer_id rate
    move _timer_next:timer_id get_ticks
    add  _timer_next:timer_id rate
ret

proc reset_timer timer_id
    move _timer_next:timer_id get_ticks
    add  _timer_next:timer_id _timer_rate:timer_id
ret

proc check_timer elapsed timer_id
    move elapsed 0

    cmp get_ticks _timer_next:timer_id
    jumpl end

    move elapsed 1
    add _timer_next:timer_id _timer_rate:timer_id
    label end
ret
