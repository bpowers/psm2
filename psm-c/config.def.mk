VERSION = 0.1.0

PREFIX=/usr/local

WARNFLAGS := -pedantic -Wvariadic-macros \
             -Wformat -Wall -Wextra -Wundef -Wpointer-arith \
             -Wcast-qual -Wwrite-strings -Wsign-compare \
             -Wstrict-aliasing=2 -Wno-unused-parameter \
             -Werror -Wno-error=unused-function

CPPFLAGS = -DVERSION=\"${VERSION}\" -D_DEFAULT_SOURCE -D_BSD_SOURCE -D_XOPEN_SOURCE=600
CFLAGS += -g -std=c99 -Os -flto ${WARNFLAGS} ${CPPFLAGS}
LDFLAGS += -g -flto -static

CC = /usr/local/musl/bin/musl-gcc
