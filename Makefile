OBJS   = callbacks.o crustyvm.o tilemap.o synth.o main.o
TARGET = crustygame
CFLAGS = `pkg-config sdl2 --cflags` -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -ggdb -Og
LDFLAGS = `pkg-config sdl2 --libs` -lm

$(TARGET): $(OBJS)
	$(CC) -o $(TARGET) $(OBJS) $(LDFLAGS)

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
