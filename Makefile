DEBUG=yes

CC = gcc
CFLAGS = -Wall -Wextra

NDISWRAPPER = ndiswrapper
SRC = ndiswrapper.c
HEAD = ndiswrapper.h

ifeq ($(DEBUG),yes)
	CFLAGS += -g
endif

all: ndiswrapper

ndiswrapper: $(SRC) $(HEAD)
	$(CC) $(SRC) $(CFLAGS) -o $(NDISWRAPPER)

clean:
	rm -f $(NDISWRAPPER)

.phony: clean
