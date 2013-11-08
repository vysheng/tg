CC=cc
CFLAGS=-c -Wall -Wextra -Werror -fPIC -ggdb -O2 -fno-omit-frame-pointer -fno-strict-aliasing -rdynamic
LDFLAGS=-lreadline -lssl -lcrypto -lrt -lz -lconfig -ggdb -rdynamic
LD=cc

SRC=main.c loop.c interface.c net.c mtproto-common.c mtproto-client.c queries.c structures.c
OBJ=$(SRC:.c=.o)
EXE=telegram
HDRS=include.h  interface.h  loop.h  mtproto-client.h  mtproto-common.h  net.h  queries.h  structures.h  telegram.h  tree.h

all: $(SRC) $(EXE)
	
$(EXE): $(OBJ) 
	$(LD) $(OBJ) $(LDFLAGS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm *.o telegram || true
