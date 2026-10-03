#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-1562"
#define SID "2651"
#define BUFFER_SIZE 1024

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    /* Create TCP socket */
    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    /* Allow the port to be reused after restarting the Agent */
    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent - IT24101562\n");
    printf("TCP socket created successfully.\n");

    /* Configure server address */
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /* Bind socket */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Listen for Controller connections */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Agent listening on TCP port %d...\n", PORT);
    printf("Waiting for a Controller connection...\n");

    /* Accept Controller */
    client_fd = accept(server_fd,
                       (struct sockaddr *)&client_addr,
                       &client_len);

    if (client_fd < 0)
    {
        perror("accept");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Controller connected from %s\n",
           inet_ntoa(client_addr.sin_addr));

    /* Receive AUTH command */
    memset(buffer, 0, sizeof(buffer));

    bytes_received = recv(client_fd,
                          buffer,
                          sizeof(buffer) - 1,
                          0);

    if (bytes_received <= 0)
    {
        printf("Controller disconnected before authentication.\n");
        close(client_fd);
        close(server_fd);
        return 0;
    }

    buffer[bytes_received] = '\0';

    /* Remove newline sent by Controller */
    buffer[strcspn(buffer, "\r\n")] = '\0';

    printf("Received command: %s\n", buffer);

    /* Check authentication */
    if (strcmp(buffer, "AUTH " AUTH_TOKEN) == 0)
    {
        const char *response =
            "OK AUTHENTICATED SID:" SID "\n";

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication successful.\n");
    }
    else
    {
        const char *response =
            "ERR 001 AUTH_FAILED SID:" SID "\n";

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication failed.\n");
    }

    close(client_fd);
    close(server_fd);

    return 0;
}
