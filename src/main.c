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

#define FILE_COUNT 6 

const char* files[FILE_COUNT] = {
    "test/main.c",
    "test/arena.h",
    "test/foo.c",
    "test/foo.h",
    "test/lib/lib.h",
    "test/lib2/lib.h",
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

void parse_include(HashTable* ht, const char* file, const char* buffer, struct stat st) {
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
    size_t len = end - start;

    char* include_start = arena_alloc(&arena, len + 1);
    strncpy(include_start, start, len);
    include_start[len] = 0;

    char* slash = strrchr(file, '/');
    if (!slash) {
        if (add_dependency(ht, file, include_start) != 0) {
            fprintf(stderr, "Failed to add_dependency");
            cleanup_and_exit(1);
        }

        return;
    }

    char* dotdot = strstr(include_start, "../");
    if (!dotdot) {
        len += (slash - file + 1);

        char copy[len];
        strncpy(copy, file, slash + 1 - file);
        copy[slash + 1 - file] = 0;
        strcat(copy, include_start);

        if (add_dependency(ht, file, copy) != 0) {
            fprintf(stderr, "Failed to add_dependency");
            cleanup_and_exit(1);
        }

        return;
    } 

    char* copy = arena_alloc(&arena, len);
    strncpy(copy, file,  slash - file);
    copy[slash - file] = 0;

    while (dotdot && slash) {
        char* prev_slash = strrchr(copy, '/');
        if (prev_slash) {
            *prev_slash = 0;
        } else {
            break;
        }

        include_start = dotdot + 3;
        dotdot = strstr(include_start, "../");
    }

    strcat(copy, "/");
    strcat(copy, include_start);

    if (add_dependency(ht, file, copy) != 0) {
        fprintf(stderr, "Failed to add_dependency");
        cleanup_and_exit(1);
    }
}

void search_for_preprocessor(HashTable* ht, const char* buffer, struct stat st, const char* file) {
    __m256i char_match = _mm256_set1_epi8('#');

    const char* current = buffer;
    size_t processed = 0;

    while (processed + 32 <= st.st_size) {
        __m256i str = _mm256_lddqu_si256((__m256i*) current);
        __m256i result = _mm256_cmpeq_epi8(str, char_match);
        int mask = _mm256_movemask_epi8(result);

        if (mask != 0) {
            while (mask != 0) {
                int pos = __builtin_ctz(mask);
                size_t abs_pos = processed + pos;

                if (abs_pos > st.st_size) break;

                if (strncmp(buffer + abs_pos + 1, "include", 7) == 0) {
                    parse_include(ht, file, buffer + abs_pos + 8, st);
                }               

                mask &= mask - 1;
            }
        }

        current += 32;
        processed += 32;
    }

    for (size_t i = processed; i < st.st_size; i++) {
        if (buffer[i] == '#' && strncmp(buffer + i + 1, "include", 7) == 0) {
            parse_include(ht, file, buffer + i + 8, st);
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

void load_hashtable(HashTable* ht, uint8_t count) {
    for (int i = 0; i < count; i++) {
        insert_ht(ht, files[i], 0);
    }

    for (int i = 0; i < count; i++) {
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


        uint32_t hash = 5381;
        for (const char* p = buffer; *p; p++) {
            hash = ((hash << 5) + hash) ^ (unsigned char)*p;
        }

        insert_ht(ht, files[i], hash);
        search_for_preprocessor(ht, buffer, st, files[i]);

        munmap(buffer, st.st_size);
        close(fd);
    }
}

void build_without_cache(HashTable* ht, uint8_t count) {
    for (int i = 0; i < count; i++) {

    }
}

void build_with_cache(HashTable* ht, CachedFiles* cache){}

int main() {
    HashTable* ht = create_hashtable(&arena, 128);
    if (!ht) {
        cleanup_and_exit(1);
    }

    load_hashtable(ht, FILE_COUNT);

    CachedFiles* cache = load_hashes();

    if (cache == NULL) {
        // no cache
        build_without_cache(ht, FILE_COUNT);
    } else {
        // has cache
        build_with_cache(ht, cache);
    }

    print_hashtable(ht);
    cleanup_and_exit(0);
}
