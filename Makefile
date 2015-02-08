include config.mk

EXE = psm
SRC = psm.c
OBJ = ${SRC:.c=.o}

all: $(EXE)

config.h:
	cp config.def.h config.h

config.mk:
	cp config.def.mk config.mk

.c.o:
	@echo "  CC    $<"
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

$(EXE): ${OBJ}
	@echo "  LD    $@"
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

install: ${EXE}
	install -c -m 4755 ${EXE} ${PREFIX}/bin/${EXE}

clean:
	rm -f $(EXE) ${OBJ}

.PHONY: all install clean
