#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define BUFFER_SIZE 1024

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

    char token[100];
    char command[150];
    char buffer[BUFFER_SIZE];

    ssize_t bytes_received;

    /* Create TCP socket */
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Controller - IT24101562\n");

    /* Configure Agent address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET,
                  SERVER_IP,
                  &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connecting to Agent at %s:%d...\n",
           SERVER_IP, PORT);

    /* Connect to Agent */
    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to RemoteOps Agent.\n");

    /* Ask user for authentication token */
    printf("Enter authentication token: ");

    if (scanf("%99s", token) != 1)
    {
        printf("Failed to read authentication token.\n");
        close(sock_fd);
        return 1;
    }

    /* Create AUTH command */
    snprintf(command,
             sizeof(command),
             "AUTH %s\n",
             token);

    /* Send AUTH command */
    if (send(sock_fd,
             command,
             strlen(command),
             0) < 0)
    {
        perror("send");
        close(sock_fd);
        return 1;
    }

    /* Receive Agent response */
    memset(buffer, 0, sizeof(buffer));

    bytes_received = recv(sock_fd,
                          buffer,
                          sizeof(buffer) - 1,
                          0);

    if (bytes_received <= 0)
    {
        printf("Agent closed the connection.\n");
        close(sock_fd);
        return 1;
    }

    buffer[bytes_received] = '\0';

    printf("Agent response: %s", buffer);

    close(sock_fd);

    return 0;
}
