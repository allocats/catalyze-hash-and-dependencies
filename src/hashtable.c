#include "hashtable.h"

#include "arena.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t hash_path(const char* path) {
    const char* slash = strrchr(path, '/');
    path = slash ? slash + 1 : path;

    uint32_t hash = 5381;
    for (const char* p = path; *p; p++) {
        hash = ((hash << 5) + hash) ^ (unsigned char)*p;
    }

    return hash;
}

static inline size_t align_capacity(size_t capacity) {
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

Node* get_ht(HashTable* ht, const char* path) {
    uint32_t hash = hash_path(path);
    size_t idx = hash & (ht -> capacity - 1);
    Node* node = ht -> nodes[idx];

    while (node) {
        if (strcmp(node -> path, path) == 0) {
            return node;
        }
        node = node -> next;
    }

    return NULL;
}

Node* insert_ht(HashTable* ht, const char* path, uint32_t content_hash) {
    if (ht -> count >= ht -> capacity) {
        ht -> nodes = arena_realloc(ht -> arena, ht -> nodes, sizeof(Node*) * ht -> capacity, sizeof(Node*) * ht -> capacity * 2);
        ht -> capacity *= 2;

        if (!ht -> nodes) {
            return NULL;
        }
    }

    uint32_t hash = hash_path(path);
    size_t idx = hash & (ht -> capacity - 1);
    Node* node = ht -> nodes[idx];

    while (node) {
        if (strcmp(node -> path, path) == 0) {
            node -> content_hash = content_hash;
            return node;
        }
        node = node -> next;
    }

    node = create_node(ht -> arena, path, content_hash);
    if (!node) {
        return NULL;
    }

    node -> next = ht -> nodes[idx];
    ht -> nodes[idx] = node;
    ht -> count++;

    return node;
}

static int node_add_dependency(Arena* arena, Node* src, Node* dep) {
    if (src -> dep_count >= src -> dep_capacity) {
        src -> dependencies = arena_realloc(arena, src -> dependencies, sizeof(Node) * src -> dep_capacity, sizeof(Node) * src -> dep_capacity * 2);

        if (!src -> dependencies) {
            return -1;
        }

        src -> dep_capacity *= 2;
    }

    src -> dependencies[src -> dep_count++] = dep;
    return 0;
}

int add_dependency(HashTable* ht, const char* file, const char* include) {
    Node* file_node = get_ht(ht, file);
    Node* include_node = get_ht(ht, include);

    if (!file_node) {
        file_node = insert_ht(ht, file, 0);
    }

    if (!include_node) {
        include_node = insert_ht(ht, include, 0);
    }

    if (!file_node || !include_node) {
        return -1;
    }

    return node_add_dependency(ht -> arena, file_node, include_node);
}

void print_hashtable(HashTable* ht) {
    printf("\n=== HashTable ===\n\n");
    printf("Stats:\n");
    printf("  Count: %zu\n", ht -> count);
    printf("  Capacity: %zu\n", ht -> capacity);

    for (int i = 0; i < ht -> capacity; i++) {
        Node* node = ht -> nodes[i];
        while (node) {
            printf("\nNode %d:\n", i);
            printf("  Name: %s\n", node -> name);
            printf("  Path: %s\n", node -> path);
            printf("  Content-Hash: %x\n\n", node -> content_hash);

            if (node -> dep_count > 0) {
                printf("  Dependencies:\n");

                for (int k = 0; k < node -> dep_count; k++) {
                    printf("    %d. %s\n", k, node -> dependencies[k] -> name);
                    printf("      Path: %s\n", node -> dependencies[k] -> path);
                }
            }

            node = node -> next;
        }
    }
    
    printf("\n=== End of HT ===\n");
}
