CC=cc
CFLAGS=-c -Wall -Wextra -fPIC
LDFLAGS=-lreadline
LD=cc

SRC=main.c loop.c interface.c
OBJ=$(SRC:.c=.o)
EXE=telegram

all: $(SRC) $(EXE)
	
$(EXE): $(OBJ) 
	$(LD) $(LDFLAGS) $(OBJ) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

