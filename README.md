# Chat server and client using select()

This example provides a simple multi-client chat server and a terminal client implemented in C using `select()` for I/O multiplexing.

## Files
- `server.c` — TCP chat server that accepts multiple clients and broadcasts messages.
- `client.c` — TCP chat client that reads from stdin and displays messages from the server.
- `Makefile` — Convenience build targets.

## Build

```sh
make
```

This produces:
- `./chat_server`
- `./chat_client`

Or build individually:

```sh
cc -Wall -Wextra -O2 -o chat_server server.c
cc -Wall -Wextra -O2 -o chat_client client.c
```

## Run

1. Start the server:
   ```sh
   ./chat_server 5555
   ```

2. Start one or more clients in separate terminals:
   ```sh
   ./chat_client 127.0.0.1 5555
   ```
   or with IPv6:
   ```sh
   ./chat_client ::1 5555
   ```

3. Type into any client and press Enter. Messages are broadcast to other connected clients.
   - Exit client: type `/quit` or press Ctrl-D
   - Stop server: press Ctrl-C

## Notes

- The code uses `getaddrinfo` and supports both IPv4 and IPv6.
- `select()` is used to monitor multiple sockets and stdin without threads.
- Sockets are blocking for simplicity. For production, consider non-blocking sockets and write buffers to avoid head-of-line blocking when broadcasting to slow receivers.
- The example treats input as line-oriented text. The client sends lines terminated by newline, and the server prefixes broadcasts with the sender's address.
- `FD_SETSIZE` limits the maximum file descriptor number that `select()` can monitor. On many systems this is 1024; adjust or use `poll`/`epoll`/`kqueue` for larger scales.
- Tested on Linux/macOS toolchains supporting POSIX sockets.
