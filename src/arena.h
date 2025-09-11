/*
 *
 *  Inspired by tsoding's implementation at https://github.com/tsoding/arena
 *  Original work licensed under MIT License
 *
 *  Usage:
 *
 *      #include "arena.h"
 *
 */

#ifndef ARENA_H
#define ARENA_H 

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#define DEFAULT_CAPACITY (2 * 1024) 

#define arena_array(arena, type, count) \
    (type*) arena_alloc(arena, sizeof(type) * (count)) 

#define arena_array_zero(arena, type, count) \
    (type*) arena_memset(arena_alloc(arena, sizeof(type) * (count)), 0, sizeof(type) * (count)) 

typedef struct ArenaBlock {
    struct ArenaBlock* next;
    size_t usage;
    size_t capacity;
    uintptr_t data[];
} ArenaBlock;

typedef struct {
    ArenaBlock* start;
    ArenaBlock* end;
} Arena;

static ArenaBlock* new_block(size_t size);
static void free_block(ArenaBlock* block);

static void* arena_alloc(Arena* arena, size_t size);
static void* arena_realloc(Arena* arena, void* ptr, size_t old_size, size_t new_size);
static void* arena_memset(void* ptr, int value, size_t len);
static void* arena_memcpy(void* dest, const void* src, size_t len);
static char* arena_strdup(Arena* arena, const char* str);

static void arena_reset(Arena* arena);
static void arena_free(Arena* arena); 

static size_t total_capacity(Arena* arena);
static size_t total_usage(Arena* arena); 

static inline size_t align_size(size_t size) {
    return (size + sizeof(void*) - 1) & ~(sizeof(void*) - 1);
}

static inline size_t _strlen(const char* str) {
    size_t len = 0;
    while (*str++) len++;
    return len;
}

static ArenaBlock* new_block(size_t size) {
    size_t capacity = DEFAULT_CAPACITY;

    while (size > capacity * sizeof(uintptr_t)) capacity *= 2;

    size_t bytes = capacity * sizeof(uintptr_t);
    size_t total_size = sizeof(ArenaBlock) + bytes;
    ArenaBlock* block = (ArenaBlock*) malloc(total_size);
    assert(block);

    block -> next = NULL;
    block -> usage =  0;
    block -> capacity = bytes;

    return block;
}

static inline void free_block(ArenaBlock* block) {
    free(block);
}

static void* arena_alloc(Arena* arena, size_t size) {
    size = align_size(size);

    if (arena -> end == NULL && arena -> start == NULL) {
        arena -> end = new_block(size);
        arena -> start = arena -> end;
    }

    while (arena -> end -> usage + size > arena -> end -> capacity && arena -> end -> next != NULL) {
        arena -> end = arena -> end -> next;
    } 

    if (arena -> end -> usage + size > arena -> end -> capacity) {
            ArenaBlock* block = new_block(size);
            arena -> end -> next = block;
            arena -> end = block;
    }

    void* result = (char*) &arena -> end -> data + arena -> end -> usage;
    arena -> end -> usage += size;

    return result;
}

static void* arena_realloc(Arena* arena, void* ptr, size_t old_size, size_t new_size) {
    if (new_size <= old_size) return ptr;

    void* result = arena_alloc(arena, new_size);
    char* new_ptr = (char*) result;
    char* old_ptr = (char*) ptr;

    for (size_t i = 0; i < old_size; i++) {
        new_ptr[i] = old_ptr[i];
    }

    for (size_t i = old_size; i < new_size; i++) {
        new_ptr[i] = 0;
    }

    return result;
}

static void* arena_memset(void* ptr, int value, size_t len) {
    char* p = (char*) ptr;
    char byte_value = (char) value;

    while (len > 0) {
        *p++ = byte_value;
        len--;
    }

    return ptr;
} 

static void* arena_memcpy(void* dest, const void* src, size_t len) {
    char* d = dest;
    const char* s = src;

    while (len > 0) {
        *d++ = *s++;
        len--;
    }

    return dest;
}

static char* arena_strdup(Arena* arena, const char* str) {
    size_t len = _strlen(str);
    char* duplicate = (char*) arena_alloc(arena, len + 1);

    arena_memcpy(duplicate, str, len + 1);
    duplicate[len] = '\0';

    return duplicate;
}

static inline void arena_reset(Arena* arena) {
    for (ArenaBlock* block = arena -> start; block != NULL; block = block -> next) {
        block -> usage = 0;
    }
    arena -> end = arena -> start;
}

static void arena_free(Arena* arena) {
    ArenaBlock* block = arena -> start;

    while (block != NULL) {
        ArenaBlock* previous = block;
        block = block -> next;
        free_block(previous);
    }

    arena -> start = NULL;
    arena -> end = NULL;
}

static size_t total_capacity(Arena* arena) {
    ArenaBlock* current = arena -> start;
    size_t total = 0;

    while (current != NULL) {
        total += current -> capacity;
        current = current -> next;
    }
    
    return total;
}

static size_t total_usage(Arena* arena) {
    ArenaBlock* current = arena -> start;
    size_t total = 0;

    while (current != NULL) {
        total += current -> usage;
        current = current -> next;
    }
    
    return total;
}

#ifdef __cplusplus 
}
#endif

#endif // !ARENA_H
