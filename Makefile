DEBUG=yes

CC? = gcc
CFLAGS += -Wall -Wextra

SRC = ndiswrapper.c

ifndef PROJ
	PROJ = ndiswrapper
	STRIP = strip
else
	DEBUG = no
	LDFLAGS += -static
endif

ifeq ($(DEBUG),yes)
	CFLAGS += -g
endif

all: ndiswrapper

ndiswrapper: $(SRC)
	$(CC) $(SRC) $(CFLAGS) -o $(PROJ) $(LDFLAGS)
ifeq ($(DEBUG),no)
	$(STRIP) $(PROJ)
endif

clean:
	rm -f $(PROJ)

.phony: clean

distclean:
	rm -f ndiswrapper ndiswrapper.exe

.phony: distclean
