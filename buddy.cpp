//
// Created by Miljan on 31-Dec-16.
//

#include <cstdlib>
#include <cstdio>
#include <cstring>

#include "buddy.h"
#include "errors.h"

using namespace std;

#define GET_ORDER(x) (x)

buddy_t buddy[20];
mutex *buddy_lock, *cache_list_lock;
unsigned buddy_max_order = 0, start_address = 0;
void *memory;

static unsigned count_trailing_zeros(int num) {
    unsigned res = 0;
    num = (num ^ (num - 1)) >> 1; // trailing 0s to 1s rest 0s

    for (; num; res++)
        num >>= 1;

    return res;
}

static unsigned round_high_power2(int num) {
    num--;
    num |= num >> 1;
    num |= num >> 2;
    num |= num >> 4;
    num |= num >> 8;
    num |= num >> 16;
    num++;
    num += (num == 0);

    return (unsigned) num;
}

inline static unsigned buddy_to_id(buddy_block_t *bud, unsigned order) {
    return ((((unsigned) bud - start_address) / BLOCK_SIZE) / (1 << order));
}

inline static void set_tag_bit(unsigned char *tag, unsigned id) {
    *(tag + (id >> 3)) |= 1 << (id & 7);
}

inline static void clear_tag_bit(unsigned char *tag, unsigned id) {
    *(tag + (id >> 3)) &= ~(1 << (id & 7));
}

inline static bool test_tag_bit(unsigned char *tag, unsigned id) {
    return (bool) (*(tag + (id >> 3)) & (1 << (id & 7)));
}

inline static void *find_buddy(buddy_block_t *bud, unsigned order) {
    return (void *) (((((unsigned long) bud - start_address) / BLOCK_SIZE) ^ (1UL << order)) * BLOCK_SIZE +
                     start_address);
}

int buddy_initialize(unsigned base_addr, unsigned order, unsigned already_occupied) {
    buddy_max_order = order;
    start_address = base_addr;
    //printf("Already %u\n", already_occupied);

    buddy[GET_ORDER(0)].next = NULL;

    buddy_block_t *tmpbuddy = (buddy_block_t *) base_addr;
    tmpbuddy->order = order;
    tmpbuddy->next = NULL;


    for (unsigned i = order; i > 0; i--) {
        unsigned tag_bits = 1U << (order - i);
        buddy[GET_ORDER(i)].tag = (unsigned char *) memory;
        buddy[GET_ORDER(i)].next = NULL;

        memory += ((tag_bits + 7) >> 3) * sizeof(unsigned char);
    }
    buddy[GET_ORDER(order)].next = tmpbuddy;
    buddy[GET_ORDER(0)].tag = (unsigned char *) memory;
    memory += (((1U << order) + 7) >> 3) * sizeof(unsigned char);

    //printf("FREE BYTES: %u\n", base_addr - (unsigned) memory);

    if (already_occupied) {
        unsigned tmporder = buddy_max_order - 1;
        while (1) {
            set_tag_bit(buddy[GET_ORDER(tmporder + 1)].tag,
                        buddy_to_id(buddy[GET_ORDER(tmporder + 1)].next, tmporder + 1));

            buddy_block_t *tmpbuddy_left = (buddy_block_t *) (
                    buddy_to_id(buddy[GET_ORDER(tmporder + 1)].next, tmporder) * (BLOCK_SIZE
                            << tmporder) + start_address);

            buddy_block_t *tmpbuddy_right = (buddy_block_t *) ((unsigned) tmpbuddy_left + (BLOCK_SIZE << tmporder));

            buddy[GET_ORDER(tmporder + 1)].next = buddy[GET_ORDER(tmporder + 1)].next->next;
            if ((already_occupied & (1 << tmporder)) == already_occupied) {
                //set_tag_bit(buddy[GET_ORDER(tmporder)].tag, buddy_to_id(tmpbuddy_left));
                buddy[GET_ORDER(tmporder)].next = tmpbuddy_left;
                tmpbuddy_left->next = NULL;
                tmpbuddy_left->order = tmporder;
                set_tag_bit(buddy[GET_ORDER(tmporder)].tag, buddy_to_id(tmpbuddy_right, tmporder));
                break;
            }

            if (!(already_occupied & (1 << tmporder))) {
                tmpbuddy_left->next = NULL;
                tmpbuddy_left->order = tmpbuddy_right->order = tmporder;
                tmpbuddy_right->next = tmpbuddy_left;
                buddy[GET_ORDER(tmporder)].next = tmpbuddy_right;
            } else if ((already_occupied & (1 << tmporder))) {
                tmpbuddy_left->next = NULL;
                buddy[GET_ORDER(tmporder)].next = tmpbuddy_left;
                set_tag_bit(buddy[GET_ORDER(tmporder)].tag, buddy_to_id(tmpbuddy_right, tmporder));
                already_occupied &= ~(1 << tmporder);
            }

            tmporder--;
        }
    }

    //debug_info();

    return 0;
}

