CC = c89
CFLAGS = -Wall -Wpedantic -D_POSIX_C_SOURCE=199506L
LDFLAGS = 

SOURCES = messageio.c server.c server_main.c poll_record.c
OBJECTS = messageio.o server.o server_main.o poll_record.o
HEADERS = messageio.h server.h standards.h

SRCS = $(patsubst %, src/%, $(SOURCES))
OBJS = $(patsubst %, obj/%, $(OBJECTS))
INCS = $(patsubst %, inc/%, $(HEADERS))
TARGET = server_main

.PHONY: all
all: $(TARGET)

obj/%.o: src/%.c $(INCS)
	$(CC) -c -o $@ $< $(CFLAGS)
$(TARGET): $(OBJS)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean
clean:
	rm $(TARGET) obj/*.o

