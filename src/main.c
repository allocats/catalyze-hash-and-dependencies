#include <fcntl.h>
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_COUNT 4

const char* files[FILE_COUNT] = {
    "test/main.c",
    "test/arena.h",
    "test/foo.c",
    "test/foo.h",
};

size_t align_32(size_t size) {
    return (size + 31) & ~(31);
}

uint32_t checksum(const char* name, struct stat st) {
    uint32_t hash = 5381;

    for (uint8_t i = 0; i < strlen(name); i++) {
        hash = ((hash << 5) + hash) + name[i];
    }

    hash ^= (uint32_t) st.st_size;
    hash ^= (uint32_t) (st.st_size >> 32);
    hash ^= st.st_mtim.tv_nsec;

    return hash;
}

void parse_include(const char* buffer) {
    while (*buffer != '"') {
        if (*buffer == '<') return;
        buffer += 1;
    }
    buffer += 1;

    const char* start = buffer;
    while (*buffer != '"') {
        buffer += 1;
    }

    const char* end = buffer;
    int len = end - start;
    char copy[len + 1];
    strncpy(copy, start, len);
    copy[len] = 0;

    char* slash_pos = strrchr(copy, '/'); 
    const char* filename_start;

    if (slash_pos != NULL) {
        filename_start = slash_pos + 1;
        len = strlen(filename_start);
    } else {
        filename_start = copy;
    }

    for (int i = 0; i < FILE_COUNT; i++) {
        const char* source = strrchr(files[i], '/');

        if (source == NULL) {
            source = files[i];
        } else {
            source += 1;
        }

        if (strncmp(source, filename_start, len) == 0) {
            printf("Found header %.*s looking for corresponding C file...\n", len, filename_start);

            char c_file[len + 1];
            strncpy(c_file, filename_start, len);
            c_file[len] = 0;

            if (len > 2 && c_file[len - 2] == '.' && c_file[len - 1] == 'h') {
                c_file[len - 1] = 'c';
            }

            for (int j = 0; j < FILE_COUNT; j++) {
                const char* c_source = strrchr(files[j], '/'); 
                if (c_source == NULL) {
                    c_source = files[j];
                } else {
                    c_source += 1;
                }

                if (strncmp(c_source, c_file, len) == 0) {
                    printf("Found matching C file %.*s\n", len, c_file);
                    break;
                }
            }
        }
    }
}

void search_for_preprocessor(const char* buffer, size_t size) {
    __m256i char_match = _mm256_set1_epi8('#');

    const char* current = buffer;
    const char* end = buffer + size;
    int i = 0;

    while (current < end) {
        __m256i str = _mm256_lddqu_si256((__m256i*) current);
        __m256i result = _mm256_cmpeq_epi8(str, char_match);
        int mask = _mm256_movemask_epi8(result);

        if (mask != 0) {
            while (mask != 0) {
                int pos = __builtin_ctz(mask);
                size_t abs_pos = i * 32 + pos;

                if (abs_pos > size) break;

                if (strncmp(buffer + abs_pos + 1, "include", 7) == 0) {
                    parse_include(buffer + abs_pos + 8);
                }               

                mask &= mask - 1;
            }
        }

        current += 32;
        i++;
    }
}

int main() {
    for (int i = 0; i < FILE_COUNT; i++) {
        int fd = open(files[i], O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "File not found!\n");
            return 1;
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            fprintf(stderr, "fstat failed!\n");
            return 1;
        }

        uint32_t hash = checksum(files[i], st);
        fprintf(stdout, "\n%s Hash: %x\n", files[i], hash);

        char* buffer = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); 
        if (buffer == MAP_FAILED) {
            fprintf(stderr, "Unable to allocate file!\n");
            return 1;
        }

        size_t size = align_32(st.st_size);
        search_for_preprocessor(buffer, size);

        munmap(buffer, st.st_size);
        close(fd);
    }

    return 0;
}
