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
#include <pthread.h>
#include <signal.h>
#include <time.h>

#define PORT 9410
#define AUTH_TOKEN "OPS-1562"
#define SID "2651"
#define BUFFER_SIZE 4096
#define STORAGE_DIR "./agentfiles/IT24101562"
#define LOG_FILE "remoteops_IT24101562.log"
#define MONITOR_INTERVAL 2

/* =========================================================
   CLIENT AND MONITOR STRUCTURES
   ========================================================= */

typedef struct
{
    int client_fd;
    struct sockaddr_in client_addr;
} ClientInfo;

typedef struct
{
    int active;
    int udp_port;
    char controller_ip[INET_ADDRSTRLEN];

    pthread_t thread;
    pthread_mutex_t lock;
} MonitorInfo;

/* =========================================================
   GLOBAL LOG MUTEX

   Multiple Controller threads may write to the log file.
   This mutex prevents their log entries from overlapping.
   ========================================================= */

pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* =========================================================
   LOGGING FUNCTION
   ========================================================= */

void write_log(const char *client_ip,
               const char *event)
{
    time_t current_time;
    struct tm time_info;
    char timestamp[64];

    time(&current_time);

    localtime_r(&current_time,
                &time_info);

    strftime(timestamp,
             sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S",
             &time_info);

    pthread_mutex_lock(&log_mutex);

    FILE *log_file =
        fopen(LOG_FILE, "a");

    if (log_file != NULL)
    {
        fprintf(log_file,
                "%s | %s | %s | SID:%s\n",
                timestamp,
                client_ip,
                event,
                SID);

        fflush(log_file);
        fclose(log_file);
    }
    else
    {
        perror("Unable to open log file");
    }

    pthread_mutex_unlock(&log_mutex);
}

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

        if (sent < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (sent == 0)
        {
            return -1;
        }

        total_sent +=
            (size_t)sent;
    }

    return 0;
}

int send_text(int socket_fd,
              const char *text)
{
    return send_all(socket_fd,
                    text,
                    strlen(text));
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
            if (errno == EINTR)
            {
                continue;
            }

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

/* =========================================================
   FILE TRANSFER HELPERS
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
            remaining > sizeof(buffer)
                ? sizeof(buffer)
                : (size_t)remaining;

        ssize_t received =
            recv(socket_fd,
                 buffer,
                 amount,
                 0);

        if (received < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            return -1;
        }

        if (received == 0)
        {
            return -1;
        }

        size_t written =
            fwrite(buffer,
                   1,
                   (size_t)received,
                   file);

        if (written !=
            (size_t)received)
        {
            return -1;
        }

        remaining -=
            (unsigned long long)received;
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
            remaining > sizeof(buffer)
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

    if (gethostname(hostname,
                    sizeof(hostname)) != 0)
    {
        snprintf(hostname,
                 sizeof(hostname),
                 "UNKNOWN");
    }

    if (uname(&os_info) != 0)
    {
        send_text(
            client_fd,
            "ERR 016 SYSINFO_FAILED SID:"
            SID "\n");

        return;
    }

    if (sysinfo(&mem_info) != 0)
    {
        send_text(
            client_fd,
            "ERR 016 SYSINFO_FAILED SID:"
            SID "\n");

        return;
    }

    unsigned long total_ram =
        mem_info.totalram *
        mem_info.mem_unit /
        (1024 * 1024);

    unsigned long free_ram =
        mem_info.freeram *
        mem_info.mem_unit /
        (1024 * 1024);

    snprintf(
        response,
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

    send_text(client_fd,
              response);
}

/* =========================================================
   LISTPROC
   ========================================================= */

void send_listproc(int client_fd)
{
    FILE *fp;
    char line[512];

    fp =
        popen(
            "ps -eo pid,comm --no-headers",
            "r");

    if (fp == NULL)
    {
        send_text(
            client_fd,
            "ERR 003 LISTPROC_FAILED SID:"
            SID "\n");

        return;
    }

    send_text(client_fd,
              "PID COMMAND\n");

    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        if (send_text(client_fd,
                      line) < 0)
        {
            break;
        }
    }

    pclose(fp);

    send_text(
        client_fd,
        "OK LISTPROC SID:"
        SID "\n");
}

