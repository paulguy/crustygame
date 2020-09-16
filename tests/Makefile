
CFLAGS=-Wall -fprofile-arcs -ftest-coverage -O0 -g
LDFLAGS=-lssl -lcrypto -pthread
TARGETS=net.test x509.test

all: $(TARGETS)

%.test: unity/unity.o %.test.o ../%.o ../x509.o
	@echo "$@ $<"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -vf $(TARGETS)
	rm -vf *.gcda *.gcno *.o unity/*.o unity/*.gcda unity/*.gcno
.PHONY: clean
