#include "hashtable.h"

#include "arena.h"

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

uint32_t hash_string(const char* str) {
    uint32_t hash = 5381;
    for (const char* p = str; *p; p++) {
        hash = ((hash << 5) + hash) + (unsigned char)*p;
    }
    return hash;
}

static uint32_t hash_content(const char* buffer, const char* name, struct stat st) {
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

static uint32_t hash_file(const char* file) {
    int fd = open(file, O_RDONLY);
    if (fd == -1) {
        return 0;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        close(fd);
        return 0;
    }

    char* buffer = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buffer == MAP_FAILED) {
        close(fd);
        return 0;
    }

    uint32_t hash = hash_content(buffer, file, st);

    munmap(buffer, st.st_size);
    close(fd);
    return hash;
}

static char* extract_name(Arena* arena, const char* path) {
    const char* last_slash = strrchr(path, '/');
    const char* start = last_slash ? last_slash + 1 : path;
    return arena_strdup(arena, start);
}

static Node* create_node(Arena* arena, const char* path) {
    Node* node = arena_alloc(arena, sizeof(Node));

    if (!node) {
        return NULL;
    }

    node -> path = arena_strdup(arena, path);
    node -> filename = extract_name(arena, path);
    node -> content_hash = hash_file(path);

    node -> dependencies = arena_array_zero(arena, char*, 4);
    node -> dep_count = 0;
    node -> dep_count = 4;

    node -> next = NULL;

    if (node -> content_hash == 0 || !node -> path || !node -> filename || !node -> dependencies) {
        return NULL;
    }

    return node;
}

HashTable* create_hashtable(Arena* arena, uint8_t capacity) {
    HashTable* ht = arena_alloc(arena, sizeof(HashTable));

    if (!ht) {
        return NULL;
    }

    ht -> count = 0;
    ht -> capacity = capacity;
    ht -> nodes = arena_array_zero(arena, Node*, capacity);

    if (!ht -> nodes) {
        return NULL;
    }

    return ht;
}

uint32_t insert_hashtable(Arena* arena, HashTable* ht, const char* path) {
    uint32_t hash = hash_string(path);
    uint32_t idx = hash % ht -> capacity;

    Node* current = ht -> nodes[idx];
    while (current) {
        if (strcmp(current -> path, path) == 0) {
            current -> content_hash = hash_file(path);
            return 0;
        }
        current = current -> next;
    }

    Node* node = create_node(arena, path);
    if (!node) {
        return 1;
    }

    node -> next = ht -> nodes[idx];
    ht -> nodes[idx] = node;
    ht -> count++;
    return 0;
}

Node* search_path(HashTable* ht, const char* path) {
    uint32_t hash = hash_string(path);
    uint32_t idx = hash % ht -> capacity;
    
    Node* node = ht -> nodes[idx];
    while (node) {
        if (strcmp(node -> path, path) == 0) {
            return node;
        }

        node = node -> next;
    }

    return NULL;
}

Node* search_name(HashTable* ht, const char* name) {
    for (int i = 0; i < ht -> capacity; i++) {
        Node* current = ht -> nodes[i];
        while (current) {
            if (strcmp(current -> filename, name) == 0) {
                return current;
            }

            current = current -> next;
        }
    }

    return NULL;
}

static uint32_t node_add_dependency(Arena* arena, HashTable* ht, Node* node, const char* src) {
    for (uint8_t i = 0; i < node -> dep_count; i++) {
        if (strcmp(node -> dependencies[i], src) == 0) {
            return 1;
        }
    }

    if (node -> dep_count >= node -> dep_capacity) {
        node -> dependencies = arena_realloc(arena, node -> dependencies, node -> dep_capacity, node -> dep_capacity * 2);
        if (!node -> dependencies) {
            return 1;
        }

        node -> dep_capacity *= 2;
    }

    node -> dependencies[node -> dep_count] = arena_strdup(arena, src); 
    if (!node -> dependencies[node -> dep_count]) {
        return 1;
    }

    node -> dep_count++;
    return 0;
}

uint32_t add_dependency(Arena* arena, HashTable* ht, const char* dest, const char* src) {
    Node* node = search_path(ht, dest);
    if (!node) {
        if (insert_hashtable(arena, ht, src) != 0) {
            return 1;
        }

        node = search_path(ht, dest);
    }

    return node_add_dependency(arena, ht, node, src);
}
