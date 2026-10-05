#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#define SERVER_IP "127.0.0.1"
#define PORT 9410
#define UDP_PORT 9500
#define BUFFER_SIZE 4096
#define SID "2651"

int udp_socket_fd = -1;
int monitoring_active = 0;

/* =========================================================
   TCP HELPERS
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

        total_sent +=
            (size_t)sent;
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

    while (position <
           buffer_size - 1)
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
            buffer[position++] =
                ch;
        }
    }

    buffer[position] = '\0';

    return (ssize_t)position;
}

/* =========================================================
   FILE HELPERS
   ========================================================= */

int recv_exact_to_file(
    int socket_fd,
    FILE *file,
    unsigned long long file_size)
{
    char buffer[BUFFER_SIZE];

    unsigned long long remaining =
        file_size;

    while (remaining > 0)
    {
        size_t amount =
            remaining >
                    sizeof(buffer)
                ? sizeof(buffer)
                : (size_t)remaining;

        ssize_t received =
            recv(socket_fd,
                 buffer,
                 amount,
                 0);

        if (received <= 0)
        {
            return -1;
        }

        if (fwrite(buffer,
                   1,
                   (size_t)received,
                   file) !=
            (size_t)received)
        {
            return -1;
        }

        remaining -=
            (unsigned long long)
                received;
    }

    return 0;
}

int send_file_bytes(
    int socket_fd,
    FILE *file,
    unsigned long long file_size)
{
    char buffer[BUFFER_SIZE];

    unsigned long long remaining =
        file_size;

    while (remaining > 0)
    {
        size_t amount =
            remaining >
                    sizeof(buffer)
                ? sizeof(buffer)
                : (size_t)remaining;

        size_t bytes_read =
            fread(buffer,
                  1,
                  amount,
                  file);

        if (bytes_read == 0)
        {
            return -1;
        }

        if (send_all(
                socket_fd,
                buffer,
                bytes_read) < 0)
        {
            return -1;
        }

        remaining -=
            (unsigned long long)
                bytes_read;
    }

    return 0;
}

/* =========================================================
   MULTI-LINE RESPONSE
   ========================================================= */

