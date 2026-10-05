#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define BUFFER_SIZE 4096

int main(void)
{
    int sock_fd;
    struct sockaddr_in server_addr;

    char token[100];
    char command[200];
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

    /* ===================================== */
    /* AUTHENTICATION                        */
    /* ===================================== */

    printf("Enter authentication token: ");

    if (scanf("%99s", token) != 1)
    {
        printf("Failed to read authentication token.\n");
        close(sock_fd);
        return 1;
    }

    snprintf(command,
             sizeof(command),
             "AUTH %s\n",
             token);

    if (send(sock_fd,
             command,
             strlen(command),
             0) < 0)
    {
        perror("send");
        close(sock_fd);
        return 1;
    }

    memset(buffer, 0, sizeof(buffer));

    bytes_received =
        recv(sock_fd,
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

    if (strstr(buffer, "OK AUTHENTICATED") == NULL)
    {
        printf("Authentication failed. Closing connection.\n");
        close(sock_fd);
        return 1;
    }

    /*
     * Remove the newline left by scanf so that
     * fgets() can be used for commands containing spaces.
     */
    int ch;

    while ((ch = getchar()) != '\n' && ch != EOF)
    {
        /* discard remaining input */
    }

    /* ===================================== */
    /* COMMAND LOOP                          */
    /* ===================================== */

    while (1)
    {
        printf("\nAvailable commands:\n");
        printf("  SYSINFO\n");
        printf("  LISTPROC\n");
        printf("  EXEC DATE\n");
        printf("  EXEC UPTIME\n");
        printf("  EXEC DISKFREE\n");
        printf("  EXEC HOSTNAME\n");
        printf("  EXEC WHOAMI\n");
        printf("  QUIT\n");

        printf("\nEnter command: ");

        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL)
        {
            break;
        }

        /* Remove newline from keyboard input */
        command[strcspn(command, "\r\n")] = '\0';

        if (strlen(command) == 0)
        {
            continue;
        }

        /* Add protocol newline */
        char send_buffer[250];

        snprintf(send_buffer,
                 sizeof(send_buffer),
                 "%s\n",
                 command);

        if (send(sock_fd,
                 send_buffer,
                 strlen(send_buffer),
                 0) < 0)
        {
            perror("send");
            break;
        }

        /*
         * LISTPROC and EXEC may return multiple
         * TCP receive blocks.
         */
        if (strcmp(command, "LISTPROC") == 0 ||
            strncmp(command, "EXEC ", 5) == 0)
        {
            while (1)
            {
                memset(buffer, 0, sizeof(buffer));

                bytes_received =
                    recv(sock_fd,
                         buffer,
                         sizeof(buffer) - 1,
                         0);

                if (bytes_received <= 0)
                {
                    printf("Agent closed the connection.\n");
                    break;
                }

                buffer[bytes_received] = '\0';

                printf("%s", buffer);

                /*
                 * Successful LISTPROC / EXEC or
                 * an ERR response finishes command.
                 */
                if (strstr(buffer,
                           "OK LISTPROC SID:2651") != NULL ||
                    strstr(buffer,
                           "OK EXEC SID:2651") != NULL ||
                    strstr(buffer,
                           "ERR ") != NULL)
                {
                    break;
                }
            }
        }
        else
        {
            memset(buffer, 0, sizeof(buffer));

            bytes_received =
                recv(sock_fd,
                     buffer,
                     sizeof(buffer) - 1,
                     0);

            if (bytes_received <= 0)
            {
                printf("Agent closed the connection.\n");
                break;
            }

            buffer[bytes_received] = '\0';

            printf("\nAgent response:\n%s", buffer);
        }

        if (strcmp(command, "QUIT") == 0)
        {
            break;
        }
    }

    close(sock_fd);

    return 0;
}
