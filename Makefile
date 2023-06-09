.PHONY: all debug lo clean purge

ifeq ($(strip $(IF)),)
    $(error A variável IF não está definida. Utilize: make IF=<interface de rede>)
endif

LFLAGS = -pthread -lssl -lcrypto -g
CFLAGS = -g

all: CFLAGS += -DNETINTERFACE=\"$(IF)\"
all: client server

WARNING = -Wall -Wextra -Wno-packed-bitfield-compat


# Alvo "debug"
ifeq ($(filter debug,$(MAKECMDGOALS)),debug)
CFLAGS += -DDEBUG
endif

# Alvo "showDeleted"
ifeq ($(filter showDeleted,$(MAKECMDGOALS)),showDeleted)
CFLAGS += -DSHOWDELETED="TRUE"
endif

RUN_ARGS := $(wordlist 2,$(words $(MAKECMDGOALS)),$(MAKECMDGOALS)) \
	$(eval $(RUN_ARGS):;@:)

client: Client.c ConexaoRawSocket.o message.o pilha.o tokenlizer.o connectionManager.o files.o
	gcc $(CFLAGS) -o client Client.c ConexaoRawSocket.o message.o pilha.o tokenlizer.o connectionManager.o files.o $(WARNING) $(LFLAGS)

server: Server.c ConexaoRawSocket.o message.o pilha.o tokenlizer.o connectionManager.o files.o
	gcc $(CFLAGS) -o server Server.c ConexaoRawSocket.o message.o pilha.o tokenlizer.o connectionManager.o files.o $(WARNING) $(LFLAGS)

ConexaoRawSocket.o: ConexaoRawSocket.c ConexaoRawSocket.h
	gcc $(CFLAGS) -o ConexaoRawSocket.o -c ConexaoRawSocket.c $(WARNING)

message.o: message.c message.h
	gcc $(CFLAGS) -o message.o -c message.c $(WARNING)

pilha.o: pilha.c pilha.h
	gcc $(CFLAGS) -o pilha.o -c pilha.c $(WARNING)

tokenlizer.o: tokenlizer.c tokenlizer.h
	gcc $(CFLAGS) -o tokenlizer.o -c tokenlizer.c $(WARNING)

files.o: files.c files.h
	gcc $(CFLAGS) -o files.o -c files.c $(WARNING)

connectionManager.o: connectionManager.c connectionManager.h message.h
	gcc $(CFLAGS) -o connectionManager.o -c connectionManager.c $(WARNING)

lo: CFLAGS += -DLO
lo: all

debug: all

clean:
	- rm -f *.o *~

purge: clean
	- rm -f client server a.out core
