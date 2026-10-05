#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-1562"
#define SID "2651"

#define BUFFER_SIZE 4096
#define STORAGE_DIR "./agentfiles/IT24101562"

/* =========================================================
   TCP HELPER FUNCTIONS
   ========================================================= */

int send_all(int socket_fd, const void *data, size_t length)
{
    const char *buffer = (const char *)data;
    size_t total_sent = 0;

    while (total_sent < length)
    {
        ssize_t sent = send(socket_fd,
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

int send_text(int socket_fd, const char *text)
{
    return send_all(socket_fd, text, strlen(text));
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
            recv(socket_fd, &ch, 1, 0);

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

/* Receive exact bytes from Controller into a file */
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

/* Send exact file bytes to Controller */
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
   SYSINFO
   ========================================================= */

void send_sysinfo(int client_fd)
{
    char response[2048];
    char hostname[256];

    struct utsname os_info;
    struct sysinfo mem_info;

    long cpu_cores =
        sysconf(_SC_NPROCESSORS_ONLN);

    gethostname(hostname, sizeof(hostname));
    uname(&os_info);
    sysinfo(&mem_info);

    unsigned long total_ram =
        mem_info.totalram *
        mem_info.mem_unit /
        (1024 * 1024);

    unsigned long free_ram =
        mem_info.freeram *
        mem_info.mem_unit /
        (1024 * 1024);

    snprintf(response,
             sizeof(response),
             "HOSTNAME: %s\n"
             "OS: %s\n"
             "KERNEL: %s\n"
             "CPU_CORES: %ld\n"
             "TOTAL_RAM_MB: %lu\n"
             "FREE_RAM_MB: %lu\n"
             "UPTIME_SEC: %ld\n"
             "OK SYSINFO SID:%s\n",
             hostname,
             os_info.sysname,
             os_info.release,
             cpu_cores,
             total_ram,
             free_ram,
             mem_info.uptime,
             SID);

    send_text(client_fd, response);
}

/* =========================================================
   LISTPROC
   ========================================================= */

void send_listproc(int client_fd)
{
    FILE *fp;
    char line[512];

    fp = popen("ps -eo pid,comm --no-headers", "r");

    if (fp == NULL)
    {
        send_text(client_fd,
                  "ERR 003 LISTPROC_FAILED SID:"
                  SID "\n");

        return;
    }

    send_text(client_fd, "PID COMMAND\n");

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        send_text(client_fd, line);
    }

    pclose(fp);

    send_text(client_fd,
              "OK LISTPROC SID:" SID "\n");
}

/* =========================================================
   EXEC
   ========================================================= */

void execute_whitelist_command(int client_fd,
                               const char *name)
{
    const char *shell_command = NULL;

    if (strcmp(name, "DATE") == 0)
    {
        shell_command = "date";
    }
    else if (strcmp(name, "UPTIME") == 0)
    {
        shell_command = "uptime";
    }
    else if (strcmp(name, "DISKFREE") == 0)
    {
        shell_command = "df -h";
    }
    else if (strcmp(name, "HOSTNAME") == 0)
    {
        shell_command = "hostname";
    }
    else if (strcmp(name, "WHOAMI") == 0)
    {
        shell_command = "whoami";
    }
    else
    {
        send_text(client_fd,
                  "ERR 004 EXEC_NOT_ALLOWED SID:"
                  SID "\n");

        return;
    }

    FILE *fp;
    char line[512];

    fp = popen(shell_command, "r");

    if (fp == NULL)
    {
        send_text(client_fd,
                  "ERR 005 EXEC_FAILED SID:"
                  SID "\n");

        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        send_text(client_fd, line);
    }

    pclose(fp);

    send_text(client_fd,
              "OK EXEC SID:" SID "\n");
}

/* =========================================================
   FILE VALIDATION
   ========================================================= */

int valid_filename(const char *filename)
{
    if (filename == NULL ||
        filename[0] == '\0')
    {
        return 0;
    }

    /*
     * Reject directory traversal and paths.
     * Only simple filenames are accepted.
     */
    if (strstr(filename, "..") != NULL ||
        strchr(filename, '/') != NULL ||
        strchr(filename, '\\') != NULL)
    {
        return 0;
    }

    return 1;
}

/* =========================================================
   PUT
   ========================================================= */

void handle_put(int client_fd,
                const char *filename,
                unsigned long long file_size)
{
    char filepath[1024];

    if (!valid_filename(filename))
    {
        send_text(client_fd,
                  "ERR 006 INVALID_FILENAME SID:"
                  SID "\n");

        return;
    }

    if (mkdir("./agentfiles", 0755) < 0 &&
        errno != EEXIST)
    {
        send_text(client_fd,
                  "ERR 007 STORAGE_ERROR SID:"
                  SID "\n");

        return;
    }

    if (mkdir(STORAGE_DIR, 0755) < 0 &&
        errno != EEXIST)
    {
        send_text(client_fd,
                  "ERR 007 STORAGE_ERROR SID:"
                  SID "\n");

        return;
    }

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *file =
        fopen(filepath, "wb");

    if (file == NULL)
    {
        send_text(client_fd,
                  "ERR 007 STORAGE_ERROR SID:"
                  SID "\n");

        return;
    }

    send_text(client_fd,
              "OK READY SID:" SID "\n");

    if (recv_exact_to_file(client_fd,
                           file,
                           file_size) < 0)
    {
        fclose(file);
        remove(filepath);

        send_text(client_fd,
                  "ERR 008 PUT_FAILED SID:"
                  SID "\n");

        return;
    }

    fclose(file);

    send_text(client_fd,
              "OK PUT SID:" SID "\n");

    printf("File uploaded: %s (%llu bytes)\n",
           filepath,
           file_size);
}

/* =========================================================
   GET
   ========================================================= */

void handle_get(int client_fd,
                const char *filename)
{
    char filepath[1024];
    char response[1024];

    if (!valid_filename(filename))
    {
        send_text(client_fd,
                  "ERR 006 INVALID_FILENAME SID:"
                  SID "\n");

        return;
    }

    snprintf(filepath,
             sizeof(filepath),
             "%s/%s",
             STORAGE_DIR,
             filename);

    FILE *file =
        fopen(filepath, "rb");

    if (file == NULL)
    {
        send_text(client_fd,
                  "ERR 010 FILE_NOT_FOUND SID:"
                  SID "\n");

        return;
    }

    /*
     * Determine exact file size.
     */
    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);

        send_text(client_fd,
                  "ERR 011 GET_FAILED SID:"
                  SID "\n");

        return;
    }

    long size = ftell(file);

    if (size < 0)
    {
        fclose(file);

        send_text(client_fd,
                  "ERR 011 GET_FAILED SID:"
                  SID "\n");

        return;
    }

    rewind(file);

    unsigned long long file_size =
        (unsigned long long)size;

    /*
     * Tell Controller the exact number
     * of bytes that will follow.
     */
    snprintf(response,
             sizeof(response),
             "OK GET %llu SID:%s\n",
             file_size,
             SID);

    if (send_text(client_fd,
                  response) < 0)
    {
        fclose(file);
        return;
    }

    /*
     * Send exactly file_size bytes.
     */
    if (send_file_bytes(client_fd,
                        file,
                        file_size) < 0)
    {
        fclose(file);
        return;
    }

    fclose(file);

    /*
     * Final newline-terminated response.
     */
    send_text(client_fd,
              "OK GET_COMPLETE SID:"
              SID "\n");

    printf("File downloaded: %s (%llu bytes)\n",
           filepath,
           file_size);
}

/* =========================================================
   MAIN
   ========================================================= */

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len =
        sizeof(client_addr);

    char buffer[BUFFER_SIZE];

    /* Create TCP socket */
    server_fd =
        socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &opt,
                   sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Configure server address */
    memset(&server_addr,
           0,
           sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    /* Bind */
    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    /* Listen */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent - IT24101562\n");
    printf("Agent listening on TCP port %d...\n",
           PORT);
    printf("Waiting for a Controller connection...\n");

    /* Accept Controller */
    client_fd =
        accept(server_fd,
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

    /* =====================================================
       AUTHENTICATION
       ===================================================== */

    ssize_t line_length =
        recv_line(client_fd,
                  buffer,
                  sizeof(buffer));

    if (line_length <= 0)
    {
        printf("Controller disconnected.\n");

        close(client_fd);
        close(server_fd);

        return 0;
    }

    printf("Received command: %s\n",
           buffer);

    if (strcmp(buffer,
               "AUTH " AUTH_TOKEN) != 0)
    {
        send_text(client_fd,
                  "ERR 001 AUTH_FAILED SID:"
                  SID "\n");

        printf("Authentication failed.\n");

        close(client_fd);
        close(server_fd);

        return 0;
    }

    send_text(client_fd,
              "OK AUTHENTICATED SID:"
              SID "\n");

    printf("Authentication successful.\n");

    /* =====================================================
       COMMAND LOOP
       ===================================================== */

    while (1)
    {
        line_length =
            recv_line(client_fd,
                      buffer,
                      sizeof(buffer));

        if (line_length <= 0)
        {
            printf("Controller disconnected.\n");
            break;
        }

        printf("Received command: %s\n",
               buffer);

        /* SYSINFO */
        if (strcmp(buffer,
                   "SYSINFO") == 0)
        {
            send_sysinfo(client_fd);
        }

        /* LISTPROC */
        else if (strcmp(buffer,
                        "LISTPROC") == 0)
        {
            send_listproc(client_fd);
        }

        /* EXEC */
        else if (strncmp(buffer,
                         "EXEC ",
                         5) == 0)
        {
            execute_whitelist_command(
                client_fd,
                buffer + 5);
        }

        /* PUT <filename> <size> */
        else if (strncmp(buffer,
                         "PUT ",
                         4) == 0)
        {
            char filename[256];
            unsigned long long file_size;
            char extra;

            int parsed =
                sscanf(buffer,
                       "PUT %255s %llu %c",
                       filename,
                       &file_size,
                       &extra);

            if (parsed != 2)
            {
                send_text(client_fd,
                          "ERR 009 BAD_PUT_FORMAT SID:"
                          SID "\n");

                continue;
            }

            handle_put(client_fd,
                       filename,
                       file_size);
        }

        /* GET <filename> */
        else if (strncmp(buffer,
                         "GET ",
                         4) == 0)
        {
            char filename[256];
            char extra;

            int parsed =
                sscanf(buffer,
                       "GET %255s %c",
                       filename,
                       &extra);

            if (parsed != 1)
            {
                send_text(client_fd,
                          "ERR 012 BAD_GET_FORMAT SID:"
                          SID "\n");

                continue;
            }

            handle_get(client_fd,
                       filename);
        }

        /* QUIT */
        else if (strcmp(buffer,
                        "QUIT") == 0)
        {
            send_text(client_fd,
                      "OK BYE SID:"
                      SID "\n");

            printf("Controller requested disconnect.\n");

            break;
        }

        /* Unknown command */
        else
        {
            send_text(client_fd,
                      "ERR 002 UNKNOWN_COMMAND SID:"
                      SID "\n");
        }
    }

    close(client_fd);
    close(server_fd);

    return 0;
}
