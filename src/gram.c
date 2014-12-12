#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gram.h"

#define PTRBUFF_SIZE (1<<4)
#define PTRBUFF_SIZE2 (1<<10)

/************************************************************************\
*				 PTRBUFF				 *
* This type works like a kind of obstack, but useless functions were	 *
* removed, and the code was optimized for handling pointers only.	 *
\************************************************************************/

struct ptrbuff_chunk {
  struct ptrbuff_chunk * prev;
  void * ptrs[PTRBUFF_SIZE2];
};

struct ptrbuff {
  int count;
  struct ptrbuff_chunk * last;
  void * ptrs[PTRBUFF_SIZE];
};

static inline void ptrbuff_init(struct ptrbuff * buff) {
  buff->count = 0;
  buff->last = NULL;
}

// please, do not call directly, but from ptrbuff_push.
static void ptrbuff_push2(struct ptrbuff * buff, void * ptr) {
  int c = (buff->count - PTRBUFF_SIZE) & (PTRBUFF_SIZE2 - 1);
  if (c == 0) {
    struct ptrbuff_chunk * newchunk = malloc(sizeof (struct ptrbuff_chunk));
    newchunk->prev = buff->last;
    buff->last = newchunk;
  }
  buff->last->ptrs[c] = ptr;
}

static inline void ptrbuff_push(struct ptrbuff * buff, void * ptr) {
  if (buff->count >= PTRBUFF_SIZE)
    ptrbuff_push2(buff, ptr);
  else
    buff->ptrs[buff->count] = ptr;
  buff->count++;
}

/**
 * Use this function to close the ptrbuff.
 *   * All the pointers stored will be copied (in order) to dest.
 *   * Its size will return to zero, and
 *   * all the extra chunks will be freed (but not the ptrbuff).
 * So you may use this ptrbuff again.
 */
static void ptrbuff_finalize(void ** dest, struct ptrbuff * buff) {
  // simple case first.
  if (buff->count <= PTRBUFF_SIZE) {
    memcpy(dest, buff->ptrs, sizeof (void *) * buff->count);
    buff->count = 0;
    return;
  }
  // copy the first (full) array of pointers
  memcpy(dest, buff->ptrs, PTRBUFF_SIZE);
  buff->count -= PTRBUFF_SIZE;
  dest += PTRBUFF_SIZE;
  // copy the last array of pointers which may be partially full.
  int pos = (buff->count - 1) & ~(PTRBUFF_SIZE2 - 1);
  int c = ((buff->count - 1) & (PTRBUFF_SIZE2 - 1)) + 1;
  struct ptrbuff_chunk * tmp = buff->last->prev;
  memcpy(dest + pos, buff->last->ptrs, sizeof (void *) * c);
  free(buff->last);
  buff->last = tmp;
  while (buff->last) {
    pos -= PTRBUFF_SIZE2;
    memcpy(dest + pos, buff->last->ptrs, sizeof (void *) * PTRBUFF_SIZE2);
    tmp = buff->last->prev;
    free(buff->last);
    buff->last = tmp;
  }
  buff->count = 0;
}

static void ptrbuff_reset(struct ptrbuff * buff, void(*freefn)(void *)) {
  int i;
  if (buff->count <= PTRBUFF_SIZE) {
    if (freefn)
      for (i = 0; i < buff->count; i++)
	freefn(buff->ptrs[i]);
    return;
  }
  buff->count -= PTRBUFF_SIZE;
  while (buff->last) {
    int c = ((buff->count - 1) & (PTRBUFF_SIZE2 - 1)) + 1;
    struct ptrbuff_chunk * prev = buff->last->prev;
    free(buff->last);
    buff->last = prev;
    buff->count -= c;
  }
  assert(buff->count == 0);
}

/************************************************************************\
*				   GRAM					 *
* This module provides the parser combinators for complete PEG (parsing  *
* expression grammars) support.						 *
\************************************************************************/

struct gram {
  void * user_data;
  struct ast * (*matcher)(const char * text, int cursor, struct gram * gram, struct gram_state * state);
};

