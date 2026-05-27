#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BUFFER_SIZE (1024 * 1024)  // 1MB 缓冲区

void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s <filename> <size> [pattern|random]\n", prog);
    fprintf(stderr, "  size: 数字 + 单位 (b, k, m, g) 例如: 100m, 1g, 512k\n");
    fprintf(stderr, "  pattern: 填充指定字符(如 'A')，默认 random\n");
    fprintf(stderr, "Example: %s test.dat 100m\n", prog);
    fprintf(stderr, "         %s test.dat 2g A\n", prog);
}

long long parse_size(const char *str) {
    char *end;
    long long num = strtoll(str, &end, 10);
    if (end == str) return -1;
    switch (*end) {
        case 'b': case 'B': return num;
        case 'k': case 'K': return num * 1024LL;
        case 'm': case 'M': return num * 1024LL * 1024;
        case 'g': case 'G': return num * 1024LL * 1024 * 1024;
        default: return -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char *filename = argv[1];
    long long total_bytes = parse_size(argv[2]);
    if (total_bytes <= 0) {
        fprintf(stderr, "Invalid size: %s\n", argv[2]);
        return 1;
    }

    int use_random = 1;
    unsigned char fill_char = 0;
    if (argc >= 4) {
        if (strlen(argv[3]) == 1) {
            use_random = 0;
            fill_char = (unsigned char)argv[3][0];
        } else if (strcmp(argv[3], "random") == 0) {
            use_random = 1;
        } else {
            fprintf(stderr, "Invalid pattern, use a single character or 'random'\n");
            return 1;
        }
    }

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    unsigned char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) {
        perror("malloc");
        fclose(fp);
        return 1;
    }

    srand(time(NULL));
    long long remaining = total_bytes;
    printf("Generating %lld bytes (%s)...\n", total_bytes, filename);
    while (remaining > 0) {
        int chunk = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : (int)remaining;
        if (use_random) {
            for (int i = 0; i < chunk; i++) {
                buffer[i] = rand() & 0xFF;
            }
        } else {
            memset(buffer, fill_char, chunk);
        }
        size_t written = fwrite(buffer, 1, chunk, fp);
        if (written != chunk) {
            perror("fwrite");
            break;
        }
        remaining -= chunk;
        // 简单进度显示
        if (remaining % (BUFFER_SIZE * 100) == 0) {
            printf("\rProgress: %.1f%%", (double)(total_bytes - remaining) / total_bytes * 100);
            fflush(stdout);
        }
    }
    printf("\rDone. %lld bytes written to %s\n", total_bytes - remaining, filename);

    free(buffer);
    fclose(fp);
    return 0;
}