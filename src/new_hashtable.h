#ifndef NEW_HASHTABLE_H
#define NEW_HASHTABLE_H

#include "arena.h"

#include <stdint.h>

// Note: The index will be bounded to the filename, for example test/foo.h = foo.h, and that gets hashed
typedef struct Node {
    char* path;
    char* name;
    uint32_t content_hash;
    size_t dep_count;
    size_t dep_capacity;
    struct Node** dependencies;
    struct Node* next;
} Node;

typedef struct {
    Arena* arena;
    Node** nodes;
    size_t count;
    size_t capacity;
} HashTable;

HashTable* create_hashtable(Arena* arena, size_t capacity);

#endif // !HASHTABLE_H
