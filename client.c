/*
 Simple chat client using select() for I/O multiplexing.
 Monitors both stdin and the socket to send and receive messages concurrently.

 Build:
   cc -Wall -Wextra -O2 -o chat_client client.c

 Run:
   ./chat_client <host> <port>

 Usage:
   Type messages then press Enter to send.
   Type /quit and press Enter or press Ctrl-D to exit.
*/

#define _POSIX_C_SOURCE 200112L

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define LINE_BUF 4096
#define RECV_BUF 4096

static int g_sockfd = -1;

static void die(const char *msg) {
    perror(msg);
    if (g_sockfd >= 0) close(g_sockfd);
    exit(EXIT_FAILURE);
}

static void on_sigint(int signo) {
    (void)signo;
    if (g_sockfd >= 0) {
        const char *bye = "/client-disconnect\n";
        send(g_sockfd, bye, strlen(bye), 0);
        close(g_sockfd);
    }
    fprintf(stderr, "\nClient exiting.\n");
    exit(0);
}

static int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;   // both IPv4/IPv6
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return -1;
    }

    int sfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sfd < 0) continue;

        if (connect(sfd, rp->ai_addr, rp->ai_len) == 0) {
            // Connected
            char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
            if (getnameinfo(rp->ai_addr, rp->ai_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                fprintf(stderr, "Connected to %s:%s\n", hbuf, sbuf);
            }
            break;
        }

        close(sfd);
        sfd = -1;
    }

    freeaddrinfo(res);
    return sfd;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    g_sockfd = connect_to_server(argv[1], argv[2]);
    if (g_sockfd < 0) die("connect_to_server");

    // Unbuffer stdout to interleave messages promptly
    setvbuf(stdout, NULL, _IONBF, 0);

    fd_set master, read_fds;
    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    FD_SET(STDIN_FILENO, &master);
    FD_SET(g_sockfd, &master);

    int fdmax = (g_sockfd > STDIN_FILENO) ? g_sockfd : STDIN_FILENO;

    char line[LINE_BUF];
    char recvbuf[RECV_BUF];

    fprintf(stderr, "Type messages and press Enter to send. Type /quit to exit.\n");

    while (1) {
        read_fds = master;
        int ready = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            die("select");
        }

        // Socket readable
        if (FD_ISSET(g_sockfd, &read_fds)) {
            ssize_t n = recv(g_sockfd, recvbuf, sizeof(recvbuf) - 1, 0);
            if (n <= 0) {
                if (n == 0) {
                    fprintf(stderr, "Server closed the connection.\n");
                } else {
                    fprintf(stderr, "recv error: %s\n", strerror(errno));
                }
                break;
            }
            recvbuf[n] = '\0';
            fputs(recvbuf, stdout);
        }

        // Stdin readable
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (!fgets(line, sizeof(line), stdin)) {
                // EOF (Ctrl-D)
                fprintf(stderr, "EOF on stdin. Exiting.\n");
                break;
            }
            if (strcmp(line, "/quit\n") == 0 || strcmp(line, "/quit\r\n") == 0) {
                fprintf(stderr, "Quitting.\n");
                break;
            }

            size_t len = strlen(line);
            ssize_t sent = send(g_sockfd, line, len, 0);
            if (sent < 0) {
                fprintf(stderr, "send error: %s\n", strerror(errno));
                break;
            }
        }
    }

    if (g_sockfd >= 0) close(g_sockfd);
    return 0;
}