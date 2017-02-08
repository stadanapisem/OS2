//
// Created by Miljan on 01-Jan-17.
//

#include <cstdio>
#include <cstring>

#include "slab.h"
#include "buddy.h"
#include "errors.h"

using namespace std;

static small_caches_t cache_sizes[]{
        {32,     NULL},
        {64,     NULL},
        {128,    NULL},
        {256,    NULL},
        {512,    NULL},
        {1024,   NULL},
        {2048,   NULL},
        {4096,   NULL},
        {8192,   NULL},
        {16384,  NULL},
        {32768,  NULL},
        {65536,  NULL},
        {131072, NULL}
};

static kmem_cache_t cache_cache = {
        "kmem_cache",
        sizeof(kmem_cache_t) + sizeof(void *),
};

kmem_cache_t *cache_list;

void kmem_init(void *space, int block_num) {
    init(space, block_num);
    cache_list = &cache_cache;
    kmem_cache_init();
    kmem_cache_sizes_init();

}

static inline unsigned L1_cache_align(int x) {
    return (unsigned) ((x + CACHE_L1_LINE_SIZE - 1) & ~(CACHE_L1_LINE_SIZE - 1));
}

static void kmem_cache_estimate(unsigned order, size_t size, size_t *left_over, unsigned *num, bool mngroffslab) {
    size_t wastage = BLOCK_SIZE << order, extra = 0, base = 0;

    if (!mngroffslab)
        extra = sizeof(kmem_bufctl_t), base = sizeof(slab_t);

    int i = 0;
    for (i = 0; i * size + L1_cache_align(base + i * extra) <= wastage; i++);

    if (i > 0)
        i--;

    *num = (unsigned) i;
    wastage -= i * size;
    wastage -= L1_cache_align(base + i * extra);
    *left_over = wastage;

}

static void kmem_cache_sizes_init() {
    char name[MAXNAMESIZE];

    for (int i = 0; i < 13; i++) {
        memset(name, 0, sizeof(name));
        sprintf(name, "size-%d", cache_sizes[i].cache_size);
        cache_sizes[i].small_cache = kmem_cache_create(name, cache_sizes[i].cache_size, NULL, NULL);

        if (!cache_sizes[i].small_cache)
            exit(-1);
    }
}

static void kmem_cache_init() {
    size_t left_over;
    cache_cache.slab_free = cache_cache.slab_full = cache_cache.slab_partial = NULL;
    cache_cache.slab_cache = NULL;
    cache_cache.growing = false;
    cache_cache.grown_since = false;
    cache_cache.mngroffslab = false;
    cache_cache.lock = new(cache_cache.lock_mem) mutex();


    kmem_cache_estimate(0, cache_cache.object_size, &left_over, &cache_cache.numobj, cache_cache.mngroffslab);

    if (!cache_cache.numobj)
        exit(-3);

    cache_cache.offset = left_over / CACHE_L1_LINE_SIZE;
    cache_cache.next_offset = 0;
}

kmem_cache_t *kmem_cache_create(const char *name, size_t size, void (*ctor)(void *), void (*dtor)(void *)) {
    kmem_cache_t *tmpcache = NULL;
    size += sizeof(void *); // Add padding for pointer to slab_t*; Not smart, but does the job...

    if (!name || (strlen(name) >= MAXNAMESIZE - 1)) { // ERROR id 0
        get_error(0);
        return NULL;
    }

    tmpcache = (kmem_cache_t *) kmem_cache_alloc(&cache_cache);

    if (!tmpcache) { // ERROR id 1
        cache_cache.error_code = 1;
        return NULL;
    }

    memset(tmpcache, 0, sizeof(kmem_cache_t));

    bool mngroffslab = false;

    if (size >= (BLOCK_SIZE >> 3))
        mngroffslab = true;

    size_t left_over;
    do {

        kmem_cache_estimate(tmpcache->order, size, &left_over, &tmpcache->numobj, mngroffslab);

        if (!tmpcache->numobj) {
            tmpcache->order++;
            continue;
        }

        if (tmpcache->order > 6) { // An object larger than 2^6 pages, I don't think so...
            kmem_cache_estimate(tmpcache->order = 6, size, &left_over, &tmpcache->numobj, mngroffslab);
            break;
        }

        if ((left_over * 8) <= (BLOCK_SIZE << tmpcache->order)) // Acceptable fragmentation according to J. Bonwick
            break;

        tmpcache->order++;
    } while (1);

    if (!tmpcache->numobj) { // ERROR id 2
        kmem_cache_free(&cache_cache, tmpcache);
        tmpcache->error_code = 2;
        return NULL;
    }

    tmpcache->offset = left_over / CACHE_L1_LINE_SIZE;
    tmpcache->next_offset = 0;
    tmpcache->object_size = size;
    tmpcache->slab_free = tmpcache->slab_full = tmpcache->slab_partial = NULL;
    tmpcache->growing = false;
    tmpcache->grown_since = false;
    tmpcache->error_code = 255;

    if (mngroffslab)
        tmpcache->slab_cache = kmem_find_general_cache(
                L1_cache_align(tmpcache->numobj * sizeof(kmem_bufctl_t) + sizeof(slab_t)));

    tmpcache->constructor = ctor;
    tmpcache->destructor = dtor;
    strncpy(tmpcache->name, name, MAXNAMESIZE);
    tmpcache->mngroffslab = mngroffslab;
    tmpcache->lock = new(tmpcache->lock_mem) mutex();

    lock(cache_list_lock);

    tmpcache->next = cache_list;
    cache_list = tmpcache;

    unlock(cache_list_lock);

    return tmpcache;
}

