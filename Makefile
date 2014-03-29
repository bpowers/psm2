include config.mk

EXE = psm
SRC = psm.c
OBJ = ${SRC:.c=.o}

all: $(EXE)

config.h:
	cp config.def.h config.h

.c.o:
	@echo "  CC    $<"
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

$(EXE): ${OBJ}
	@echo "  LD    $@"
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f $(EXE) ${OBJ}

.PHONY: all clean
