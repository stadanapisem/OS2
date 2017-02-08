//
// Created by Miljan on 01-Jan-17.
//

#ifndef OS2_SLAB_H
#define OS2_SLAB_H

#include <stdlib.h>
#include <mutex>

using namespace std;

#define BLOCK_SIZE (4096)
#define CACHE_L1_LINE_SIZE (64)
#define MAXNAMESIZE (20)
#define BUFCTLEND (255)

typedef unsigned char kmem_bufctl_t;
struct slab_t;

struct kmem_cache_t {
    char name[MAXNAMESIZE];
    size_t object_size;
    unsigned order, numobj, next_offset, offset; // 2^order pages is used by cache
    char lock_mem[sizeof(mutex)];
    mutex *lock;
    bool mngroffslab, grown_since, growing;

    void (*constructor)(void *);

    void (*destructor)(void *);

    slab_t *slab_full, *slab_partial, *slab_free;
    kmem_cache_t *slab_cache; // Slab descriptor is kept off-slab, when object size is
                              // larger than 512B
    kmem_cache_t *next; // Next cache in cache_list of caches
    unsigned error_code; // Last error that happened to this cache
};

struct slab_t {
    slab_t *next;
    kmem_cache_t *my_cache;
    unsigned inuse;
    size_t offset;
    void *mem;
    kmem_bufctl_t free;
};

struct small_caches_t {
    size_t cache_size;
    kmem_cache_t *small_cache;
};

void kmem_init(void *space, int block_num);

static void kmem_cache_sizes_init();

static void kmem_cache_init();

static kmem_cache_t *kmem_find_general_cache(size_t size);

static void kmem_cache_grow(kmem_cache_t *cachep);

kmem_cache_t *
kmem_cache_create(const char *name, size_t size, void (*ctor)(void *), void (*dtor)(void *)); // Allocate cache

int kmem_cache_shrink(kmem_cache_t *cachep); // Shrink cache

void *kmem_cache_alloc(kmem_cache_t *cachep); // Allocate one object from cache

void kmem_cache_free(kmem_cache_t *cachep, void *objp); // Deallocate one object from cache

void *kmalloc(size_t size); // Alloacate one small memory buffer

void kfree(const void *objp); // Deallocate one small memory buffer

void kmem_cache_destroy(kmem_cache_t *cachep); // Deallocate cache

void kmem_cache_info(kmem_cache_t *cachep); // Print cache info

int kmem_cache_error(kmem_cache_t *cachep); // Print error message

extern kmem_cache_t *cache_list;
#endif //OS2_SLAB_H
