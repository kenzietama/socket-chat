/*
 Simple chat server using select() for I/O multiplexing.
 Supports multiple clients, broadcasting line-based messages from any client to all others.

 Build:
   cc -Wall -Wextra -O2 -o chat_server server.c

 Run:
   ./chat_server <port>

 Notes:
 - IPv4/IPv6 supported via getaddrinfo.
 - Uses select() and FD_SETSIZE-limited number of file descriptors.
 - Uses blocking sockets for simplicity. For production, consider non-blocking + write buffers.
*/

#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BACKLOG 16
#define RECV_BUF 4096
#define OUT_BUF  (RECV_BUF + 128) // space for prefix

static int g_listener_fd = -1;

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (g_listener_fd >= 0) close(g_listener_fd);
    exit(EXIT_FAILURE);
}

static void on_sigint(int signo) {
    (void)signo;
    if (g_listener_fd >= 0) close(g_listener_fd);
    fprintf(stderr, "\nServer shutting down.\n");
    exit(0);
}

static void sockaddr_to_str(const struct sockaddr *sa, char *host, size_t hostlen, char *serv, size_t servlen) {
    int rc = getnameinfo(sa,
                         (sa->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
                         host, (socklen_t)hostlen, serv, (socklen_t)servlen,
                         NI_NUMERICHOST | NI_NUMERICSERV);
    if (rc != 0) {
        snprintf(host, hostlen, "unknown");
        snprintf(serv, servlen, "0");
    }
}

static int create_and_listen(const char *port) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;      // both IPv4/IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;      // for binding

    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) die("getaddrinfo: %s", gai_strerror(rc));

    int listener = -1;

    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            if (listen(fd, BACKLOG) == 0) {
                listener = fd;
                break;
            }
        }
        close(fd);
    }

    char host[NI_MAXHOST], serv[NI_MAXSERV];
    if (listener >= 0) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        if (getsockname(listener, (struct sockaddr *)&ss, &slen) == 0) {
            sockaddr_to_str((struct sockaddr *)&ss, host, sizeof(host), serv, sizeof(serv));
            fprintf(stderr, "Listening on %s:%s\n", host, serv);
        }
    }

    freeaddrinfo(res);

    if (listener < 0) die("Failed to bind/listen on port %s", port);
    return listener;
}

static void broadcast(fd_set *master, int fdmax, int sender_fd, int listener_fd, const char *data, size_t len) {
    for (int j = 0; j <= fdmax; ++j) {
        if (j != listener_fd && j != sender_fd && FD_ISSET(j, master)) {
            ssize_t sent = send(j, data, len, 0);
            if (sent < 0) {
                // On error, close and remove this client
                fprintf(stderr, "send to fd %d failed (%s). Closing.\n", j, strerror(errno));
                close(j);
                FD_CLR(j, master);
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    g_listener_fd = create_and_listen(argv[1]);

    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);

    FD_SET(g_listener_fd, &master);
    int fdmax = g_listener_fd;

    char recvbuf[RECV_BUF];
    char outbuf[OUT_BUF];

    while (1) {
        read_fds = master;
        int ready = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            die("select failed: %s", strerror(errno));
        }

        for (int i = 0; i <= fdmax && ready > 0; ++i) {
            if (!FD_ISSET(i, &read_fds)) continue;
            ready--;

            if (i == g_listener_fd) {
                // New connection
                struct sockaddr_storage remote_addr;
                socklen_t addr_len = sizeof(remote_addr);
                int newfd = accept(g_listener_fd, (struct sockaddr *)&remote_addr, &addr_len);
                if (newfd < 0) {
                    fprintf(stderr, "accept failed: %s\n", strerror(errno));
                    continue;
                }

                FD_SET(newfd, &master);
                if (newfd > fdmax) fdmax = newfd;

                char host[NI_MAXHOST], serv[NI_MAXSERV];
                sockaddr_to_str((struct sockaddr *)&remote_addr, host, sizeof(host), serv, sizeof(serv));
                fprintf(stderr, "New connection from %s:%s on fd %d\n", host, serv, newfd);

                // Welcome message
                int n = snprintf(outbuf, sizeof(outbuf), "[server] Welcome! There are up to %d fds available.\n", FD_SETSIZE);
                send(newfd, outbuf, (size_t)n, 0);

            } else {
                // Data from a client
                ssize_t nbytes = recv(i, recvbuf, sizeof(recvbuf) - 1, 0);
                if (nbytes <= 0) {
                    if (nbytes == 0) {
                        // connection closed
                        fprintf(stderr, "fd %d disconnected\n", i);
                    } else {
                        fprintf(stderr, "recv from fd %d error: %s\n", i, strerror(errno));
                    }
                    close(i);
                    FD_CLR(i, &master);
                } else {
                    recvbuf[nbytes] = '\0'; // treating as text

                    // Obtain sender addr for prefix
                    struct sockaddr_storage ss;
                    socklen_t slen = sizeof(ss);
                    char host[NI_MAXHOST], serv[NI_MAXSERV];
                    if (getpeername(i, (struct sockaddr *)&ss, &slen) == 0) {
                        sockaddr_to_str((struct sockaddr *)&ss, host, sizeof(host), serv, sizeof(serv));
                    } else {
                        snprintf(host, sizeof(host), "fd-%d", i);
                        snprintf(serv, sizeof(serv), "0");
                    }

                    // Prepare outgoing message with prefix
                    int hdr = snprintf(outbuf, sizeof(outbuf), "[%s:%s] ", host, serv);
                    size_t payload_len = (size_t)nbytes;
                    size_t total_len = 0;

                    if ((size_t)hdr + payload_len < sizeof(outbuf)) {
                        memcpy(outbuf + hdr, recvbuf, payload_len);
                        total_len = (size_t)hdr + payload_len;
                    } else {
                        // Truncate if needed
                        size_t copy_len = sizeof(outbuf) - (size_t)hdr - 1;
                        memcpy(outbuf + hdr, recvbuf, copy_len);
                        outbuf[sizeof(outbuf) - 1] = '\0';
                        total_len = sizeof(outbuf) - 1;
                    }

                    // Echo to server console
                    fwrite(outbuf, 1, total_len, stdout);
                    if (total_len == 0 || outbuf[total_len - 1] != '\n') fputc('\n', stdout);
                    fflush(stdout);

                    // Broadcast to other clients
                    broadcast(&master, fdmax, i, g_listener_fd, outbuf, total_len);
                }
            }
        }
    }

    return 0;
}