#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 8888
#define BUF_SIZE 1024

#define MODE_UPLOAD   1
#define MODE_DOWNLOAD 2

void run_upload(int cfd) {
    const char *files[] = {"client_files/test.txt", "client_files/test.dat", NULL};
    int file_count = 0;
    while (files[file_count] != NULL) file_count++;

    uint32_t mode_net = htonl(MODE_UPLOAD);
    send(cfd, &mode_net, sizeof(mode_net), 0);

    uint32_t count_net = htonl(file_count);
    send(cfd, &count_net, sizeof(count_net), 0);

    for (int i = 0; i < file_count; i++) {
        const char *path = files[i];
        const char *filename = strrchr(path, '/');
        if (filename == NULL) filename = path;
        else filename++;

        FILE *fp = fopen(path, "rb");
        if (fp == NULL) {
            perror("fopen");
            continue;
        }
        fseek(fp, 0, SEEK_END);
        long file_size = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        send(cfd, filename, strlen(filename), 0);
        send(cfd, "\n", 1, 0);

        uint32_t size_net = htonl((uint32_t)file_size);
        send(cfd, &size_net, sizeof(size_net), 0);

        // Receive existing bytes from server for resume
        uint32_t existing_net;
        int ret = recv(cfd, &existing_net, sizeof(existing_net), 0);
        if (ret != sizeof(existing_net)) {
            printf("Failed to receive existing bytes for %s\n", filename);
            fclose(fp);
            return;
        }
        uint32_t existing = ntohl(existing_net);

        if (existing >= (uint32_t)file_size) {
            printf("File %s already complete on server, skipped\n", filename);
            fclose(fp);
            continue;
        }

        if (existing > 0) {
            printf("Resuming upload from byte %u/%ld\n", existing, file_size);
            fseek(fp, existing, SEEK_SET);
        }

        long remaining = file_size - existing;
        char buf[BUF_SIZE];
        size_t send_total = 0;
        while (send_total < (size_t)remaining) {
            size_t need = (remaining - send_total) > BUF_SIZE ? BUF_SIZE : (remaining - send_total);
            size_t n = fread(buf, 1, need, fp);
            if (n == 0) break;
            int s = send(cfd, buf, n, 0);
            if (s <= 0) {
                perror("send");
                break;
            }
            send_total += s;
            printf("Sent: %zu / %ld bytes\r", existing + send_total, file_size);
            fflush(stdout);
        }
        printf("\nFile sent: %s\n", filename);
        fclose(fp);
    }
}

void run_download(int cfd) {
    const char *files[] = {"test.txt", "123.dat", NULL};
    int file_count = 0;
    while (files[file_count] != NULL) file_count++;

    if (mkdir("client_files", 0755) == -1 && errno != EEXIST) {
        perror("mkdir client_files");
        return;
    }

    uint32_t mode_net = htonl(MODE_DOWNLOAD);
    send(cfd, &mode_net, sizeof(mode_net), 0);

    uint32_t count_net = htonl(file_count);
    send(cfd, &count_net, sizeof(count_net), 0);

    for (int i = 0; i < file_count; i++) {
        const char *filename = files[i];

        send(cfd, filename, strlen(filename), 0);
        send(cfd, "\n", 1, 0);

        uint32_t status_net;
        int n = recv(cfd, &status_net, sizeof(status_net), 0);
        if (n != sizeof(status_net)) {
            printf("Failed to receive status for %s\n", filename);
            return;
        }
        if (ntohl(status_net) != 0) {
            printf("File not found on server: %s\n", filename);
            continue;
        }

        uint32_t size_net;
        n = recv(cfd, &size_net, sizeof(size_net), 0);
        if (n != sizeof(size_net)) {
            printf("Failed to receive file size for %s\n", filename);
            return;
        }
        uint32_t file_size = ntohl(size_net);

        char save_path[512];
        snprintf(save_path, sizeof(save_path), "client_files/%s", filename);

        // Check for existing partial file
        uint32_t existing = 0;
        struct stat st;
        if (stat(save_path, &st) == 0 && (uint32_t)st.st_size < file_size) {
            existing = (uint32_t)st.st_size;
        }

        // Send existing bytes to server for resume
        uint32_t existing_net = htonl(existing);
        send(cfd, &existing_net, sizeof(existing_net), 0);

        if (existing >= file_size) {
            printf("File %s already complete, skipped\n", filename);
            continue;
        }

        if (existing > 0) {
            printf("Resuming download from byte %u/%u\n", existing, file_size);
        }

        FILE *fp = fopen(save_path, existing > 0 ? "ab" : "wb");
        if (fp == NULL) {
            perror("fopen");
            // Discard incoming data
            char discard[BUF_SIZE];
            uint32_t left = file_size - existing;
            while (left > 0) {
                int nr = recv(cfd, discard, left > BUF_SIZE ? BUF_SIZE : left, 0);
                if (nr <= 0) break;
                left -= nr;
            }
            continue;
        }

        char buf[BUF_SIZE];
        uint32_t received = 0;
        uint32_t remaining = file_size - existing;
        while (received < remaining) {
            int need = (remaining - received) > BUF_SIZE ? BUF_SIZE : (remaining - received);
            int nr = recv(cfd, buf, need, 0);
            if (nr <= 0) {
                printf("Transfer interrupted at %u/%u bytes (can resume later)\n",
                    existing + received, file_size);
                break;
            }
            fwrite(buf, 1, nr, fp);
            received += nr;
            printf("Downloaded: %u / %u bytes\r", existing + received, file_size);
            fflush(stdout);
        }
        printf("\nFile downloaded: %s -> %s\n", filename, save_path);
        fclose(fp);
    }
}

int main(void) {
    if (mkdir("client_files", 0755) == -1 && errno != EEXIST) {
        perror("mkdir client_files");
    }

    int cfd = socket(AF_INET, SOCK_STREAM, 0);
    if (cfd == -1) {
        perror("socket");
        return -1;
    }
    printf("Socket created successfully!\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    int ret = connect(cfd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret == -1) {
        perror("connect");
        close(cfd);
        return -1;
    }
    printf("Connected to server successfully\n");

    int mode;
    printf("Select mode: 1=Upload, 2=Download: ");
    if (scanf("%d", &mode) != 1) {
        printf("Invalid input\n");
        close(cfd);
        return -1;
    }

    if (mode == MODE_UPLOAD) {
        run_upload(cfd);
    } else if (mode == MODE_DOWNLOAD) {
        run_download(cfd);
    } else {
        printf("Invalid mode\n");
    }

    close(cfd);
    return 0;
}
