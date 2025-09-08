#include "new_hashtable.h"

#include "arena.h"

#include <string.h>

static size_t align_capacity(size_t capacity) {
    return 1 << (32 - __builtin_clz(capacity - 1));
}

static Node* create_node(Arena* arena, const char* path, uint32_t content_hash) {
    Node* node = arena_alloc(arena, sizeof(*node));
    if (!node) {
        return NULL;
    }

    node -> path = arena_strdup(arena, path);
    const char* slash = strrchr(path, '/');
    node -> name = arena_strdup(arena, (slash ? slash + 1 : path));

    node -> content_hash = content_hash;
    node -> dep_count = 0;
    node -> dep_capacity = 2;

    node -> dependencies = arena_array_zero(arena, Node*, node -> dep_capacity);
    node -> next = NULL;

    if (!node -> dependencies) {
        return NULL;
    }
    
    return node;
}

HashTable* create_hashtable(Arena* arena, size_t capacity) {
    HashTable* ht = arena_alloc(arena, sizeof(*ht));
    if (!ht) {
        return NULL;
    }

    ht -> arena = arena;
    ht -> count = 0;
    ht -> capacity = align_capacity(capacity);
    ht -> nodes = arena_array_zero(arena, Node*, ht -> capacity);

    if (!ht -> nodes) {
        return NULL;
    }

    return ht;
}
