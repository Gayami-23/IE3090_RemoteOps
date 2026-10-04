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

int main(void)
{
    int server_fd;
    int client_fd;

    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;

    socklen_t client_len = sizeof(client_addr);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd < 0)
    {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;

    setsockopt(server_fd,
               SOL_SOCKET,
               SO_REUSEADDR,
               &opt,
               sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd,
             (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("RemoteOps Agent - IT24101562\n");
    printf("Agent listening on TCP port %d...\n", PORT);
    printf("Waiting for a Controller connection...\n");

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

    /* ---------- AUTHENTICATION ---------- */

    memset(buffer, 0, sizeof(buffer));

    bytes_received =
        recv(client_fd, buffer, sizeof(buffer) - 1, 0);

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

    /* ---------- COMMAND LOOP ---------- */

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

        if (strcmp(buffer, "SYSINFO") == 0)
        {
            send_sysinfo(client_fd);
        }
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
