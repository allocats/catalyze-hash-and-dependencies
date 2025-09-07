#include <fcntl.h>
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "arena.h"
#include "hashtable.h"

static Arena arena = {0};

#define FILE_COUNT 4

const char* files[FILE_COUNT] = {
    "test/main.c",
    "test/arena.h",
    "test/foo.c",
    "test/foo.h",
};

typedef struct {
    uint32_t* name_hashes;
    uint32_t* content_hashes;
    uint8_t count;
    uint8_t capacity;
} CachedFiles;

void cleanup_and_exit(int code) {
    arena_free(&arena);
    exit(code);
}

uint32_t checksum(const char* buffer, const char* name, struct stat st) {
    uint32_t hash = 5381;

    for (uint8_t i = 0; i < strlen(name); i++) {
        hash = ((hash << 5) + hash) + name[i];
    }

    hash ^= (uint32_t) st.st_size;
    hash ^= (uint32_t) (st.st_size >> 32);
    hash ^= st.st_mtim.tv_nsec;

    for (size_t i = 0; i < st.st_size; i++) {
        hash = ((hash << 5) + hash) + buffer[i];
    }

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
    size_t processed = 0;

    while (processed + 32 <= size) {
        __m256i str = _mm256_lddqu_si256((__m256i*) current);
        __m256i result = _mm256_cmpeq_epi8(str, char_match);
        int mask = _mm256_movemask_epi8(result);

        if (mask != 0) {
            while (mask != 0) {
                int pos = __builtin_ctz(mask);
                size_t abs_pos = processed + pos;

                if (abs_pos > size) break;

                if (strncmp(buffer + abs_pos + 1, "include", 7) == 0) {
                    parse_include(buffer + abs_pos + 8);
                }               

                mask &= mask - 1;
            }
        }

        current += 32;
        processed += 32;
    }

    for (size_t i = processed; i < size; i++) {
        if (buffer[i] == '#' && strncmp(buffer + i + 1, "include", 7) == 0) {
            parse_include(buffer + i + 8);
        }
    }

}

CachedFiles* parse_cache(char* buffer) {
    CachedFiles* cache = arena_alloc(&arena, sizeof(*cache));

    cache -> name_hashes = arena_array_zero(&arena, uint32_t, FILE_COUNT);
    cache -> content_hashes = arena_array_zero(&arena, uint32_t, FILE_COUNT);
    cache -> count = 0;
    cache -> capacity = FILE_COUNT;

    char* current = buffer; 

    while (*current != '\0' && cache -> count < cache -> capacity) {
        char* token = strchr(current, ',');
        if (!token) {
            break;
        }

        size_t len = token - current;
        char temp = current[len];
        current[len] = '\0';
        cache -> name_hashes[cache -> count] = (uint32_t) strtoul(current, NULL, 16);
        current[len] = temp;

        current = token + 1;

        token = strchr(current, ',');
        if (!token) {
            len = strlen(current);
        } else {
            len = token - current;
        }

        temp = current[len];
        current[len] = '\0';
        cache -> content_hashes[cache -> count] = (uint32_t) strtoul(current, NULL, 16);
        current[len] = temp;

        cache -> count++;

        if (!token) {
            break;
        }

        current = token + 1;
    }

    return cache;
}

CachedFiles* load_hashes() {
    int fd = open("catalyze.cache", O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        fprintf(stderr, "fstat failed!\n");
        cleanup_and_exit(1);
    }

    char* buffer = mmap(NULL, st.st_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0); 
    if (buffer == MAP_FAILED) {
        fprintf(stderr, "Unable to allocate file!\n");
        cleanup_and_exit(1);
    }

    CachedFiles* result = parse_cache(buffer);

    close(fd);
    munmap(buffer, st.st_size);
    return result;
}

void print_cache(CachedFiles* cache) {
    for (int i = 0; i < cache -> count; i++) {
        printf("Cache: %x, %x\n", cache -> name_hashes[i], cache -> content_hashes[i]);
    }
}

int main() {
    CachedFiles* cache = load_hashes();

    if (cache == NULL) {
        // no cache
    } else {
        print_cache(cache);
    }

    HashTable* ht = create_hashtable(&arena, 128);

    if (!ht) {
        cleanup_and_exit(1);
    }

    for (int i = 0; i < FILE_COUNT; i++) {
        int fd = open(files[i], O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "File not found!\n");
            cleanup_and_exit(1);
        }

        struct stat st;
        if (fstat(fd, &st) == -1) {
            fprintf(stderr, "fstat failed!\n");
            cleanup_and_exit(1);
        }

        char* buffer = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0); 
        if (buffer == MAP_FAILED) {
            fprintf(stderr, "Unable to allocate file!\n");
            cleanup_and_exit(1);
        }

        insert_hashtable(&arena, ht, files[i]);
        // search_for_preprocessor(buffer, st.st_size);

        munmap(buffer, st.st_size);
        close(fd);
    }

    print_hashtable(ht);

    cleanup_and_exit(0);
}
