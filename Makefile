CC ?= cc
CFLAGS ?= -Wall -Wextra -O2

all: chat_server chat_client

chat_server: server.c
	$(CC) $(CFLAGS) -o $@ $<

chat_client: client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f chat_server chat_client

.PHONY: all clean