static inline kmem_bufctl_t *slab_bufctl(slab_t *slabp) {
    return (kmem_bufctl_t *) (slabp + 1);
}

#define OBJ_ADDR(x) ((x) + sizeof(void *))
#define SLAB_ADDR(x) ((x) - sizeof(void *))

void *kmem_cache_alloc(kmem_cache_t *cachep) {
    lock(cachep->lock);
    while (1) {
        slab_t *partial = cachep->slab_partial;
        if (!partial) {
            slab_t *free = cachep->slab_free;

            if (!free) {
                kmem_cache_grow(cachep);
                continue;
            } else {
                cachep->slab_free = free->next;
                free->next = cachep->slab_partial;
                cachep->slab_partial = free;
                partial = cachep->slab_partial;
            }
        }

        slab_t *slabp = (slab_t *) ((char *) partial - (unsigned) (&((slab_t *) 0)->next));
        void *objp;

        slabp->inuse++;
        objp = slabp->mem + slabp->free * cachep->object_size;
        slabp->free = slab_bufctl(slabp)[slabp->free];

        if (slabp->free == BUFCTLEND) {
            slab_t *last = NULL;
            for (auto i = cachep->slab_partial; i; i = i->next) {
                if (i == partial) { // Transfer slab from partial to full cache_list
                    if (last) {
                        last->next = partial->next;
                    } else cachep->slab_partial = partial->next;

                    partial->next = cachep->slab_full;
                    cachep->slab_full = partial;
                    break;
                }
                last = i;
            }

        }
        unlock(cachep->lock);
        return OBJ_ADDR(objp);
    }
}

static void kmem_cache_grow(kmem_cache_t *cachep) {
    void *objp = buddy_alloc(cachep->order);
    if (!objp) { // ERROR id 3
        cachep->error_code = 3;
        return;
    }

    size_t offset = cachep->next_offset * cachep->object_size;
    cachep->next_offset++;
    if (cachep->next_offset >= cachep->offset)
        cachep->next_offset = 0;

    cachep->growing = true;


    slab_t *slabp;
    if (cachep->mngroffslab) {
        slabp = (slab_t *) kmem_cache_alloc(cachep->slab_cache);

        if (!slabp) { // ERROR id 4
            buddy_free(objp, cachep->order);
            cachep->error_code = 4;
            return;
        }

    } else {
        slabp = (slab_t *) objp + offset;
        offset += L1_cache_align(cachep->numobj * sizeof(kmem_bufctl_t) + sizeof(slab_t));
    }

    slabp->offset = offset;
    slabp->mem = objp + slabp->offset;
    slabp->inuse = 0;


    unsigned i;

    for (i = 0; i < cachep->numobj; i++) {
        void *objp = slabp->mem + cachep->object_size * i;
        *((unsigned long *) objp) = (unsigned long) slabp; // Set slab_t * at beginning of each object
        if (cachep->constructor)
            cachep->constructor(OBJ_ADDR(objp));

        slab_bufctl(slabp)[i] = (kmem_bufctl_t) (i + 1);
    }

    slab_bufctl(slabp)[i - 1] = (unsigned char) BUFCTLEND;
    slabp->free = 0;

    cachep->growing = false;
    slabp->next = cachep->slab_free;
    cachep->slab_free = slabp;
    slabp->my_cache = cachep;

    cachep->grown_since = true; // Update grown flag, when cache has grown.
}

static kmem_cache_t *kmem_find_general_cache(size_t size) {
    for (int i = 0; i < 13; i++) {
        if (size > cache_sizes[i].cache_size)
            continue;

        return cache_sizes[i].small_cache;
    }
}

static void kmem_slab_destroy(kmem_cache_t *cachep, slab_t *slabp, slab_t **last) {
    if (!cachep || !slabp)
        return;

    *last = slabp->next;

    if (cachep->destructor) {
        for (int i = 0; i < cachep->numobj; i++)
            cachep->destructor(OBJ_ADDR(slabp->mem + cachep->object_size * i));
    }

    buddy_free(slabp->mem - slabp->offset, cachep->order);

    if (cachep->mngroffslab)
        kmem_cache_free(cachep->slab_cache, slabp);
}