struct gram_state {
  int last;
  int depth;
};

inline void gram_state_update_last(struct gram_state * state, int val) {
  if (val > state->last)
    state->last = val;
}

inline int gram_state_get_last(struct gram_state * state) {
  return state->last;
}

void gram_state_incr_depth_stack_overflow() {
  fprintf(stderr, "STACK OVERFLOW! Make sure the grammar consume input before doing\n"
      "infinite loops, e.g. left recursions are not allowed.\n");
  exit(1);
}
inline void gram_state_incr_depth(struct gram_state * state) {
  state->depth++;
  if (state->depth & ~0xfff)
    gram_state_incr_depth_stack_overflow();
}

inline void gram_state_decr_depth(struct gram_state * state) {
  state->depth--;
}

static struct ast * allocate_ast(void * user_data, int from, int len, int num_children) {
  struct ast * ast = malloc(sizeof (struct ast) + sizeof (struct ast*) * (num_children+1));
  ast->user_data = user_data;
  ast->from = from;
  ast->len = len;
  memset(ast->children, 0, sizeof (struct ast*) * (num_children+1));
  return ast;
}

struct ast * parse(const char * text, struct gram * gram, int * last) {
  struct gram_state state = {
    .last = 0,
    .depth = 0
  };
  if (last)
    state.last = *last;
  struct ast * res = gram->matcher(text, 0, gram, &state);
  if (last)
    *last = state.last;
  return res;
}

void free_ast(struct ast * ast) {
  int i;
  if (ast == NULL) {
    fprintf(stderr, "ERROR: free_ast called with a NULL pointer. Avoiding SIGSEGV.\n");
    fflush(stderr);
    exit(1);
  }
  for (i = 0; ast->children[i]; i++)
    free(ast->children[i]);
  free(ast);
}

void dump_ast(struct ast * ast, int indent, void (*debug)(void * user_data)) {
  if (!ast) {
    printf("%*sNULL\n", indent, "");
    return;
  } else if (debug) {
    printf("%*sfrom=%d, len=%d, userdata=", indent, "", ast->from, ast->len);
    debug(ast->user_data);
  } else {
    printf("%*sfrom=%d, len=%d, userdata=%p\n", indent, "",
	ast->from, ast->len, ast->user_data);
  }
  int ch;
  for (ch = 0; ast->children[ch]; ch++)
    dump_ast(ast->children[ch], indent + 2, debug);
}

static void filter_ast_flatten(struct ptrbuff * buff, struct ast * ast,
    enum filter_ast_mode (*filter)(struct ast * node, void * privdata),
    void * privdata) {
  int i;
  switch (filter(ast, privdata)) {
    // keep the node itself and its children:
    case FILTER_AST_KEEP:
      ptrbuff_push(buff, filter_ast(ast, filter, privdata));
      break;
    // drop the node itself, but keep its children:
    case FILTER_AST_ONLY_KEEP_CHILDREN:
      for (i = 0; ast->children[i]; i++)
	filter_ast_flatten(buff, ast->children[i], filter, privdata);
      break;
    // keep the element itself but drop its children:
    case FILTER_AST_LEAF:
      ptrbuff_push(buff, allocate_ast(ast->user_data, ast->from, ast->len, 0));
      break;
    // drop the element itself and its children:
    case FILTER_AST_DISCARD:
      break;
    default:
      fprintf(stderr, "{filter/purge}_ast callback function 'filter' returned invalid value.\n");
      exit(1);
  }
}

struct ast * filter_ast(struct ast * ast,
    enum filter_ast_mode (*filter)(struct ast * node, void * privdata),
    void * privdata) {
  struct ptrbuff buff;
  int i;
  ptrbuff_init(&buff);
  for (i = 0; ast->children[i]; i++)
    filter_ast_flatten(&buff, ast->children[i], filter, privdata);
  ast = allocate_ast(ast->user_data, ast->from, ast->len, buff.count);
  ptrbuff_finalize((void**)ast->children, &buff);
  return ast;
}

