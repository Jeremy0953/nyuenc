#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

void encode(const char *data, size_t size, FILE *outputFile) {
    if (size == 0) return;

    char currentChar = data[0];
    unsigned char count = 1; // 使用unsigned char而不是size_t

    for (size_t i = 1; i < size; i++) {
        if (data[i] == currentChar) {
            count++;
        } else {
            // 写入当前字符
            fwrite(&currentChar, 1, 1, outputFile);
            // 以二进制形式的16进制编码写入计数
            fwrite(&count, sizeof(unsigned char), 1, outputFile);

            currentChar = data[i];
            count = 1;
        }
    }

    // 处理最后一组字符
    fwrite(&currentChar, 1, 1, outputFile);
    fwrite(&count, sizeof(unsigned char), 1, outputFile);
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

    fclose(outputFile);
    return 0;
}
