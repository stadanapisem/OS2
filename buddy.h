//
// Created by Miljan on 31-Dec-16.
//

#ifndef OS2_BUDDY_H
#define OS2_BUDDY_H

#include "slab.h"

struct buddy_block_t {
    unsigned order;
    buddy_block_t *next;
};

struct buddy_t {
    buddy_block_t *next;
    unsigned char *tag;
};

int buddy_initialize(unsigned base_addr, unsigned order, unsigned already_occupied);

void init(void *space, int block_num);

void *buddy_alloc(unsigned order);

void buddy_free(void *addr, unsigned order);

void debug_info();

extern mutex *cache_list_lock;

#endif //OS2_BUDDY_H