static enum filter_ast_mode purge_ast_fn(struct ast * node, void * privdata) {
  return node->user_data ? FILTER_AST_KEEP : FILTER_AST_ONLY_KEEP_CHILDREN;
}

struct ast * purge_ast(struct ast * ast) {
  return filter_ast(ast, &purge_ast_fn, NULL);
}

static struct ast * dot_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  if (text[cursor]) { // if we are not past the last char of the string.
    gram_state_update_last(state, cursor + 1);
    return allocate_ast(gram->user_data, cursor, 1, 0);
  }
  return NULL;
}

struct gram * new_gram_dot(void * user_data) {
  struct gram * gram;
  gram = malloc(sizeof (struct gram));
  gram->user_data = user_data;
  gram->matcher = &dot_matcher;
  return gram;
}

struct gram_string {
  struct gram gram;
  int len;
  char text[];
};

static struct ast * string_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  // safe cast cause we know this is only used from new_gram_string
  struct gram_string * g = (struct gram_string *)gram;
  if (!strncmp(text + cursor, g->text, g->len)) {
    // string matches
    gram_state_update_last(state, cursor + g->len);
    return allocate_ast(gram->user_data, cursor, g->len, 0);
  }
  return NULL;
}

struct gram * new_gram_string(void * user_data, const char * text) {
  struct gram_string * g;
  if (!text) {
    fprintf(stderr, "new_gram_string: NULL text.\n");
    return NULL;
  }
  int len = strlen(text);
  g = malloc(sizeof (struct gram_string) + len + 1);
  g->len = len;
  g->gram.user_data = user_data;
  g->gram.matcher = &string_matcher;
  memcpy(g->text, text, len + 1);
  return &g->gram;
}

struct gram_range {
  struct gram gram;
  char from, to;
};

static struct ast * range_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  // safe cast cause we know this is only used from new_gram_string
  struct gram_range * g = (struct gram_range *)gram;
  if (g->from <= text[cursor] && text[cursor] <= g->to) {
    gram_state_update_last(state, cursor + 1);
    return allocate_ast(gram->user_data, cursor, 1, 0);
  }
  return NULL;
}

struct gram * new_gram_range(void * user_data, char from, char to) {
  struct gram_range * g;
  if (from > to) {
    fprintf(stderr, "new_gram_range: from must be <= than to.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_range));
  g->gram.user_data = user_data;
  g->gram.matcher = &range_matcher;
  g->from = from;
  g->to = to;
  return &g->gram;
}

static struct ast * int_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  char * end = NULL;
  gram_state_update_last(state, cursor);
  if (!text[cursor]) {
    return NULL;
  }
  long int val = strtol(text + cursor, &end, 0);
  if (errno == ERANGE && (val == LONG_MAX || val == LONG_MIN)) {
    if (end) // endptr could also be updated...
      gram_state_update_last(state, end - text);
    return NULL;
  }
  if (end == text + cursor) // invalid
    return NULL;
  gram_state_update_last(state, end - text);
  return allocate_ast(gram->user_data, cursor, end - (text + cursor), 0);
}

struct gram * new_gram_int(void * user_data) {
  struct gram * gram;
  gram = malloc(sizeof (struct gram));
  gram->user_data = user_data;
  gram->matcher = &int_matcher;
  return gram;
}

// used by new_gram_opt, new_gram_plus, new_gram_aster, new_gram_posla
// and new_gram_negla.
struct gram_child {
  struct gram gram;
  struct gram * child;
};

static struct ast * opt_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  struct ast * ast, * subast;
  // safe cast cause we know this is only used from new_gram_opt
  struct gram_child * g = (struct gram_child *)gram;
  // recursive call:
  gram_state_incr_depth(state);
  subast = g->child->matcher(text, cursor, g->child, state);
  if (subast) {
    ast = allocate_ast(gram->user_data, cursor, subast->len, 1);
    ast->children[0] = subast;
  } else {
    ast = allocate_ast(gram->user_data, cursor, 0, 0);
  }
  gram_state_decr_depth(state);
  return ast;
}

