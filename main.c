#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

// 保存上次字符和计数的全局变量
char lastChar = 0;
unsigned char lastCount = 0;

void flushLastChar(FILE *outputFile) {
    if (lastCount > 0) {
        fwrite(&lastChar, 1, 1, outputFile);
        fwrite(&lastCount, sizeof(unsigned char), 1, outputFile);
        lastCount = 0; // 重置计数
    }
}

void encode(const char *data, size_t size, FILE *outputFile) {
    if (size == 0) return;

    for (size_t i = 0; i < size; i++) {
        if (data[i] == lastChar) {
            lastCount++;
        } else {
            flushLastChar(outputFile);

            lastChar = data[i];
            lastCount = 1;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file1> [file2] ...\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE *outputFile = fopen("file2.enc", "wb");
    if (!outputFile) {
        perror("Error opening output file");
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd == -1) {
            perror("Error opening file");
            continue;
        }

        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            perror("Error getting file size");
            close(fd);
            continue;
        }

        if (sb.st_size == 0) {
            close(fd);
            continue;
        }

        char *data = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) {
            perror("Error mapping file");
            close(fd);
            continue;
        }

        encode(data, sb.st_size, outputFile);

        munmap(data, sb.st_size);
        close(fd);
    }

    // 处理最后一次字符和计数
    flushLastChar(outputFile);

    fclose(outputFile);
    return 0;
}
