#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define BUFFER_SIZE 4096
#define SID "2651"

/* =========================================================
   TCP HELPER FUNCTIONS
   ========================================================= */

int send_all(int socket_fd,
             const void *data,
             size_t length)
{
    const char *buffer =
        (const char *)data;

    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent =
            send(socket_fd,
                 buffer + total_sent,
                 length - total_sent,
                 0);

        if (sent <= 0)
        {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 0;
}

ssize_t recv_line(int socket_fd,
                  char *buffer,
                  size_t buffer_size)
{
    size_t position = 0;

    if (buffer_size == 0)
    {
        return -1;
    }

    while (position < buffer_size - 1)
    {
        char ch;

        ssize_t received =
            recv(socket_fd,
                 &ch,
                 1,
                 0);

        if (received == 0)
        {
            if (position == 0)
            {
                return 0;
            }

            break;
        }

        if (received < 0)
        {
            return -1;
        }

        if (ch == '\n')
        {
            break;
        }

        if (ch != '\r')
        {
            buffer[position++] = ch;
        }
    }

    buffer[position] = '\0';

    return (ssize_t)position;
}

/* Receive exactly file_size bytes and save to a file */
int recv_exact_to_file(int socket_fd,
                       FILE *file,
                       unsigned long long file_size)
{
    char buffer[BUFFER_SIZE];
    unsigned long long remaining = file_size;

    while (remaining > 0)
    {
        size_t amount_to_receive;

        if (remaining > sizeof(buffer))
        {
            amount_to_receive = sizeof(buffer);
        }
        else
        {
            amount_to_receive = (size_t)remaining;
        }

        ssize_t received =
            recv(socket_fd,
                 buffer,
                 amount_to_receive,
                 0);

        if (received <= 0)
        {
            return -1;
        }

        size_t written =
            fwrite(buffer,
                   1,
                   (size_t)received,
                   file);

        if (written != (size_t)received)
        {
            return -1;
        }

        remaining -=
            (unsigned long long)received;
    }

    return 0;
}

/* Send exact local file bytes for PUT */
int send_file_bytes(int socket_fd,
                    FILE *file,
                    unsigned long long file_size)
{
    char buffer[BUFFER_SIZE];
    unsigned long long remaining = file_size;

    while (remaining > 0)
    {
        size_t amount_to_read;

        if (remaining > sizeof(buffer))
        {
            amount_to_read = sizeof(buffer);
        }
        else
        {
            amount_to_read = (size_t)remaining;
        }

        size_t bytes_read =
            fread(buffer,
                  1,
                  amount_to_read,
                  file);

        if (bytes_read == 0)
        {
            return -1;
        }

        if (send_all(socket_fd,
                     buffer,
                     bytes_read) < 0)
        {
            return -1;
        }

        remaining -=
            (unsigned long long)bytes_read;
    }

    return 0;
}

/* =========================================================
   MULTI-LINE RESPONSES
   ========================================================= */

int receive_until_marker(int socket_fd,
                         const char *success_marker)
{
    char line[BUFFER_SIZE];

    while (1)
    {
        ssize_t length =
            recv_line(socket_fd,
                      line,
                      sizeof(line));

        if (length <= 0)
        {
            printf("Agent closed the connection.\n");
            return -1;
        }

        printf("%s\n", line);

        if (strstr(line,
                   success_marker) != NULL)
        {
            return 0;
        }

        if (strncmp(line,
                    "ERR ",
                    4) == 0)
        {
            return -1;
        }
    }
}

/* =========================================================
   PUT
   ========================================================= */

void handle_put(int socket_fd,
                const char *command)
{
    char filename[256];
    char protocol_command[1024];
    char response[BUFFER_SIZE];

    if (sscanf(command,
               "PUT %255s",
               filename) != 1)
    {
        printf("Usage: PUT <filename>\n");
        return;
    }

    FILE *file =
        fopen(filename, "rb");

    if (file == NULL)
    {
        printf("Local file not found: %s\n",
               filename);
        return;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        printf("Unable to determine file size.\n");
        fclose(file);
        return;
    }

    long size = ftell(file);

    if (size < 0)
    {
        printf("Unable to determine file size.\n");
        fclose(file);
        return;
    }

    rewind(file);

    unsigned long long file_size =
        (unsigned long long)size;

    snprintf(protocol_command,
             sizeof(protocol_command),
             "PUT %s %llu\n",
             filename,
             file_size);

    if (send_all(socket_fd,
                 protocol_command,
                 strlen(protocol_command)) < 0)
    {
        printf("Failed to send PUT command.\n");
        fclose(file);
        return;
    }

    /* Wait until Agent is ready */
    ssize_t length =
        recv_line(socket_fd,
                  response,
                  sizeof(response));

    if (length <= 0)
    {
        printf("Agent closed the connection.\n");
        fclose(file);
        return;
    }

    printf("%s\n", response);

    if (strcmp(response,
               "OK READY SID:" SID) != 0)
    {
        printf("Agent did not accept the upload.\n");
        fclose(file);
        return;
    }

    if (send_file_bytes(socket_fd,
                        file,
                        file_size) < 0)
    {
        printf("File upload failed.\n");
        fclose(file);
        return;
    }

    fclose(file);

    /* Receive final PUT response */
    length =
        recv_line(socket_fd,
                  response,
                  sizeof(response));

    if (length <= 0)
    {
        printf("Agent closed the connection.\n");
        return;
    }

    printf("%s\n", response);

    if (strcmp(response,
               "OK PUT SID:" SID) == 0)
    {
        printf("Upload completed: %s (%llu bytes)\n",
               filename,
               file_size);
    }
}

/* =========================================================
   GET
   ========================================================= */

void handle_get(int socket_fd,
                const char *command)
{
    char filename[256];
    char protocol_command[512];
    char response[BUFFER_SIZE];

    if (sscanf(command,
               "GET %255s",
               filename) != 1)
    {
        printf("Usage: GET <filename>\n");
        return;
    }

    /*
     * Send GET request to Agent.
     */
    snprintf(protocol_command,
             sizeof(protocol_command),
             "GET %s\n",
             filename);

    if (send_all(socket_fd,
                 protocol_command,
                 strlen(protocol_command)) < 0)
    {
        printf("Failed to send GET command.\n");
        return;
    }

    /*
     * Expected response:
     * OK GET <size> SID:2651
     */
    ssize_t length =
        recv_line(socket_fd,
                  response,
                  sizeof(response));

    if (length <= 0)
    {
        printf("Agent closed the connection.\n");
        return;
    }

    printf("%s\n", response);

    /* Handle Agent error */
    if (strncmp(response,
                "ERR ",
                4) == 0)
    {
        return;
    }

    unsigned long long file_size;
    char received_sid[100];

    int parsed =
        sscanf(response,
               "OK GET %llu SID:%99s",
               &file_size,
               received_sid);

    if (parsed != 2 ||
        strcmp(received_sid, SID) != 0)
    {
        printf("Invalid GET response from Agent.\n");
        return;
    }

    /*
     * Save downloads with a different local name
     * so we can compare them with the original.
     *
     * Example:
     * test.txt -> downloaded_test.txt
     */
    char download_name[512];

    snprintf(download_name,
             sizeof(download_name),
             "downloaded_%s",
             filename);

    FILE *file =
        fopen(download_name, "wb");

    if (file == NULL)
    {
        printf("Unable to create local download file.\n");
        return;
    }

    /*
     * Receive exactly the announced number of bytes.
     */
    if (recv_exact_to_file(socket_fd,
                           file,
                           file_size) < 0)
    {
        fclose(file);
        remove(download_name);

        printf("File download failed.\n");
        return;
    }

    fclose(file);

    /*
     * After exact file bytes, Agent sends:
     * OK GET_COMPLETE SID:2651
     */
    length =
        recv_line(socket_fd,
                  response,
                  sizeof(response));

    if (length <= 0)
    {
        printf("Agent closed the connection.\n");
        return;
    }

    printf("%s\n", response);

    if (strcmp(response,
               "OK GET_COMPLETE SID:" SID) == 0)
    {
        printf("Download completed: %s (%llu bytes)\n",
               download_name,
               file_size);
    }
    else
    {
        printf("Unexpected GET completion response.\n");
    }
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    int sock_fd;

    struct sockaddr_in server_addr;

    char token[100];
    char command[512];
    char buffer[BUFFER_SIZE];

    /* Create TCP socket */
    sock_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (sock_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Controller - IT24101562\n");

    memset(&server_addr,
           0,
           sizeof(server_addr));

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
           SERVER_IP,
           PORT);

    if (connect(sock_fd,
                (struct sockaddr *)&server_addr,
                sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to RemoteOps Agent.\n");

    /* =====================================================
       AUTHENTICATION
       ===================================================== */

    printf("Enter authentication token: ");

    if (scanf("%99s", token) != 1)
    {
        close(sock_fd);
        return 1;
    }

    snprintf(buffer,
             sizeof(buffer),
             "AUTH %s\n",
             token);

    if (send_all(sock_fd,
                 buffer,
                 strlen(buffer)) < 0)
    {
        printf("Failed to send authentication.\n");
        close(sock_fd);
        return 1;
    }

    ssize_t length =
        recv_line(sock_fd,
                  buffer,
                  sizeof(buffer));

    if (length <= 0)
    {
        printf("Agent closed the connection.\n");
        close(sock_fd);
        return 1;
    }

    printf("Agent response: %s\n",
           buffer);

    if (strcmp(buffer,
               "OK AUTHENTICATED SID:" SID) != 0)
    {
        printf("Authentication failed.\n");
        close(sock_fd);
        return 1;
    }

    /* Remove newline left by scanf */
    int ch;

    while ((ch = getchar()) != '\n' &&
           ch != EOF)
    {
        /* discard */
    }

    /* =====================================================
       COMMAND LOOP
       ===================================================== */

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
        printf("  PUT <filename>\n");
        printf("  GET <filename>\n");
        printf("  QUIT\n");

        printf("\nEnter command: ");

        if (fgets(command,
                  sizeof(command),
                  stdin) == NULL)
        {
            break;
        }

        command[strcspn(command,
                       "\r\n")] = '\0';

        if (strlen(command) == 0)
        {
            continue;
        }

        /* ---------------- PUT ---------------- */

        if (strncmp(command,
                    "PUT ",
                    4) == 0)
        {
            handle_put(sock_fd,
                       command);

            continue;
        }

        /* ---------------- GET ---------------- */

        if (strncmp(command,
                    "GET ",
                    4) == 0)
        {
            handle_get(sock_fd,
                       command);

            continue;
        }

        /* Send normal text command */
        snprintf(buffer,
                 sizeof(buffer),
                 "%s\n",
                 command);

        if (send_all(sock_fd,
                     buffer,
                     strlen(buffer)) < 0)
        {
            printf("Failed to send command.\n");
            break;
        }

        /* ---------------- LISTPROC ---------------- */

        if (strcmp(command,
                   "LISTPROC") == 0)
        {
            receive_until_marker(
                sock_fd,
                "OK LISTPROC SID:" SID);
        }

        /* ---------------- EXEC ---------------- */

        else if (strncmp(command,
                         "EXEC ",
                         5) == 0)
        {
            receive_until_marker(
                sock_fd,
                "OK EXEC SID:" SID);
        }

        /* ---------------- SYSINFO ---------------- */

        else if (strcmp(command,
                        "SYSINFO") == 0)
        {
            receive_until_marker(
                sock_fd,
                "OK SYSINFO SID:" SID);
        }

        /* ---------------- OTHER ---------------- */

        else
        {
            length =
                recv_line(sock_fd,
                          buffer,
                          sizeof(buffer));

            if (length <= 0)
            {
                printf("Agent closed the connection.\n");
                break;
            }

            printf("Agent response: %s\n",
                   buffer);
        }

        if (strcmp(command,
                   "QUIT") == 0)
        {
            break;
        }
    }

    close(sock_fd);

    return 0;
}