void kmem_cache_free(kmem_cache_t *cachep, void *objp) {
    if (!cachep || !objp)
        return;

    lock(cachep->lock);

    slab_t *slabp = (slab_t *) (void *) *(unsigned long *) SLAB_ADDR(objp);
    unsigned objnr = ((unsigned) objp - (unsigned) slabp->mem) / cachep->object_size;
    slab_bufctl(slabp)[objnr] = slabp->free;
    slabp->free = (kmem_bufctl_t) objnr;

    if(cachep->constructor)
        cachep->constructor(objp);

    slabp->inuse--;
    if (!slabp->inuse) { // Was full (in case there is only 1 element) or partial, now is free
        slab_t *last = NULL;
        for (auto i = cachep->slab_full; i; i = i->next) {
            if (i == slabp) {
                if (!last)
                    cachep->slab_full = i->next;
                else last->next = i->next;
                i->next = cachep->slab_free;
                cachep->slab_free = i;
                break;
            }
            last = i;
        }
        last = NULL;
        for (auto i = cachep->slab_partial; i; i = i->next) {
            if (i == slabp) {
                if (!last)
                    cachep->slab_partial = i->next;
                else last->next = i->next;
                i->next = cachep->slab_free;
                cachep->slab_free = i;
                break;
            }
            last = i;
        }
    } else if (slabp->inuse + 1 == cachep->numobj) { // Was full, now partial
        slab_t *last = NULL;
        for (auto i = cachep->slab_full; i; i = i->next) {
            if (i == slabp) {
                if (!last)
                    cachep->slab_full = i->next;
                else last->next = i->next;
                i->next = cachep->slab_partial;
                cachep->slab_partial = i;
                break;
            }
            last = i;
        }
    }

    unlock(cachep->lock);
}

void kmem_cache_destroy(kmem_cache_t *cachep) {
    if (cachep == &cache_cache)
        return;

    lock(cachep->lock);

    while (cachep->slab_full)
        kmem_slab_destroy(cachep, cachep->slab_full, &cachep->slab_full);

    while (cachep->slab_partial)
        kmem_slab_destroy(cachep, cachep->slab_partial, &cachep->slab_partial);

    while (cachep->slab_free)
        kmem_slab_destroy(cachep, cachep->slab_free, &cachep->slab_free);

    unlock(cachep->lock);

    kmem_cache_free(&cache_cache, cachep);

    lock(cache_list_lock);

    kmem_cache_t *last = NULL;
    for (auto i = cache_list; i; i = i->next) {
        if (i == cachep) {
            if (!last)
                cache_list = i->next;
            else
                last->next = i->next;
            break;
        }
        last = i;
    }

    unlock(cache_list_lock);
}

int kmem_cache_shrink(kmem_cache_t *cachep) {
    lock(cachep->lock);

    int ret = 0;
    if (!cachep->grown_since && !cachep->growing) { // If hasn't grown since last shrinking. Shrink it!
        for (auto i = cachep->slab_free; i; i = i->next) {
            kmem_slab_destroy(cachep, i, &cachep->slab_free);
            ret++;
        }
    }

    if (!ret)
        cachep->grown_since = false; // Cache hasn't grown since last shrink

    unlock(cachep->lock);
    return ret;
}

void *kmalloc(size_t size) {
    for (int i = 0; i < 13; i++) {
        if (cache_sizes[i].cache_size < size)
            continue;

        return kmem_cache_alloc(cache_sizes[i].small_cache);
    }

    return NULL;
}

void kfree(const void *objp) {
    if (!objp)
        return;

    slab_t *slabp = (slab_t *) (void *) *(unsigned long *) SLAB_ADDR(objp);
    kmem_cache_t *c = (slabp)->my_cache;
    kmem_cache_free(c, (void *) objp);
}

void kmem_cache_info(kmem_cache_t *cachep) {
    unsigned num_slabs = 0, num_objs = 0;

    for (auto i = cachep->slab_partial; i; i = i->next)
        num_slabs++, num_objs += i->inuse;

    for (auto i = cachep->slab_full; i; i = i->next)
        num_slabs++, num_objs += i->inuse;

    for (auto i = cachep->slab_free; i; i = i->next)
        num_slabs++;

    if (!num_slabs)
        printf("Name\tObject size\tCache size\tSlab num.\tObject num.\tOccupance%%\n%s\t%u\t%u\t%u\t%u\t\n",
               cachep->name,
               cachep->object_size, 1 << cachep->order, num_slabs,
               cachep->numobj);

    else
        printf("Name\tObject size\tCache size\tSlab num.\tObject num.\tOccupance%%\n%s\t%u\t%u\t%u\t%u\t%.2f%%\n",
               cachep->name,
               cachep->object_size, 1 << cachep->order, num_slabs,
               cachep->numobj, (float) 100 * num_objs / (num_slabs * cachep->numobj));
    //  printf("%.2f%%\n", );

}

int kmem_cache_error(kmem_cache_t *cachep) {

    if (cachep->error_code == 255)
        return 0;
    else
        get_error(cachep->error_code);

    int ret = cachep->error_code;
    cachep->error_code = 255;
    return ret;
}