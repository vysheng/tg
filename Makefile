srcdir=.

CFLAGS=-I/usr/local/include
LDFLAGS=-L/usr/local/lib
CPPFLAGS=
DEFS=-DHAVE_CONFIG_H
COMPILE_FLAGS=${CFLAGS} ${CPPFLAGS} ${DEFS} -Wall -Wextra -Werror -fno-strict-aliasing -fno-omit-frame-pointer -ggdb

EXTRA_LIBS=
LOCAL_LDFLAGS=-lm -lcrypto -lz -lssl -rdynamic -ggdb -lconfig -lreadline -lexecinfo ${EXTRA_LIBS}
LINK_FLAGS=${LDFLAGS} ${LOCAL_LDFLAGS}

HEADERS= ${srcdir}/constants.h  ${srcdir}/include.h  ${srcdir}/interface.h  ${srcdir}/LICENSE.h  ${srcdir}/loop.h  ${srcdir}/mtproto-client.h  ${srcdir}/mtproto-common.h  ${srcdir}/net.h  ${srcdir}/no-preview.h  ${srcdir}/queries.h  ${srcdir}/structures.h  ${srcdir}/telegram.h  ${srcdir}/tree.h ${srcdir}/config.h ${srcdir}/binlog.h
INCLUDE=-I. -I${srcdir}
CC=gcc
OBJECTS=main.o loop.o interface.o net.o mtproto-common.o mtproto-client.o queries.o structures.o binlog.o

.SUFFIXES:

.SUFFIXES: .c .h .o

all: telegram

${OBJECTS}: ${HEADERS}

telegram: ${OBJECTS}
	${CC} ${OBJECTS} ${LINK_FLAGS} -o $@

.c.o :
	${CC} ${COMPILE_FLAGS} ${INCLUDE} -c $< -o $@


clean:
	rm -rf *.o telegram > /dev/null || echo "all clean" 

