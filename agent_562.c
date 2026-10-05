#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-1562"
#define SID "2651"
#define BUFFER_SIZE 1024

/* Send system information to the Controller */
void send_sysinfo(int client_fd)
{
    char response[2048];
    char hostname[256];

    struct utsname os_info;
    struct sysinfo mem_info;

    long cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);

    gethostname(hostname, sizeof(hostname));
    uname(&os_info);
    sysinfo(&mem_info);

    unsigned long total_ram =
        mem_info.totalram * mem_info.mem_unit / (1024 * 1024);

    unsigned long free_ram =
        mem_info.freeram * mem_info.mem_unit / (1024 * 1024);

    snprintf(response, sizeof(response),
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

    send(client_fd, response, strlen(response), 0);
}

/* Send running process information */
void send_listproc(int client_fd)
{
    FILE *fp;
    char line[512];

    fp = popen("ps -eo pid,comm --no-headers", "r");

    if (fp == NULL)
    {
        const char *error =
            "ERR 003 LISTPROC_FAILED SID:" SID "\n";

        send(client_fd, error, strlen(error), 0);
        return;
    }

    const char *header = "PID COMMAND\n";

    send(client_fd, header, strlen(header), 0);

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        send(client_fd, line, strlen(line), 0);
    }

    pclose(fp);

    const char *end =
        "OK LISTPROC SID:" SID "\n";

    send(client_fd, end, strlen(end), 0);
}

/* Execute only approved whitelist commands */
void execute_whitelist_command(int client_fd, const char *name)
{
    const char *shell_command = NULL;

    /*
     * Only these commands are allowed.
     * User input is never passed directly to popen().
     */
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
        const char *error =
            "ERR 004 EXEC_NOT_ALLOWED SID:" SID "\n";

        send(client_fd, error, strlen(error), 0);
        return;
    }

    FILE *fp;
    char line[512];

    fp = popen(shell_command, "r");

    if (fp == NULL)
    {
        const char *error =
            "ERR 005 EXEC_FAILED SID:" SID "\n";

        send(client_fd, error, strlen(error), 0);
        return;
    }

    while (fgets(line, sizeof(line), fp) != NULL)
    {
        send(client_fd, line, strlen(line), 0);
    }

    pclose(fp);

    const char *end =
        "OK EXEC SID:" SID "\n";

    send(client_fd, end, strlen(end), 0);
}

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

    /* Allow port reuse */
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

    /* Listen for connections */
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent - IT24101562\n");
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

    /* ===================================== */
    /* AUTHENTICATION                        */
    /* ===================================== */

    memset(buffer, 0, sizeof(buffer));

    bytes_received =
        recv(client_fd,
             buffer,
             sizeof(buffer) - 1,
             0);

    if (bytes_received <= 0)
    {
        printf("Controller disconnected.\n");

        close(client_fd);
        close(server_fd);

        return 0;
    }

    buffer[bytes_received] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';

    printf("Received command: %s\n", buffer);

    if (strcmp(buffer, "AUTH " AUTH_TOKEN) != 0)
    {
        const char *response =
            "ERR 001 AUTH_FAILED SID:" SID "\n";

        send(client_fd,
             response,
             strlen(response),
             0);

        printf("Authentication failed.\n");

        close(client_fd);
        close(server_fd);

        return 0;
    }

    const char *auth_response =
        "OK AUTHENTICATED SID:" SID "\n";

    send(client_fd,
         auth_response,
         strlen(auth_response),
         0);

    printf("Authentication successful.\n");

    /* ===================================== */
    /* COMMAND LOOP                          */
    /* ===================================== */

    while (1)
    {
        memset(buffer, 0, sizeof(buffer));

        bytes_received =
            recv(client_fd,
                 buffer,
                 sizeof(buffer) - 1,
                 0);

        if (bytes_received <= 0)
        {
            printf("Controller disconnected.\n");
            break;
        }

        buffer[bytes_received] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received command: %s\n", buffer);

        /* SYSINFO */
        if (strcmp(buffer, "SYSINFO") == 0)
        {
            send_sysinfo(client_fd);
        }

        /* LISTPROC */
        else if (strcmp(buffer, "LISTPROC") == 0)
        {
            send_listproc(client_fd);
        }

        /* EXEC */
        else if (strncmp(buffer, "EXEC ", 5) == 0)
        {
            const char *exec_name = buffer + 5;

            execute_whitelist_command(client_fd,
                                      exec_name);
        }

        /* QUIT */
        else if (strcmp(buffer, "QUIT") == 0)
        {
            const char *response =
                "OK BYE SID:" SID "\n";

            send(client_fd,
                 response,
                 strlen(response),
                 0);

            printf("Controller requested disconnect.\n");
            break;
        }

        /* Unknown command */
        else
        {
            const char *response =
                "ERR 002 UNKNOWN_COMMAND SID:" SID "\n";

            send(client_fd,
                 response,
                 strlen(response),
                 0);
        }
    }

    close(client_fd);
    close(server_fd);

    return 0;
}
