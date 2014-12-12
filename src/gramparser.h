#ifndef GRAMPARSER_H
#define GRAMPARSER_H 1

#include <stdbool.h>

#include "gram.h"

struct gramparser;

void init_gramparser(void);

struct gramparser * new_gramparser(void);

/**
 * name shouldn't be an automatic pointer, as it's set in ast's user_data
 * after parse is called. That allows for string comparissons using just
 * the pointer.
 *   It returs -1 if there are no syntax errors on def, or the last valid
 * character of the definition otherwise.
 */
int gramparser_add(struct gramparser * gp, const char * name, const char * def);

/**
 * name shouldn't be an automatic pointer, as it's set in ast's user_data
 * after parse is called. That allows for string comparissons using just
 * the pointer.
 */
void gramparser_add_gram(struct gramparser * gp, const char * name, struct gram * g);

/**
 * returns NULL if it's not defined.
 */
struct gram * gramparser_get_gram(struct gramparser * gp, const char * name);

bool gramparser_is_complete(struct gramparser * gp);

/**
 * This frees the gramparser structure, all the undefined references, and
 * those "struct gram" internally created by gramparser_add().
 *   But it does not delete the names "names", "def" nor "g" passed as
 * parameters to gramparser_add & gramparser_add_gram.
 */
void free_gramparser(struct gramparser * gp);

#endif