/* =========================================================
   EXEC WHITELIST
   ========================================================= */

void execute_whitelist_command(
    int client_fd,
    const char *name)
{
    const char *shell_command =
        NULL;

    if (strcmp(name,
               "DATE") == 0)
    {
        shell_command = "date";
    }
    else if (strcmp(name,
                    "UPTIME") == 0)
    {
        shell_command = "uptime";
    }
    else if (strcmp(name,
                    "DISKFREE") == 0)
    {
        shell_command = "df -h";
    }
    else if (strcmp(name,
                    "HOSTNAME") == 0)
    {
        shell_command = "hostname";
    }
    else if (strcmp(name,
                    "WHOAMI") == 0)
    {
        shell_command = "whoami";
    }
    else
    {
        send_text(
            client_fd,
            "ERR 004 EXEC_NOT_ALLOWED SID:"
            SID "\n");

        return;
    }

    FILE *fp;
    char line[512];

    fp =
        popen(shell_command,
              "r");

    if (fp == NULL)
    {
        send_text(
            client_fd,
            "ERR 005 EXEC_FAILED SID:"
            SID "\n");

        return;
    }

    while (fgets(line,
                 sizeof(line),
                 fp) != NULL)
    {
        if (send_text(client_fd,
                      line) < 0)
        {
            break;
        }
    }

    pclose(fp);

    send_text(
        client_fd,
        "OK EXEC SID:"
        SID "\n");
}

/* =========================================================
   FILENAME VALIDATION
   ========================================================= */

int valid_filename(
    const char *filename)
{
    if (filename == NULL ||
        filename[0] == '\0')
    {
        return 0;
    }

    if (strstr(filename,
               "..") != NULL ||
        strchr(filename,
               '/') != NULL ||
        strchr(filename,
               '\\') != NULL)
    {
        return 0;
    }

    return 1;
}

/* =========================================================
   STORAGE DIRECTORY
   ========================================================= */

int create_storage_directory(void)
{
    if (mkdir("./agentfiles",
              0755) < 0 &&
        errno != EEXIST)
    {
        return -1;
    }

    if (mkdir(STORAGE_DIR,
              0755) < 0 &&
        errno != EEXIST)
    {
        return -1;
    }

    return 0;
}

/* =========================================================
   PUT
   ========================================================= */

void handle_put(
    int client_fd,
    const char *filename,
    unsigned long long file_size)
{
    char filepath[1024];

    if (!valid_filename(filename))
    {
        send_text(
            client_fd,
            "ERR 006 INVALID_FILENAME SID:"
            SID "\n");

        return;
    }

    if (create_storage_directory() < 0)
    {
        send_text(
            client_fd,
            "ERR 007 STORAGE_ERROR SID:"
            SID "\n");

        return;
    }

    snprintf(
        filepath,
        sizeof(filepath),
        "%s/%s",
        STORAGE_DIR,
        filename);

    FILE *file =
        fopen(filepath,
              "wb");

    if (file == NULL)
    {
        send_text(
            client_fd,
            "ERR 007 STORAGE_ERROR SID:"
            SID "\n");

        return;
    }

    if (send_text(
            client_fd,
            "OK READY SID:"
            SID "\n") < 0)
    {
        fclose(file);
        remove(filepath);
        return;
    }

    if (recv_exact_to_file(
            client_fd,
            file,
            file_size) < 0)
    {
        fclose(file);

        remove(filepath);

        send_text(
            client_fd,
            "ERR 008 PUT_FAILED SID:"
            SID "\n");

        return;
    }

    fclose(file);

    send_text(
        client_fd,
        "OK PUT SID:"
        SID "\n");

    printf(
        "File uploaded: %s (%llu bytes)\n",
        filepath,
        file_size);
}

