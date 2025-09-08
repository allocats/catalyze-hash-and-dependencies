#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "arena.h"

#include <stdint.h>
#include <sys/stat.h>

typedef struct Node {
    uint32_t content_hash;
    char* path;
    char* filename;
    char** dependencies;
    uint8_t dep_count;
    uint8_t dep_capacity;
    struct Node* next;
} Node;

typedef struct {
    Node** nodes;
    uint8_t count;
    uint8_t capacity;
} HashTable;

uint32_t hash_string(const char* str);
char* extract_name(Arena* arena, const char* path);

HashTable* create_hashtable(Arena* arena, uint8_t capacity);
uint32_t insert_hashtable(Arena* arena, HashTable* ht, const char* path, const char* buffer, struct stat st);
uint32_t add_dependency(Arena* arean, HashTable* ht, const char* dest, const char* src, const char* buffer, struct stat st);

Node* search_path(HashTable* ht, const char* path);
Node* search_name(HashTable* ht, const char* name);

void print_hashtable(HashTable* ht);

#endif // !HASHTABLE_H
