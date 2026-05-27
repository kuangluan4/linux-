#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <signal.h>

#define PORT 8888
#define BUF_SIZE 1024
#define MAX_CLIENTS 100
#define THREAD_POOL_SIZE 4

#define MODE_UPLOAD   1
#define MODE_DOWNLOAD 2

pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    int cfd;
    char client_ip[16];
    int client_port;
} task_t;

task_t task_queue[256];
int queue_front = 0, queue_rear = 0;
int queue_count = 0;

pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

volatile int server_running = 1;

void create_directories() {
    if (mkdir("server_files", 0755) == -1 && errno != EEXIST) {
        perror("mkdir server_files");
        exit(1);
    }
    if (mkdir("logs", 0755) == -1 && errno != EEXIST) {
        perror("mkdir logs");
        exit(1);
    }
}

void write_log(const char *client_ip, const char *org_filename,
    const char *action, const char *detail, uint32_t file_size) {
    pthread_mutex_lock(&log_mutex);
    FILE *log = fopen("logs/transfer.log", "a");
    if (log == NULL) {
        perror("fopen log");
    } else {
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        fprintf(log, "%04d-%02d-%02d %02d:%02d:%02d - %s %s %s (%u bytes) %s\n",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec,
            client_ip, action, org_filename, file_size, detail);
        fclose(log);
    }
    pthread_mutex_unlock(&log_mutex);
}

static void skip_bytes(int cfd, uint32_t count) {
    char discard[BUF_SIZE];
    while (count > 0) {
        int n = recv(cfd, discard, count > BUF_SIZE ? BUF_SIZE : count, 0);
        if (n <= 0) break;
        count -= n;
    }
}

void handle_upload(int cfd, const char *client_ip) {
    uint32_t file_count_net;
    int ret = recv(cfd, &file_count_net, sizeof(file_count_net), 0);
    if (ret != sizeof(file_count_net)) {
        pthread_mutex_lock(&print_mutex);
        printf("Failed to receive file count\n");
        pthread_mutex_unlock(&print_mutex);
        return;
    }
    uint32_t file_count = ntohl(file_count_net);

    pthread_mutex_lock(&print_mutex);
    printf("Receiving %u files from %s\n", file_count, client_ip);
    pthread_mutex_unlock(&print_mutex);

    for (uint32_t file_idx = 0; file_idx < file_count; file_idx++) {
        char filename[256] = {0};
        int i = 0;
        while (i < (int)sizeof(filename) - 1) {
            char c;
            int n = recv(cfd, &c, 1, 0);
            if (n <= 0) {
                pthread_mutex_lock(&print_mutex);
                printf("Failed to receive filename\n");
                pthread_mutex_unlock(&print_mutex);
                return;
            }
            if (c == '\n') break;
            filename[i++] = c;
        }
        filename[i] = '\0';

        const char *safe_name = strrchr(filename, '/');
        if (safe_name) safe_name++;
        else safe_name = filename;

        uint32_t file_size_net;
        ret = recv(cfd, &file_size_net, sizeof(file_size_net), 0);
        if (ret != sizeof(file_size_net)) {
            pthread_mutex_lock(&print_mutex);
            printf("Failed to receive file size\n");
            pthread_mutex_unlock(&print_mutex);
            return;
        }
        uint32_t file_size = ntohl(file_size_net);

        char save_name[512];
        snprintf(save_name, sizeof(save_name), "server_files/%s", safe_name);

        // Check for existing partial file
        uint32_t existing = 0;
        struct stat st;
        if (stat(save_name, &st) == 0) {
            if ((uint32_t)st.st_size < file_size) {
                existing = (uint32_t)st.st_size;
            } else if ((uint32_t)st.st_size > file_size) {
                // File on disk is larger than expected, remove and restart
                remove(save_name);
            }
        }

        // Tell client how many bytes we already have
        uint32_t existing_net = htonl(existing);
        send(cfd, &existing_net, sizeof(existing_net), 0);

        if (existing >= file_size) {
            pthread_mutex_lock(&print_mutex);
            printf("File %s already complete, skipped\n", safe_name);
            pthread_mutex_unlock(&print_mutex);
            continue;
        }

        FILE *fp = fopen(save_name, existing > 0 ? "ab" : "wb");
        if (fp == NULL) {
            perror("fopen");
            skip_bytes(cfd, file_size - existing);
            continue;
        }

        uint32_t remaining = file_size - existing;
        uint32_t received = 0;
        char buf[BUF_SIZE];
        while (received < remaining) {
            int need = (remaining - received) > BUF_SIZE ? BUF_SIZE : (remaining - received);
            int n = recv(cfd, buf, need, 0);
            if (n <= 0) {
                pthread_mutex_lock(&print_mutex);
                printf("Transfer interrupted at %u/%u bytes (can resume later)\n",
                    existing + received, file_size);
                pthread_mutex_unlock(&print_mutex);
                break;
            }
            fwrite(buf, 1, n, fp);
            received += n;
            pthread_mutex_lock(&print_mutex);
            printf("Received %u/%u bytes\r", existing + received, file_size);
            fflush(stdout);
            pthread_mutex_unlock(&print_mutex);
        }
        printf("\n");
        fclose(fp);

        if (existing + received >= file_size) {
            write_log(client_ip, safe_name, "uploaded", save_name, file_size);
        }
    }
}