/* =========================================================
   GET
   ========================================================= */

void handle_get(
    int client_fd,
    const char *filename)
{
    char filepath[1024];
    char response[1024];

    if (!valid_filename(filename))
    {
        send_text(
            client_fd,
            "ERR 006 INVALID_FILENAME SID:"
            SID "\n");

        return;
    }

    snprintf(
        filepath,
        sizeof(filepath),
        "%s/%s",
        STORAGE_DIR,
        filename);

    FILE *file =
        fopen(filepath,
              "rb");

    if (file == NULL)
    {
        send_text(
            client_fd,
            "ERR 010 FILE_NOT_FOUND SID:"
            SID "\n");

        return;
    }

    if (fseek(file,
              0,
              SEEK_END) != 0)
    {
        fclose(file);

        send_text(
            client_fd,
            "ERR 011 GET_FAILED SID:"
            SID "\n");

        return;
    }

    long size =
        ftell(file);

    if (size < 0)
    {
        fclose(file);

        send_text(
            client_fd,
            "ERR 011 GET_FAILED SID:"
            SID "\n");

        return;
    }

    rewind(file);

    unsigned long long file_size =
        (unsigned long long)size;

    snprintf(
        response,
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

    if (send_file_bytes(
            client_fd,
            file,
            file_size) < 0)
    {
        fclose(file);
        return;
    }

    fclose(file);

    send_text(
        client_fd,
        "OK GET_COMPLETE SID:"
        SID "\n");

    printf(
        "File downloaded: %s (%llu bytes)\n",
        filepath,
        file_size);
}

/* =========================================================
   UDP MONITOR THREAD
   ========================================================= */

void *monitor_thread_function(
    void *arg)
{
    MonitorInfo *monitor =
        (MonitorInfo *)arg;

    int udp_socket =
        socket(AF_INET,
               SOCK_DGRAM,
               0);

    if (udp_socket < 0)
    {
        perror("UDP socket");
        return NULL;
    }

    while (1)
    {
        pthread_mutex_lock(
            &monitor->lock);

        int active =
            monitor->active;

        int udp_port =
            monitor->udp_port;

        char controller_ip[
            INET_ADDRSTRLEN];

        snprintf(
            controller_ip,
            sizeof(controller_ip),
            "%s",
            monitor->controller_ip);

        pthread_mutex_unlock(
            &monitor->lock);

        if (!active)
        {
            break;
        }

        struct sysinfo info;

        if (sysinfo(&info) == 0)
        {
            unsigned long total_ram_mb =
                info.totalram *
                info.mem_unit /
                (1024 * 1024);

            unsigned long free_ram_mb =
                info.freeram *
                info.mem_unit /
                (1024 * 1024);

            unsigned long used_ram_mb =
                total_ram_mb -
                free_ram_mb;

            double cpu_load =
                (double)info.loads[0] /
                65536.0;

            char message[512];

            snprintf(
                message,
                sizeof(message),
                "SYSINFO %.2f %lu %ld SID:%s",
                cpu_load,
                used_ram_mb,
                info.uptime,
                SID);

            struct sockaddr_in
                udp_address;

            memset(
                &udp_address,
                0,
                sizeof(udp_address));

            udp_address.sin_family =
                AF_INET;

            udp_address.sin_port =
                htons(
                    (unsigned short)
                    udp_port);

            if (inet_pton(
                    AF_INET,
                    controller_ip,
                    &udp_address.sin_addr) > 0)
            {
                sendto(
                    udp_socket,
                    message,
                    strlen(message),
                    0,
                    (struct sockaddr *)
                        &udp_address,
                    sizeof(udp_address));
            }
        }

        sleep(MONITOR_INTERVAL);
    }

    close(udp_socket);

    return NULL;
}

/* =========================================================
   START MONITORING
   ========================================================= */

int start_monitoring(
    int client_fd,
    MonitorInfo *monitor,
    const char *controller_ip,
    int udp_port)
{
    if (udp_port < 1 ||
        udp_port > 65535)
    {
        send_text(
            client_fd,
            "ERR 013 INVALID_UDP_PORT SID:"
            SID "\n");

        return -1;
    }

    pthread_mutex_lock(
        &monitor->lock);

    if (monitor->active)
    {
        pthread_mutex_unlock(
            &monitor->lock);

        send_text(
            client_fd,
            "ERR 014 MONITOR_ALREADY_RUNNING SID:"
            SID "\n");

        return -1;
    }

    monitor->active = 1;
    monitor->udp_port = udp_port;

    snprintf(
        monitor->controller_ip,
        sizeof(monitor->controller_ip),
        "%s",
        controller_ip);

    pthread_mutex_unlock(
        &monitor->lock);

    if (pthread_create(
            &monitor->thread,
            NULL,
            monitor_thread_function,
            monitor) != 0)
    {
        pthread_mutex_lock(
            &monitor->lock);

        monitor->active = 0;

        pthread_mutex_unlock(
            &monitor->lock);

        send_text(
            client_fd,
            "ERR 015 MONITOR_START_FAILED SID:"
            SID "\n");

        return -1;
    }

    send_text(
        client_fd,
        "OK MONITOR_STARTED SID:"
        SID "\n");

    printf(
        "UDP monitoring started for %s:%d\n",
        controller_ip,
        udp_port);

    return 0;
}

/* =========================================================
   STOP MONITORING
   ========================================================= */

void stop_monitoring(
    int client_fd,
    MonitorInfo *monitor)
{
    pthread_mutex_lock(
        &monitor->lock);

    int was_active =
        monitor->active;

    monitor->active = 0;

    pthread_mutex_unlock(
        &monitor->lock);

    if (was_active)
    {
        pthread_join(
            monitor->thread,
            NULL);
    }

    send_text(
        client_fd,
        "OK MONITOR_STOPPED SID:"
        SID "\n");
}

void stop_monitoring_disconnect(
    MonitorInfo *monitor)
{
    pthread_mutex_lock(
        &monitor->lock);

    int was_active =
        monitor->active;

    monitor->active = 0;

    pthread_mutex_unlock(
        &monitor->lock);

    if (was_active)
    {
        pthread_join(
            monitor->thread,
            NULL);
    }
}

/* =========================================================
   CLIENT THREAD
   ========================================================= */

void *handle_client(void *arg)
{
    ClientInfo *client_info =
        (ClientInfo *)arg;

    int client_fd =
        client_info->client_fd;

    struct sockaddr_in client_addr =
        client_info->client_addr;

    free(client_info);

    char controller_ip[
        INET_ADDRSTRLEN];

    if (inet_ntop(
            AF_INET,
            &client_addr.sin_addr,
            controller_ip,
            sizeof(controller_ip)) == NULL)
    {
        snprintf(
            controller_ip,
            sizeof(controller_ip),
            "UNKNOWN");
    }

    printf(
        "Controller connected from %s\n",
        controller_ip);

    write_log(
        controller_ip,
        "CONNECTED");

    MonitorInfo monitor;

    memset(&monitor,
           0,
           sizeof(monitor));

    pthread_mutex_init(
        &monitor.lock,
        NULL);

    char buffer[BUFFER_SIZE];

    /* =====================================================
       AUTHENTICATION
       ===================================================== */

    ssize_t line_length =
        recv_line(
            client_fd,
            buffer,
            sizeof(buffer));

    if (line_length <= 0)
    {
        printf(
            "Controller %s disconnected before authentication.\n",
            controller_ip);

        write_log(
            controller_ip,
            "DISCONNECTED BEFORE AUTHENTICATION");

        close(client_fd);

        pthread_mutex_destroy(
            &monitor.lock);

        return NULL;
    }

    printf(
        "[%s] Received: %s\n",
        controller_ip,
        buffer);

    /*
     * Do not write the authentication token to
     * the log file.
     */

    if (strcmp(
            buffer,
            "AUTH " AUTH_TOKEN) != 0)
    {
        send_text(
            client_fd,
            "ERR 001 AUTH_FAILED SID:"
            SID "\n");

        printf(
            "[%s] Authentication failed.\n",
            controller_ip);

        write_log(
            controller_ip,
            "AUTH FAILED");

        close(client_fd);

        pthread_mutex_destroy(
            &monitor.lock);

        return NULL;
    }

    send_text(
        client_fd,
        "OK AUTHENTICATED SID:"
        SID "\n");

    printf(
        "[%s] Authentication successful.\n",
        controller_ip);

    write_log(
        controller_ip,
        "AUTH SUCCESS");

    /* =====================================================
       COMMAND LOOP
       ===================================================== */

    while (1)
    {
        line_length =
            recv_line(
                client_fd,
                buffer,
                sizeof(buffer));

        if (line_length <= 0)
        {
            printf(
                "[%s] Controller disconnected.\n",
                controller_ip);

            write_log(
                controller_ip,
                "DISCONNECTED");

            stop_monitoring_disconnect(
                &monitor);

            break;
        }

        printf(
            "[%s] Received: %s\n",
            controller_ip,
            buffer);

        /*
         * Authentication is already complete,
         * therefore commands can safely be logged.
         */
        char log_event[
            BUFFER_SIZE + 32];

        snprintf(
            log_event,
            sizeof(log_event),
            "COMMAND %s",
            buffer);

        write_log(
            controller_ip,
            log_event);

        /* ---------------- SYSINFO ---------------- */

        if (strcmp(
                buffer,
                "SYSINFO") == 0)
        {
            send_sysinfo(
                client_fd);
        }

        /* ---------------- LISTPROC ---------------- */

        else if (strcmp(
                     buffer,
                     "LISTPROC") == 0)
        {
            send_listproc(
                client_fd);
        }

        /* ---------------- EXEC ---------------- */

        else if (strncmp(
                     buffer,
                     "EXEC ",
                     5) == 0)
        {
            execute_whitelist_command(
                client_fd,
                buffer + 5);
        }

        /* ---------------- PUT ---------------- */

        else if (strncmp(
                     buffer,
                     "PUT ",
                     4) == 0)
        {
            char filename[256];

            unsigned long long
                file_size;

            char extra;

            int parsed =
                sscanf(
                    buffer,
                    "PUT %255s %llu %c",
                    filename,
                    &file_size,
                    &extra);

            if (parsed != 2)
            {
                send_text(
                    client_fd,
                    "ERR 009 BAD_PUT_FORMAT SID:"
                    SID "\n");

                continue;
            }

            handle_put(
                client_fd,
                filename,
                file_size);
        }

        /* ---------------- GET ---------------- */

        else if (strncmp(
                     buffer,
                     "GET ",
                     4) == 0)
        {
            char filename[256];
            char extra;

            int parsed =
                sscanf(
                    buffer,
                    "GET %255s %c",
                    filename,
                    &extra);

            if (parsed != 1)
            {
                send_text(
                    client_fd,
                    "ERR 012 BAD_GET_FORMAT SID:"
                    SID "\n");

                continue;
            }

            handle_get(
                client_fd,
                filename);
        }

        /* ---------------- MONITOR START ---------------- */

        else if (strncmp(
                     buffer,
                     "MONITOR START ",
                     14) == 0)
        {
            int udp_port;
            char extra;

            int parsed =
                sscanf(
                    buffer,
                    "MONITOR START %d %c",
                    &udp_port,
                    &extra);

            if (parsed != 1)
            {
                send_text(
                    client_fd,
                    "ERR 013 INVALID_UDP_PORT SID:"
                    SID "\n");

                continue;
            }

            start_monitoring(
                client_fd,
                &monitor,
                controller_ip,
                udp_port);
        }

        /* ---------------- MONITOR STOP ---------------- */

        else if (strcmp(
                     buffer,
                     "MONITOR STOP") == 0)
        {
            stop_monitoring(
                client_fd,
                &monitor);
        }

        /* ---------------- QUIT ---------------- */

        else if (strcmp(
                     buffer,
                     "QUIT") == 0)
        {
            stop_monitoring_disconnect(
                &monitor);

            send_text(
                client_fd,
                "OK BYE SID:"
                SID "\n");

            printf(
                "[%s] Controller requested disconnect.\n",
                controller_ip);

            write_log(
                controller_ip,
                "GRACEFUL DISCONNECT");

            break;
        }

        /* ---------------- UNKNOWN COMMAND ---------------- */

        else
        {
            send_text(
                client_fd,
                "ERR 002 UNKNOWN_COMMAND SID:"
                SID "\n");

            write_log(
                controller_ip,
                "UNKNOWN COMMAND");
        }
    }

    close(client_fd);

    pthread_mutex_destroy(
        &monitor.lock);

    printf(
        "Controller thread for %s finished.\n",
        controller_ip);

    return NULL;
}

/* =========================================================
   MAIN SERVER
   ========================================================= */

int main(void)
{
    int server_fd;

    struct sockaddr_in
        server_addr;

    /*
     * Prevent Agent termination when attempting
     * to send to a disconnected Controller.
     */
    signal(SIGPIPE,
           SIG_IGN);

    server_fd =
        socket(
            AF_INET,
            SOCK_STREAM,
            0);

    if (server_fd < 0)
    {
        perror("socket");

        exit(EXIT_FAILURE);
    }

    int opt = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &opt,
            sizeof(opt)) < 0)
    {
        perror("setsockopt");

        close(server_fd);

        exit(EXIT_FAILURE);
    }

    memset(
        &server_addr,
        0,
        sizeof(server_addr));

    server_addr.sin_family =
        AF_INET;

    server_addr.sin_addr.s_addr =
        INADDR_ANY;

    server_addr.sin_port =
        htons(PORT);

    if (bind(
            server_fd,
            (struct sockaddr *)
                &server_addr,
            sizeof(server_addr)) < 0)
    {
        perror("bind");

        close(server_fd);

        exit(EXIT_FAILURE);
    }

    if (listen(
            server_fd,
            10) < 0)
    {
        perror("listen");

        close(server_fd);

        exit(EXIT_FAILURE);
    }

    printf(
        "RemoteOps Agent - IT24101562\n");

    printf(
        "Agent listening on TCP port %d...\n",
        PORT);

    printf(
        "Concurrent Controller support enabled.\n");

    printf(
        "Logging to %s\n",
        LOG_FILE);

    printf(
        "Waiting for Controller connections...\n");

    write_log(
        "AGENT",
        "AGENT STARTED");

    /* =====================================================
       ACCEPT CONTROLLERS CONTINUOUSLY
       ===================================================== */

    while (1)
    {
        struct sockaddr_in
            client_addr;

        socklen_t client_len =
            sizeof(client_addr);

        int client_fd =
            accept(
                server_fd,
                (struct sockaddr *)
                    &client_addr,
                &client_len);

        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }

            perror("accept");

            continue;
        }

        ClientInfo *client_info =
            malloc(
                sizeof(ClientInfo));

        if (client_info == NULL)
        {
            fprintf(
                stderr,
                "Unable to allocate client information.\n");

            close(client_fd);

            continue;
        }

        client_info->client_fd =
            client_fd;

        client_info->client_addr =
            client_addr;

        pthread_t client_thread;

        if (pthread_create(
                &client_thread,
                NULL,
                handle_client,
                client_info) != 0)
        {
            perror(
                "pthread_create");

            close(client_fd);

            free(client_info);

            continue;
        }

        pthread_detach(
            client_thread);
    }

    close(server_fd);

    pthread_mutex_destroy(
        &log_mutex);

    return 0;
}
