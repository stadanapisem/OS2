//
// Created by Miljan on 08-Jan-17.
//

#include <cstdio>
#include <mutex>
#include <cstring>

#include "errors.h"

using namespace std;

void lock(mutex *lock) {
    lock->lock();
}

void unlock(mutex *lock) {
    lock->unlock();
}

/*  Error Message codes and texts
 *
 *  0) Invalid Arguments
 *  1) Can't allocate cache object
 *  2) Cache with zero objects
 *  3) Not enough memory
 *  4) Can't allocate slab manager
 */
void *errors;

void error_init(void *memory) {
    errors = memory;
    char *tmpe = (char *) errors;

    strncpy(tmpe, "Invalid Arguments", ERRORMSGLEN);
    tmpe += sizeof(char) * ERRORMSGLEN;
    strncpy(tmpe, "Can't allocate cache object", ERRORMSGLEN);
    tmpe += sizeof(char) * ERRORMSGLEN;
    strncpy(tmpe, "Cache with zero objects", ERRORMSGLEN);
    tmpe += sizeof(char) * ERRORMSGLEN;
    strncpy(tmpe, "Not enough memory", ERRORMSGLEN);
    tmpe += sizeof(char) * ERRORMSGLEN;
    strncpy(tmpe, "Can't allocate slab manager", ERRORMSGLEN);

    memory = tmpe + sizeof(char) * ERRORMSGLEN;
}

void get_error(unsigned id) {
    if (id >= NUMOFERRORS)
        return;

    for (int i = 0; i < ERRORMSGLEN; i++) {
        char c = *((char *) (errors + id * ERRORMSGLEN + i));
        if (c == '\0')
            break;
        else
            printf("%c", *((char *) (errors + id * ERRORMSGLEN + i)));
    }
    puts("");
}