struct gram * new_gram_opt(void * user_data, struct gram * child) {
  struct gram_child * g;
  if (!child) {
    fprintf(stderr, "new_gram_opt: NULL child.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_child));
  g->gram.user_data = user_data;
  g->gram.matcher = &opt_matcher;
  g->child = child;
  return &g->gram;
}

static struct ast * plus_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  struct ast * ast, * subast;
  int initial_cursor = cursor;
  // safe cast cause we know this is only used from new_gram_plus
  struct gram_child * g = (struct gram_child *)gram;
  // first recursive call:
  gram_state_incr_depth(state);
  subast = g->child->matcher(text, cursor, g->child, state);
  if (!subast) {
    gram_state_decr_depth(state);
    return NULL;
  }
  struct ptrbuff buff;
  ptrbuff_init(&buff);
  cursor += subast->len;
  ptrbuff_push(&buff, subast);
  while (1) {
    // recursive call:
    subast = g->child->matcher(text, cursor, g->child, state);
    if (subast) {
      if (subast->len == 0) {
	// we reached a dead state... detecting deadlocks is a good thing :D
	fprintf(stderr, "WARNING: «plus» parsing subgrammar with epsilon transitions. E.g: ('a'?)+\n"
	    "\t(as a fallback) this match will fail, but you MUST fix the grammar.");
	ptrbuff_reset(&buff, (void(*)(void*))&free_ast);
	gram_state_decr_depth(state);
	return NULL;
      }
      cursor += subast->len;
      ptrbuff_push(&buff, subast);
    } else {
      ast = allocate_ast(gram->user_data, initial_cursor, cursor - initial_cursor, buff.count);
      ptrbuff_finalize((void**)ast->children, &buff);
      gram_state_decr_depth(state);
      return ast;
    }
  }
}

struct gram * new_gram_plus(void * user_data, struct gram * child) {
  struct gram_child * g;
  if (!child) {
    fprintf(stderr, "new_gram_plus: NULL child.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_child));
  g->gram.user_data = user_data;
  g->gram.matcher = &plus_matcher;
  g->child = child;
  return &g->gram;
}

static struct ast * aster_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  struct ast * ast, * subast;
  int initial_cursor = cursor;
  struct ptrbuff buff;
  ptrbuff_init(&buff);
  // safe cast cause we know this is only used from new_gram_aster
  struct gram_child * g = (struct gram_child *)gram;
  gram_state_incr_depth(state);
  while (1) {
    // recursive call:
    subast = g->child->matcher(text, cursor, g->child, state);
    if (subast) {
      if (subast->len == 0) {
	// we reached a dead state... detecting deadlocks is a good thing :D
	fprintf(stderr, "WARNING: «aster» parsing subgrammar with epsilon transitions. E.g: ('a'?)*\n"
	    "\t(as a fallback) this match will fail, but you MUST fix the grammar.");
	ptrbuff_reset(&buff, (void(*)(void*))&free_ast);
	gram_state_decr_depth(state);
	return NULL;
      }
      cursor += subast->len;
      ptrbuff_push(&buff, subast);
    } else {
      ast = allocate_ast(gram->user_data, initial_cursor, cursor - initial_cursor, buff.count);
      ptrbuff_finalize((void**)ast->children, &buff);
      gram_state_decr_depth(state);
      return ast;
    }
  }
}

struct gram * new_gram_aster(void * user_data, struct gram * child) {
  struct gram_child * g;
  if (!child) {
    fprintf(stderr, "new_gram_aster: NULL child.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_child));
  g->gram.user_data = user_data;
  g->gram.matcher = &aster_matcher;
  g->child = child;
  return &g->gram;
}

// used by new_gram_alt and new_gram_cat.
struct gram_children {
  struct gram gram;
  struct gram * children[];
};

static struct ast * alt_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  int ch;
  // safe cast cause we know this is only used from new_gram_alt
  struct gram_children * g = (struct gram_children *)gram;
  gram_state_incr_depth(state);
  for (ch = 0; g->children[ch]; ch++) {
    // recursive call:
    struct ast * subast;
    subast = g->children[ch]->matcher(text, cursor, g->children[ch], state);
    if (subast) {
      struct ast * ast;
      ast = allocate_ast(gram->user_data, cursor, subast->len, 1);
      ast->children[0] = subast;
      gram_state_decr_depth(state);
      return ast;
    }
  }
  gram_state_decr_depth(state);
  return NULL;
}

struct gram * new_gram_alt_arr(void * user_data, struct gram ** children) {
  struct gram_children * g;
  int i, children_count = 0;
  while (children[children_count])
    children_count++;
  if (!children_count) {
    fprintf(stderr, "ERROR: new_gram_alt: 0 children.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_children) + sizeof (struct gram *) * (children_count + 1));
  g->gram.user_data = user_data;
  g->gram.matcher = &alt_matcher;
  for (i = 0; i < children_count; i++)
    g->children[i] = children[i];
  g->children[children_count] = NULL;
  return &g->gram;
}

static struct ast * cat_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  struct ast * ast, * subast;
  int initial_cursor = cursor;
  int ch;
  struct ptrbuff buff;
  ptrbuff_init(&buff);
  // safe cast cause we know this is only used from new_gram_cat
  struct gram_children * g = (struct gram_children *)gram;
  gram_state_incr_depth(state);
  for (ch = 0; g->children[ch]; ch++) {
    // recursive call:
    subast = g->children[ch]->matcher(text, cursor, g->children[ch], state);
    if (subast) {
      cursor += subast->len;
      ptrbuff_push(&buff, subast);
    } else {
      ptrbuff_reset(&buff, (void(*)(void*))&free_ast);
      gram_state_decr_depth(state);
      return NULL;
    }
  }
  ast = allocate_ast(gram->user_data, initial_cursor, cursor - initial_cursor, buff.count);
  ptrbuff_finalize((void**)ast->children, &buff);
  gram_state_decr_depth(state);
  return ast;
}

// @args: children, last child must be NULL.
struct gram * new_gram_cat_arr(void * user_data, struct gram ** children) {
  struct gram_children * g;
  int i, children_count = 0;
  while (children[children_count])
    children_count++;
  if (!children_count) {
    fprintf(stderr, "ERROR: new_gram_cat: 0 children.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_children) + sizeof (struct gram *) * (children_count + 1));
  g->gram.user_data = user_data;
  g->gram.matcher = &cat_matcher;
  for (i = 0; i < children_count; i++)
    g->children[i] = children[i];
  return &g->gram;
}

static struct ast * posla_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  struct ast * ast = NULL, * subast;
  // safe cast cause we know this is only used from new_gram_posla
  struct gram_child * g = (struct gram_child *)gram;
  // recursive call (we must not use the same last):
  int rememberedlast = state->last;
  gram_state_incr_depth(state);
  subast = g->child->matcher(text, cursor, g->child, state);
  gram_state_decr_depth(state);
  state->last = rememberedlast;
  if (subast) {
    ast = allocate_ast(gram->user_data, cursor, 0, 1);
    ast->children[0] = subast;
  }
  return ast;
}

struct gram * new_gram_posla(void * user_data, struct gram * child) {
  struct gram_child * g;
  if (!child) {
    fprintf(stderr, "new_gram_posla: NULL child.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_child));
  g->gram.user_data = user_data;
  g->gram.matcher = &posla_matcher;
  g->child = child;
  return &g->gram;
}

static struct ast * negla_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  struct ast * ast = NULL, * subast;
  // safe cast cause we know this is only used from new_gram_posla
  struct gram_child * g = (struct gram_child *)gram;
  // recursive call (we must not use the same last):
  int rememberedlast = state->last;
  gram_state_incr_depth(state);
  subast = g->child->matcher(text, cursor, g->child, state);
  gram_state_decr_depth(state);
  state->last = rememberedlast;
  if (!subast) {
    ast = allocate_ast(gram->user_data, cursor, 0, 0);
  }
  return ast;
}

struct gram * new_gram_negla(void * user_data, struct gram * child) {
  struct gram_child * g;
  if (!child) {
    fprintf(stderr, "new_gram_negla: NULL child.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_child));
  g->gram.user_data = user_data;
  g->gram.matcher = &negla_matcher;
  g->child = child;
  return &g->gram;
}

// used by new_gram_custom.
struct gram_custom {
  struct gram gram;
  int (*matcher)(const char * text, int cursor, void * priv_data, struct gram_state * state);
  void * priv_data;
};

static struct ast * custom_matcher(const char * text, int cursor, struct gram * gram, struct gram_state * state) {
  // safe cast cause we know this is only used from new_gram_custom
  struct gram_custom * g = (struct gram_custom *)gram;
  // recursive call:
  gram_state_incr_depth(state);
  int len = g->matcher(text, cursor, g->priv_data, state);
  gram_state_decr_depth(state);
  if (len >= 0)
    return allocate_ast(gram->user_data, cursor, len, 0);
  return NULL;
}

struct gram * new_gram_custom(
    void * user_data,
    int (*matcher)(const char * text, int cursor, void * priv_data, struct gram_state * state),
    void * priv_data) {
  struct gram_custom * g;
  if (!matcher) {
    fprintf(stderr, "new_gram_custom: NULL matcher.\n");
    return NULL;
  }
  g = malloc(sizeof (struct gram_custom));
  g->gram.user_data = user_data;
  g->gram.matcher = &custom_matcher;
  g->matcher = matcher;
  g->priv_data = priv_data;
  return &g->gram;
}

void gram_set_child(struct gram * gram, struct gram * child, int pos) {
  if (gram->matcher == opt_matcher || gram->matcher == plus_matcher
      || gram->matcher == aster_matcher || gram->matcher == posla_matcher
      || gram->matcher == negla_matcher) {
    if (pos != 0) {
      fprintf(stderr, "gram_set_child called with pos>0 for single child grammar.\n");
      exit(1);
    }
    // 100% safe cast:
    struct gram_child * g = (struct gram_child *) gram;
    g->child = child;
  } else if (gram->matcher == alt_matcher || gram->matcher == cat_matcher) {
    struct gram_children * g = (struct gram_children *) gram;
    int children_count = 0;
    while (g->children[children_count])
      children_count++;
    if (pos < 0 || pos >= children_count) {
      fprintf(stderr, "gram_set_child called with pos=%d out of range [0..%d).\n",
	  pos, children_count);
      exit(1);
    }
    g->children[pos] = child;
  } else if (gram->matcher == dot_matcher) {
    printf("gram_set_child MUST NOT be called for grammars created with new_gram_dot.\n");
    exit(1);
  } else if (gram->matcher == string_matcher) {
    printf("gram_set_child MUST NOT be called for grammars created with new_gram_string.\n");
    exit(1);
  } else if (gram->matcher == range_matcher) {
    printf("gram_set_child MUST NOT be called for grammars created with new_gram_range.\n");
    exit(1);
  } else if (gram->matcher == int_matcher) {
    printf("gram_set_child MUST NOT be called for grammars created with new_gram_int.\n");
    exit(1);
  } else if (gram->matcher == custom_matcher) {
    printf("gram_set_child MUST NOT be called for grammars created with new_gram_custom.\n");
    exit(1);
  } else {
    printf("bug in gram_set_child: matcher case not considered.\n");
  }
}

void gram_set_user_data(struct gram * gram, void * user_data) {
  if (!gram) {
    fprintf(stderr, "gram_set_user_data: NULL grammar.\n");
    exit(1);
  }
  gram->user_data = user_data;
}

void * gram_get_user_data(struct gram * gram) {
  if (!gram) {
    fprintf(stderr, "gram_get_user_data: NULL grammar.\n");
    exit(1);
  }
  return gram->user_data;
}

void free_gram(struct gram * gram) {
  //if (gram->matcher == foo_matcher) {
  //  free(gram->additional_field);
  //}
  free(gram);
}
