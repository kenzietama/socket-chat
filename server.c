#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<netdb.h>

#define BUFSIZE 1024
#define MAX_CLIENTS 10 // Maximum number of clients to store addresses

// Structure to store client information
struct client_info {
    int sockfd;
    struct sockaddr_in addr;
};

void send_to_all(int j, int i, int sockfd, int nbytes_recvd, char *send_buf, fd_set *master)
{
    if (FD_ISSET(j, master)) 
    {
        if (j != sockfd && j != i)
        {
            if (send(j, send_buf, strlen(send_buf), 0) == -1)
            {
                perror("send");
            }
        }
    }
}

void send_recv(int i, fd_set *master, int sockfd, int fdmax, struct client_info *clients, int client_count)
{
    int nbytes_recvd, j;
    char recv_buf[BUFSIZE], send_buf[BUFSIZE];
    char client_id[64];

    if ((nbytes_recvd = recv(i, recv_buf, BUFSIZE-1, 0)) <= 0)
    {
        if (nbytes_recvd == 0)
        {
            // Find client's IP and port for disconnection message
            for (j = 0; j < client_count; j++) {
                if (clients[j].sockfd == i) {
                    printf("socket %d (%s:%d) hung up\n", 
                           i, inet_ntoa(clients[j].addr.sin_addr), ntohs(clients[j].addr.sin_port));
                    break;
                }
            }
        }
        else
        {
            perror("recv");
        }
        close(i);
        FD_CLR(i, master);
    }
    else
    {
        recv_buf[nbytes_recvd] = '\0'; // Null-terminate
        // Clean up \r\n from telnet
        for (j = 0; j < nbytes_recvd; j++) {
            if (recv_buf[j] == '\r' || recv_buf[j] == '\n') {
                recv_buf[j] = '\0';
                nbytes_recvd = j;
                break;
            }
        }
        if (nbytes_recvd > 0) { // Only send non-empty messages
            // Find client's IP and port
            for (j = 0; j < client_count; j++) {
                if (clients[j].sockfd == i) {
                    snprintf(client_id, sizeof(client_id), "%s:%d: ", 
                             inet_ntoa(clients[j].addr.sin_addr), 
                             ntohs(clients[j].addr.sin_port));
                    break;
                }
            }
            // Format message as "<IP>:<port>: <message>\r\n"
            snprintf(send_buf, BUFSIZE, "%s%s\r\n", client_id, recv_buf);
            for (j = 0; j <= fdmax; j++) 
            {
                send_to_all(j, i, sockfd, strlen(send_buf), send_buf, master);
            }
        }
    }
}

void connection_accept(fd_set *master, int *fdmax, int sockfd, struct sockaddr_in *client_addr, struct client_info *clients, int *client_count)
{
    socklen_t addrlen;
    int newsockfd;
    char welcome_msg[] = "Welcome to the chat server! Type your message and press Enter.\r\n";

    addrlen = sizeof(struct sockaddr_in);
    if ((newsockfd = accept(sockfd, (struct sockaddr *)client_addr, &addrlen)) == -1)
    {
        perror("accept");
        exit(1);
    }
    else
    {
        FD_SET(newsockfd, master);
        if (newsockfd > *fdmax)
        {
            *fdmax = newsockfd;
        }
        // Store client info
        if (*client_count < MAX_CLIENTS) {
            clients[*client_count].sockfd = newsockfd;
            clients[*client_count].addr = *client_addr;
            (*client_count)++;
        }
        // Send welcome message
        if (send(newsockfd, welcome_msg, strlen(welcome_msg), 0) == -1) {
            perror("send");
        }
        // Print socket descriptor, IP, and port
        printf("new connection from %s on port %d (socket descriptor: %d)\n", 
               inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), newsockfd);
    }
}

void connect_request(int *sockfd, struct sockaddr_in *my_addr)
{
    int yes = 1;

    if ((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    my_addr->sin_family = AF_INET;
    my_addr->sin_port = htons(33333);
    my_addr->sin_addr.s_addr = INADDR_ANY;
    memset(my_addr->sin_zero, '\0', sizeof(my_addr->sin_zero));

    if (setsockopt(*sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("setsockopt");
        exit(1);
    }

    if (bind(*sockfd, (struct sockaddr *)my_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    if (listen(*sockfd, 10) == -1)
    {
        perror("listen");
        exit(1);
    }

    printf("\nTCP server is waiting\n");
    fflush(stdout);
}

int main()
{
    fd_set master;
    fd_set read_fds;
    int fdmax, i;
    int sockfd = 0;
    struct sockaddr_in my_addr, client_addr;
    struct client_info clients[MAX_CLIENTS];
    int client_count = 0;

    FD_ZERO(&master);
    FD_ZERO(&read_fds);
    connect_request(&sockfd, &my_addr);
    FD_SET(sockfd, &master);

    fdmax = sockfd;
    while (1)
    {
        read_fds = master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(4);
        }

        for (i = 0; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == sockfd)
                    connection_accept(&master, &fdmax, sockfd, &client_addr, clients, &client_count);
                else
                    send_recv(i, &master, sockfd, fdmax, clients, client_count);
            }
        }
    }
    return 0;
}
