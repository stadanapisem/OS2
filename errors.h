//
// Created by Miljan on 08-Jan-17.
//

#ifndef OS2_ERRORS_H
#define OS2_ERRORS_H

#include <mutex>

using namespace std;

#define ERRORMSGLEN 30
#define NUMOFERRORS 5

void error_init(void *);

void lock(mutex *);

void unlock(mutex *);

void get_error(unsigned);

#endif //OS2_ERRORS_H
