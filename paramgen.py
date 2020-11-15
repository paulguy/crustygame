#!/usr/bin/env python3

import sys
from math import pow

def paramgen(curve, bpm, div, startval, endval, notelens):
    times = list()
    starts = list()
    diffs = list()
    lengths = list()

    if curve == "linear":
        func = lambda total, start, length: ((start + length) / total) - (start / total)
    elif curve == "power":
        func = lambda total, start, length: pow((start + length) / total, 2) - pow(start / total, 2)
    else:
        raise(ValueError("curve must be \"linear\" or \"power\", was {}.".format(curve)))

    wholepersec = 60 / (bpm / div)

    totaltime = 0
    for char in notelens:
        if char == 'w':
            totaltime = totaltime + 1
            times.append(1)
        elif char == 'h':
            totaltime = totaltime + (1 / 2)
            times.append(1 / 2)
        elif char == 'q':
            totaltime = totaltime + (1 / 4)
            times.append(1 / 4)
        elif char == 'e':
            totaltime = totaltime + (1 / 8)
            times.append(1 / 8)
        elif char == 's':
            totaltime = totaltime + (1 / 16)
            times.append(1 / 16)
        elif char == 't':
            totaltime = totaltime + (1 / 32)
            times.append(1 / 32)
        elif char == 'i':
            totaltime = totaltime + (1 / 64)
            times.append(1 / 64)
        elif char == 'o':
            totaltime = totaltime + (1 / 128)
            times.append(1 / 128)
        else:
            raise(ValueError("Invalid note length char: {}".format(char)))

    totaldiff = endval - startval
    wholediff = totaldiff / totaltime
    start = startval
    curtime = 0
    for time in times:
        starts.append(start)
        diff = totaldiff * func(totaltime, curtime, time)
        curtime = curtime + time
        diffs.append(diff)
        lengths.append(time * wholepersec)
        start = start + diff

    print("Starts: ", end='')
    for start in starts:
        print(" {: .6f}".format(start), end='')
    print()
    print("Diffs:  ", end='')
    for diff in diffs:
        print(" {: .6f}".format(diff), end='')
    print()
    print("Lengths:", end='')
    for length in lengths:
        print(" {: .6f}".format(length), end='')
    print()


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: {} <linear|power> <tempo> <div> <startval> <endval> <notelens>".format(sys.argv[0]))
    else:
        paramgen(sys.argv[1],
                 float(sys.argv[2]),
                 int(sys.argv[3]),
                 float(sys.argv[4]),
                 float(sys.argv[5]),
                 sys.argv[6])
