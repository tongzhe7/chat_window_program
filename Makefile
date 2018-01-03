CC=gcc

CFLAG+=`pkg-config --cflags --libs gtk+-2.0`
#CFLAG+=-Wall -export-dynamic
CFLAG+=-g
CFLAG+=-lpthread

OBJ = Server \
	  Client

all: $(OBJ)

Server: Server.c
	    $(CC) $^ -o $@ $(CFLAG)

Client: Client.c
	    $(CC) $^ -o $@ $(CFLAG)

.PHONY: clean

clean:
	rm -rf $(OBJ)
	rm -rf *.o