static void *__buddy_alloc(unsigned order) {
    buddy_block_t *ret;

    for (unsigned j = order; j <= buddy_max_order; j++) {
        if (!buddy[GET_ORDER(j)].next)
            continue;

        ret = buddy[GET_ORDER(j)].next;
        buddy[GET_ORDER(j)].next = ret->next;
        set_tag_bit(buddy[GET_ORDER(j)].tag, buddy_to_id(ret, j));

        while (j > order) {
            j--;

            buddy_block_t *tmpbuddy = (buddy_block_t *) ((unsigned) ret +
                                                         (BLOCK_SIZE << j));
            tmpbuddy->order = j;
            clear_tag_bit(buddy[GET_ORDER(j)].tag, buddy_to_id(tmpbuddy, j));
            tmpbuddy->next = buddy[GET_ORDER(j)].next;
            buddy[GET_ORDER(j)].next = tmpbuddy;
        }

        ret->order = order;
        set_tag_bit(buddy[GET_ORDER(order)].tag, buddy_to_id(ret, order));
        return ret;
    }

    return NULL;
}

static void __buddy_free(void *addr, unsigned order) {
    buddy_block_t *tmpbuddy = (buddy_block_t *) addr;

    while (order < buddy_max_order) {
        buddy_block_t *my_buddy = (buddy_block_t *) find_buddy(tmpbuddy, order);

        if (test_tag_bit(buddy[GET_ORDER(order)].tag, buddy_to_id(my_buddy, order)))
            break;

        if (my_buddy->order != order)
            break;

        buddy_block_t *last = NULL;

        for (buddy_block_t *i = buddy[GET_ORDER(order)].next; i; i = i->next) {
            if (i == my_buddy) {
                if (!last)
                    buddy[GET_ORDER(order)].next = my_buddy->next;
                else
                    last->next = my_buddy->next;

                break;
            }
            last = i;
        }

        if (my_buddy < tmpbuddy)
            tmpbuddy = my_buddy;

        order++;
        tmpbuddy->order = order;
    }

    tmpbuddy->order = order;
    clear_tag_bit(buddy[GET_ORDER(order)].tag, buddy_to_id(tmpbuddy, order));
    tmpbuddy->next = buddy[GET_ORDER(order)].next;
    buddy[GET_ORDER(order)].next = tmpbuddy;
}

void debug_info() {
    printf("DEBUG\nMax Order: %d\n", buddy_max_order);

    for (unsigned i = 0; i <= buddy_max_order; i++) {
        unsigned num_blocks = 0;
        for (auto j = buddy[GET_ORDER(i)].next; j; j = j->next) {
            num_blocks++;
        }

        printf("Order: %d\t%u free blocks\t%u\n", i, num_blocks, start_address);
    }
}

void *buddy_alloc(unsigned order) {
    lock(buddy_lock);

    void *ret = __buddy_alloc(order);

    unlock(buddy_lock);

    return ret;
}

void buddy_free(void *addr, unsigned order) {
    lock(buddy_lock);

    __buddy_free(addr, order);

    unlock(buddy_lock);
}

void init(void *space, int block_num) {
    if (block_num <= 2) {
        printf("Need more memory");
        exit(-1);
    }

    memset(space, 0, (size_t) BLOCK_SIZE * block_num);

    bool waste_block = 0;
    while ((unsigned) space % BLOCK_SIZE != 0)
        space++, waste_block = 1;

    if (waste_block)
        block_num--;

    block_num -= 2; // I need to store my variables somewhere, and also ditch last block for alignment

    if (block_num & (block_num - 1) == 0)
        memory = space + count_trailing_zeros(block_num) * sizeof(buddy_t);
    else
        memory = space + count_trailing_zeros(round_high_power2(block_num)) * sizeof(buddy_t);

    space = space + 4096;

    buddy_initialize((unsigned) space, count_trailing_zeros(round_high_power2(block_num)),
                     round_high_power2(block_num) - block_num);

    buddy_lock = new(memory) mutex();
    memory += sizeof(mutex *);

    cache_list_lock = new(memory) mutex();
    memory += sizeof(mutex *);

    error_init(memory);
}