int receive_until_marker(
    int socket_fd,
    const char *success_marker)
{
    char line[BUFFER_SIZE];

    while (1)
    {
        ssize_t length =
            recv_line(
                socket_fd,
                line,
                sizeof(line));

        if (length <= 0)
        {
            printf(
                "Agent closed the connection.\n");

            return -1;
        }

        printf("%s\n", line);

        if (strstr(
                line,
                success_marker) != NULL)
        {
            return 0;
        }

        if (strncmp(
                line,
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
    char request[1024];
    char response[BUFFER_SIZE];

    if (sscanf(
            command,
            "PUT %255s",
            filename) != 1)
    {
        printf(
            "Usage: PUT <filename>\n");

        return;
    }

    FILE *file =
        fopen(filename, "rb");

    if (file == NULL)
    {
        printf(
            "Local file not found: %s\n",
            filename);

        return;
    }

    fseek(file, 0, SEEK_END);

    long size =
        ftell(file);

    if (size < 0)
    {
        printf(
            "Unable to determine file size.\n");

        fclose(file);

        return;
    }

    rewind(file);

    unsigned long long file_size =
        (unsigned long long)size;

    snprintf(
        request,
        sizeof(request),
        "PUT %s %llu\n",
        filename,
        file_size);

    if (send_all(
            socket_fd,
            request,
            strlen(request)) < 0)
    {
        fclose(file);

        return;
    }

    ssize_t length =
        recv_line(
            socket_fd,
            response,
            sizeof(response));

    if (length <= 0)
    {
        fclose(file);

        return;
    }

    printf("%s\n",
           response);

    if (strcmp(
            response,
            "OK READY SID:"
            SID) != 0)
    {
        fclose(file);

        return;
    }

    if (send_file_bytes(
            socket_fd,
            file,
            file_size) < 0)
    {
        printf(
            "File upload failed.\n");

        fclose(file);

        return;
    }

    fclose(file);

    length =
        recv_line(
            socket_fd,
            response,
            sizeof(response));

    if (length <= 0)
    {
        return;
    }

    printf("%s\n",
           response);

    if (strcmp(
            response,
            "OK PUT SID:"
            SID) == 0)
    {
        printf(
            "Upload completed: %s (%llu bytes)\n",
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
    char request[512];
    char response[BUFFER_SIZE];

    if (sscanf(
            command,
            "GET %255s",
            filename) != 1)
    {
        printf(
            "Usage: GET <filename>\n");

        return;
    }

    snprintf(
        request,
        sizeof(request),
        "GET %s\n",
        filename);

    if (send_all(
            socket_fd,
            request,
            strlen(request)) < 0)
    {
        return;
    }

    ssize_t length =
        recv_line(
            socket_fd,
            response,
            sizeof(response));

    if (length <= 0)
    {
        return;
    }

    printf("%s\n",
           response);

    if (strncmp(
            response,
            "ERR ",
            4) == 0)
    {
        return;
    }

    unsigned long long file_size;
    char received_sid[100];

    int parsed =
        sscanf(
            response,
            "OK GET %llu SID:%99s",
            &file_size,
            received_sid);

    if (parsed != 2 ||
        strcmp(
            received_sid,
            SID) != 0)
    {
        printf(
            "Invalid GET response.\n");

        return;
    }

    char download_name[512];

    snprintf(
        download_name,
        sizeof(download_name),
        "downloaded_%s",
        filename);

    FILE *file =
        fopen(
            download_name,
            "wb");

    if (file == NULL)
    {
        printf(
            "Unable to create file.\n");

        return;
    }

    if (recv_exact_to_file(
            socket_fd,
            file,
            file_size) < 0)
    {
        fclose(file);

        remove(download_name);

        printf(
            "File download failed.\n");

        return;
    }

    fclose(file);

    length =
        recv_line(
            socket_fd,
            response,
            sizeof(response));

    if (length <= 0)
    {
        return;
    }

    printf("%s\n",
           response);

    if (strcmp(
            response,
            "OK GET_COMPLETE SID:"
            SID) == 0)
    {
        printf(
            "Download completed: %s (%llu bytes)\n",
            download_name,
            file_size);
    }
}

/* =========================================================
   UDP SOCKET
   ========================================================= */

int create_udp_socket(void)
{
    int socket_fd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            0);

    if (socket_fd < 0)
    {
        perror("UDP socket");

        return -1;
    }

    int opt = 1;

    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt));

    struct sockaddr_in address;

    memset(
        &address,
        0,
        sizeof(address));

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        INADDR_ANY;

    address.sin_port =
        htons(UDP_PORT);

    if (bind(
            socket_fd,
            (struct sockaddr *)
                &address,
            sizeof(address)) < 0)
    {
        perror("UDP bind");

        close(socket_fd);

        return -1;
    }

    struct timeval timeout;

    timeout.tv_sec = 5;
    timeout.tv_usec = 0;

    setsockopt(
        socket_fd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout));

    return socket_fd;
}

/* =========================================================
   RECEIVE 5 UDP UPDATES
   ========================================================= */

void receive_monitor_updates(void)
{
    char buffer[BUFFER_SIZE];

    printf(
        "\nReceiving UDP monitoring updates...\n\n");

    for (int i = 0;
         i < 5;
         i++)
    {
        struct sockaddr_in sender;

        socklen_t sender_length =
            sizeof(sender);

        ssize_t received =
            recvfrom(
                udp_socket_fd,
                buffer,
                sizeof(buffer) - 1,
                0,
                (struct sockaddr *)
                    &sender,
                &sender_length);

        if (received < 0)
        {
            printf(
                "UDP receive timeout.\n");

            break;
        }

        buffer[received] = '\0';

        printf(
            "[UDP MONITOR] %s\n",
            buffer);
    }

    printf(
        "\nReturned to command menu.\n");

    printf(
        "Monitoring is still active.\n");

    printf(
        "Use MONITOR STOP to stop monitoring.\n");
}

/* =========================================================
   MONITOR START
   ========================================================= */

void handle_monitor_start(
    int socket_fd)
{
    char request[128];
    char response[BUFFER_SIZE];

    if (monitoring_active)
    {
        printf(
            "Monitoring is already active.\n");

        return;
    }

    udp_socket_fd =
        create_udp_socket();

    if (udp_socket_fd < 0)
    {
        return;
    }

    snprintf(
        request,
        sizeof(request),
        "MONITOR START %d\n",
        UDP_PORT);

    if (send_all(
            socket_fd,
            request,
            strlen(request)) < 0)
    {
        close(udp_socket_fd);

        udp_socket_fd = -1;

        return;
    }

    ssize_t length =
        recv_line(
            socket_fd,
            response,
            sizeof(response));

    if (length <= 0)
    {
        close(udp_socket_fd);

        udp_socket_fd = -1;

        return;
    }

    printf(
        "Agent response: %s\n",
        response);

    if (strcmp(
            response,
            "OK MONITOR_STARTED SID:"
            SID) != 0)
    {
        close(udp_socket_fd);

        udp_socket_fd = -1;

        return;
    }

    monitoring_active = 1;

    printf(
        "UDP monitoring active on port %d.\n",
        UDP_PORT);

    receive_monitor_updates();
}

/* =========================================================
   MONITOR STOP
   ========================================================= */

void handle_monitor_stop(
    int socket_fd)
{
    char response[BUFFER_SIZE];

    if (!monitoring_active)
    {
        printf(
            "Monitoring is not active.\n");

        return;
    }

    const char *request =
        "MONITOR STOP\n";

    if (send_all(
            socket_fd,
            request,
            strlen(request)) < 0)
    {
        return;
    }

    ssize_t length =
        recv_line(
            socket_fd,
            response,
            sizeof(response));

    if (length <= 0)
    {
        return;
    }

    printf(
        "Agent response: %s\n",
        response);

    if (strcmp(
            response,
            "OK MONITOR_STOPPED SID:"
            SID) == 0)
    {
        monitoring_active = 0;

        if (udp_socket_fd >= 0)
        {
            close(
                udp_socket_fd);

            udp_socket_fd = -1;
        }

        printf(
            "UDP monitoring stopped.\n");
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

    sock_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (sock_fd < 0)
    {
        perror("socket");

        exit(EXIT_FAILURE);
    }

    printf(
        "RemoteOps Controller - IT24101562\n");

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_port =
        htons(PORT);

    if (inet_pton(
            AF_INET,
            SERVER_IP,
            &server_addr.sin_addr) <= 0)
    {
        perror("inet_pton");

        close(sock_fd);

        exit(EXIT_FAILURE);
    }

    printf(
        "Connecting to Agent at %s:%d...\n",
        SERVER_IP,
        PORT);

    if (connect(
            sock_fd,
            (struct sockaddr *)
                &server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("connect");

        close(sock_fd);

        exit(EXIT_FAILURE);
    }

    printf(
        "Connected to RemoteOps Agent.\n");

    /* Authentication */

    printf(
        "Enter authentication token: ");

    if (scanf(
            "%99s",
            token) != 1)
    {
        close(sock_fd);

        return 1;
    }

    snprintf(
        buffer,
        sizeof(buffer),
        "AUTH %s\n",
        token);

    send_all(
        sock_fd,
        buffer,
        strlen(buffer));

    ssize_t length =
        recv_line(
            sock_fd,
            buffer,
            sizeof(buffer));

    if (length <= 0)
    {
        close(sock_fd);

        return 1;
    }

    printf(
        "Agent response: %s\n",
        buffer);

    if (strcmp(
            buffer,
            "OK AUTHENTICATED SID:"
            SID) != 0)
    {
        printf(
            "Authentication failed.\n");

        close(sock_fd);

        return 1;
    }

    int ch;

    while ((ch = getchar()) != '\n' &&
           ch != EOF)
    {
    }

    /* Command loop */

    while (1)
    {
        printf(
            "\nAvailable commands:\n");

        printf("  SYSINFO\n");
        printf("  LISTPROC\n");
        printf("  EXEC DATE\n");
        printf("  EXEC UPTIME\n");
        printf("  EXEC DISKFREE\n");
        printf("  EXEC HOSTNAME\n");
        printf("  EXEC WHOAMI\n");
        printf("  PUT <filename>\n");
        printf("  GET <filename>\n");
        printf("  MONITOR START\n");
        printf("  MONITOR STOP\n");
        printf("  QUIT\n");

        printf(
            "\nEnter command: ");

        if (fgets(
                command,
                sizeof(command),
                stdin) == NULL)
        {
            break;
        }

        command[
            strcspn(
                command,
                "\r\n")] = '\0';

        if (strlen(command) == 0)
        {
            continue;
        }

        if (strncmp(
                command,
                "PUT ",
                4) == 0)
        {
            handle_put(
                sock_fd,
                command);

            continue;
        }

        if (strncmp(
                command,
                "GET ",
                4) == 0)
        {
            handle_get(
                sock_fd,
                command);

            continue;
        }

        if (strcmp(
                command,
                "MONITOR START") == 0)
        {
            handle_monitor_start(
                sock_fd);

            continue;
        }

        if (strcmp(
                command,
                "MONITOR STOP") == 0)
        {
            handle_monitor_stop(
                sock_fd);

            continue;
        }

        snprintf(
            buffer,
            sizeof(buffer),
            "%s\n",
            command);

        if (send_all(
                sock_fd,
                buffer,
                strlen(buffer)) < 0)
        {
            break;
        }

        if (strcmp(
                command,
                "LISTPROC") == 0)
        {
            receive_until_marker(
                sock_fd,
                "OK LISTPROC SID:"
                SID);
        }

        else if (strncmp(
                     command,
                     "EXEC ",
                     5) == 0)
        {
            receive_until_marker(
                sock_fd,
                "OK EXEC SID:"
                SID);
        }

        else if (strcmp(
                     command,
                     "SYSINFO") == 0)
        {
            receive_until_marker(
                sock_fd,
                "OK SYSINFO SID:"
                SID);
        }

        else
        {
            length =
                recv_line(
                    sock_fd,
                    buffer,
                    sizeof(buffer));

            if (length <= 0)
            {
                break;
            }

            printf(
                "Agent response: %s\n",
                buffer);
        }

        if (strcmp(
                command,
                "QUIT") == 0)
        {
            break;
        }
    }

    if (udp_socket_fd >= 0)
    {
        close(
            udp_socket_fd);
    }

    close(sock_fd);

    return 0;
}
