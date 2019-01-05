PREFIX = /usr/local

EXE = target/release/psm

.SUFFIXES:

all: $(EXE)

$(EXE):
	cargo build --release
	strip $(EXE)

install:
	install -c -m 4755 $(EXE) $(PREFIX)/bin/$(shell basename $(EXE))

clean:
	cargo clean

.PHONY: all install $(EXE) clean
