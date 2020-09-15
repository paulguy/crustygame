OBJS   = callbacks.o crustyvm.o tilemap.o main.o
TARGET = crustygame
CFLAGS = `pkg-config sdl2 --cflags` -Wall -Wextra -Wno-unused-parameter -ggdb -Og
LDFLAGS = `pkg-config sdl2 --libs` -lm

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $(TARGET) $(OBJS)

all: $(TARGET)

clean:
	rm -f $(TARGET) $(OBJS)

.PHONY: clean
