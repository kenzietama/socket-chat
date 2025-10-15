#include<stdio.h>
#include<string.h>
#include<stdlib.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>

#define BUFSIZE 1024
#define NAMESIZE 32

void send_recv(int i, int sockfd, char *username)
{
	char send_buf[BUFSIZE];
	char recv_buf[BUFSIZE];
	char message[BUFSIZE];
	int nbyte_recvd;

	if(i == 0)
	{
		fgets(send_buf, BUFSIZE, stdin);
		if(strcmp(send_buf, "quit\n") == 0)
		{
			exit(0);
		}
		else
		{
			// Format: "username: message"
			snprintf(message, BUFSIZE, "%s: %s", username, send_buf);
			send(sockfd, message, strlen(message), 0);
		}
	}
	else
	{
		nbyte_recvd = recv(sockfd, recv_buf, BUFSIZE, 0);
		recv_buf[nbyte_recvd] = '\0';
		printf("%s", recv_buf);
		fflush(stdout);
	}
}

void connect_req(int *sockfd, struct sockaddr_in *server_addr)
{
	if((*sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		perror("socket");
		exit(1);
	}
	
	server_addr->sin_family = AF_INET;
	server_addr->sin_port = htons(33333);  // Fixed to match server
	server_addr->sin_addr.s_addr = inet_addr("127.0.0.1");
	memset(server_addr->sin_zero, '\0', sizeof server_addr->sin_zero);

	if(connect(*sockfd, (struct sockaddr *)server_addr, sizeof(struct sockaddr)) == -1)
	{
		perror("connect");
		exit(1);
	}
}	

int main()
{
	int sockfd, fdmax, i;
	struct sockaddr_in server_addr;
	fd_set master;
	fd_set read_fds;
	char username[NAMESIZE];

	// Get username from user
	printf("Enter your username: ");
	fgets(username, NAMESIZE, stdin);
	username[strcspn(username, "\n")] = '\0';  // Remove newline

	connect_req(&sockfd, &server_addr);
	
	printf("Connected to chat server. Type 'quit' to exit.\n");
	
	FD_ZERO(&master);
	FD_ZERO(&read_fds);
	FD_SET(0, &master);
	FD_SET(sockfd, &master);

	fdmax = sockfd;

	while(1)
	{
		read_fds = master;
		if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
		{
			perror("select");
			exit(4);
		}
	
		for(i=0; i<=fdmax; i++)
			if(FD_ISSET(i, &read_fds))
				send_recv(i, sockfd, username);
	}

	printf("client-quieted\n");
	close(sockfd);
	return 0;
}