void handle_download(int cfd, const char *client_ip) {
    uint32_t file_count_net;
    int ret = recv(cfd, &file_count_net, sizeof(file_count_net), 0);
    if (ret != sizeof(file_count_net)) {
        pthread_mutex_lock(&print_mutex);
        printf("Failed to receive download file count\n");
        pthread_mutex_unlock(&print_mutex);
        return;
    }
    uint32_t file_count = ntohl(file_count_net);

    pthread_mutex_lock(&print_mutex);
    printf("Client %s requests %u files for download\n", client_ip, file_count);
    pthread_mutex_unlock(&print_mutex);

    for (uint32_t i = 0; i < file_count; i++) {
        char filename[256] = {0};
        int j = 0;
        while (j < (int)sizeof(filename) - 1) {
            char c;
            int n = recv(cfd, &c, 1, 0);
            if (n <= 0) return;
            if (c == '\n') break;
            filename[j++] = c;
        }
        filename[j] = '\0';

        const char *safe_name = strrchr(filename, '/');
        if (safe_name) safe_name++;
        else safe_name = filename;

        char filepath[512];
        snprintf(filepath, sizeof(filepath), "server_files/%s", safe_name);

        FILE *fp = fopen(filepath, "rb");
        if (fp == NULL) {
            uint32_t status = htonl(1);
            send(cfd, &status, sizeof(status), 0);
            pthread_mutex_lock(&print_mutex);
            printf("Download: %s not found\n", safe_name);
            pthread_mutex_unlock(&print_mutex);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        uint32_t status = htonl(0);
        send(cfd, &status, sizeof(status), 0);

        uint32_t size_net = htonl((uint32_t)file_size);
        send(cfd, &size_net, sizeof(size_net), 0);

        // Receive client's existing bytes (for resume)
        uint32_t existing_net;
        ret = recv(cfd, &existing_net, sizeof(existing_net), 0);
        if (ret != sizeof(existing_net)) {
            fclose(fp);
            return;
        }
        uint32_t existing = ntohl(existing_net);

        if (existing >= (uint32_t)file_size) {
            pthread_mutex_lock(&print_mutex);
            printf("Client already has complete %s, skipped\n", safe_name);
            pthread_mutex_unlock(&print_mutex);
            fclose(fp);
            continue;
        }

        fseek(fp, existing, SEEK_SET);
        long remaining = file_size - existing;

        char buf[BUF_SIZE];
        size_t sent = 0;
        while (sent < (size_t)remaining) {
            size_t need = (remaining - sent) > BUF_SIZE ? BUF_SIZE : (remaining - sent);
            size_t nr = fread(buf, 1, need, fp);
            if (nr == 0) break;
            int ns = send(cfd, buf, nr, 0);
            if (ns <= 0) {
                pthread_mutex_lock(&print_mutex);
                printf("Transfer interrupted at %lu/%lu bytes (can resume later)\n",
                    existing + sent, file_size);
                pthread_mutex_unlock(&print_mutex);
                break;
            }
            sent += ns;
        }
        fclose(fp);

        pthread_mutex_lock(&print_mutex);
        printf("Sent %s (%lu/%ld bytes) to %s\n",
            safe_name, existing + sent, file_size, client_ip);
        pthread_mutex_unlock(&print_mutex);

        if (existing + sent >= (size_t)file_size) {
            write_log(client_ip, safe_name, "downloaded", client_ip, (uint32_t)file_size);
        }
    }
}

void *handle_client(void *arg) {
    task_t *task = (task_t *)arg;
    int cfd = task->cfd;
    char client_ip[16];
    strcpy(client_ip, task->client_ip);
    int client_port = task->client_port;
    free(task);

    uint32_t mode_net;
    int n = recv(cfd, &mode_net, sizeof(mode_net), 0);
    if (n != sizeof(mode_net)) {
        pthread_mutex_lock(&print_mutex);
        printf("Failed to receive mode from %s:%d\n", client_ip, client_port);
        pthread_mutex_unlock(&print_mutex);
        close(cfd);
        return NULL;
    }
    uint32_t mode = ntohl(mode_net);

    if (mode == MODE_UPLOAD) {
        handle_upload(cfd, client_ip);
    } else if (mode == MODE_DOWNLOAD) {
        handle_download(cfd, client_ip);
    } else {
        pthread_mutex_lock(&print_mutex);
        printf("Unknown mode %u from %s:%d\n", mode, client_ip, client_port);
        pthread_mutex_unlock(&print_mutex);
    }

    close(cfd);
    return NULL;
}

void *worker_thread(void *arg) {
    (void)arg;
    while (server_running) {
        pthread_mutex_lock(&queue_mutex);
        while (queue_count == 0 && server_running) {
            pthread_cond_wait(&queue_cond, &queue_mutex);
        }
        if (!server_running) {
            pthread_mutex_unlock(&queue_mutex);
            break;
        }
        task_t t = task_queue[queue_front];
        queue_front = (queue_front + 1) % 256;
        queue_count--;
        pthread_mutex_unlock(&queue_mutex);

        task_t *task_ptr = (task_t *)malloc(sizeof(task_t));
        *task_ptr = t;
        handle_client(task_ptr);
    }
    return NULL;
}

void sigint_handler(int sig) {
    if (sig == SIGINT) {
        server_running = 0;
        pthread_cond_broadcast(&queue_cond);
    }
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sigint_handler);
    create_directories();

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1) {
        perror("socket");
        return -1;
    }
    printf("Socket created successfully\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr);

    int ret = bind(lfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret == -1) {
        perror("bind");
        return -1;
    }
    printf("Bind successfully\n");

    ret = listen(lfd, MAX_CLIENTS);
    if (ret == -1) {
        perror("listen");
        return -1;
    }
    printf("Listen successfully\n");

    pthread_t workers[THREAD_POOL_SIZE];
    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
    }

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int cfd = accept(lfd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (cfd == -1) {
            if (server_running) perror("accept");
            continue;
        }

        char client_ip[16];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);
        printf("Client connected: %s:%d\n", client_ip, client_port);

        task_t new_task;
        new_task.cfd = cfd;
        strcpy(new_task.client_ip, client_ip);
        new_task.client_ip[sizeof(new_task.client_ip) - 1] = '\0';
        new_task.client_port = client_port;

        pthread_mutex_lock(&queue_mutex);
        if (queue_count < 256) {
            task_queue[queue_rear] = new_task;
            queue_rear = (queue_rear + 1) % 256;
            queue_count++;
            pthread_cond_signal(&queue_cond);
        } else {
            printf("Task queue full, rejecting %s:%d\n", client_ip, client_port);
            close(cfd);
        }
        pthread_mutex_unlock(&queue_mutex);
    }

    for (int i = 0; i < THREAD_POOL_SIZE; i++) {
        pthread_join(workers[i], NULL);
    }
    printf("Server shut down.\n");
    close(lfd);
    return 0;
}
