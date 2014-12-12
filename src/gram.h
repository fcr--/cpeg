#ifndef GRAM_H
#define GRAM_H 1

struct gram;
struct gram_state;

struct ast {
  void * user_data;
  int from, len;
  struct ast * children[]; // NULL terminated array of children.
};

struct ast * parse(const char * text, struct gram * gram, int * last);

/**
 *  destroys the whole tree (subtrees included)
 */
void free_ast(struct ast * ast);

/**
 * this function dumps the ast as a kind of tree, adding 2 spaces
 * for every new indentation level, starting at indent.
 *   if debug is non null, it's called q
 */
void dump_ast(struct ast * ast, int indent, void (*debug)(void * user_data));

/**
 * this function prints a tree of the grammar for the recognized builtin
 * cases only.
 */
void dump_gram(struct gram * gram, int indent, int maxdepth,
    void (*print_user_data)(void * user_data));

/**
 * use this function to copy the AST purging the nodes whose user_data
 * is NULL. This works in O(#nodes). After this any node could end with
 * more children that it had in the original tree. That's why a copy is
 * performed. The root will NEVER be purged.
 */
struct ast * purge_ast(struct ast * ast);

enum filter_ast_mode {
  FILTER_AST_KEEP,               // keeps NODE and CHILDREN
  FILTER_AST_ONLY_KEEP_CHILDREN, // only keeps CHILDREN
  FILTER_AST_LEAF,               // keeps NODE
  FILTER_AST_DISCARD,            // doesn't keep any subnodes
};

/**
 * same as purge_ast, but this allow custom filtering. This procedure iterates
 * through the tree in using pre-order tree traversal, and for every node the
 * "filter" callback is called passing the current node and the privdata
 * pointer as parameters. Then the callback must return the destination for
 * that node:
 *   FILTER_AST_KEEP means that that node is copied and its children are
 *     traversed as well.
 *   FILTER_AST_ONLY_KEEP_CHILDREN meaning that the current node must not
 *     be copied, however the children will be traversed, meaning that several
 *     subtrees may be kept. As this node is not copied, those n subtrees will
 *     be inserted to the node p_i in the position previously occupied by
 *     p_{i-1}, the position for following children of p_i will be offsetted
 *     by n-1 places. Where: p_0 is the current node; p_{k+1} is the parent of
 *     p_k; and i is the biggest natural such as the nodes p_0 up to p_{i-1}
 *     were all filtered with FILTER_AST_ONLY_KEEP_CHILDREN.
 *     Example:
 *     * a <- KEEP
 *       * b <- KEEP
 *       * c <- ONLY_KEEP_CHILDREN
 *         * d <- ONLY_KEEP_CHILDREN
 *           * e <- KEEP
 *             * f <- KEEP
 *           * g <- KEEP
 *       * h <- KEEP
 *     Will result in:
 *     * a
 *       * b
 *       * e
 *         * f
 *       * g
 *       * h
 *   FILTER_AST_LEAF means that the current node will be copied. However its
 *     children won't be kept nor traversed.
 *   FILTER_AST_DISCARD means that both the current node and its children won't
 *     be kept.
 * The root will never be traversed, thus never filtered as well.
 */
struct ast * filter_ast(struct ast * ast,
    enum filter_ast_mode (*filter)(struct ast * node, void * privdata),
    void * privdata);

/**
 * matches a single character.
 */
struct gram * new_gram_dot(void * user_data);

/**
 * this function may be called with a temporary string as it's copied into
 * the gram structure.
 */
struct gram * new_gram_string(void * user_data, const char * text);

struct gram * new_gram_range(void * user_data, char from, char to);

struct gram * new_gram_int(void * user_data);

struct gram * new_gram_opt(void * user_data, struct gram * child);

struct gram * new_gram_plus(void * user_data, struct gram * child);

struct gram * new_gram_aster(void * user_data, struct gram * child);

// @args: children: last child MUST be NULL.
struct gram * new_gram_alt_arr(void * user_data, struct gram ** children);

// using this variadic macro, the NULL child will be added automatically
#define new_gram_alt(user_data, ...) ({ \
    struct gram * new_gram_alt_args[] = {__VA_ARGS__, NULL}; \
    new_gram_alt_arr(user_data, new_gram_alt_args);})

// @args: children, last child MUST be NULL.
struct gram * new_gram_cat_arr(void * user_data, struct gram ** children);

// using this variadic macro, the NULL child will be added automatically
#define new_gram_cat(user_data, ...) ({ \
    struct gram * new_gram_cat_args[] = {__VA_ARGS__, NULL}; \
    new_gram_cat_arr(user_data, new_gram_cat_args);})

struct gram * new_gram_posla(void * user_data, struct gram * child);
struct gram * new_gram_negla(void * user_data, struct gram * child);

// @param matcher: If it matches, return the number of characters eaten,
//    or -1 if it didn't match.
//    "text" is the text being parsed.
//    "cursor" is the index inside text where this matcher should look.
//    "state"(last) if this matcher (or any "sub-matcher" if this applies)
//    have consumed text at any point, this should be updated to the max
//    of the indices of the first character not consumed by itself (or any
//    of its children). This is used to determine where a syntax error may
//    occur.
// @param priv_data: This pointer is directly passed to matcher.
struct gram * new_gram_custom(
    void * user_data,
    int (*matcher)(const char * text, int cursor, void * priv_data, struct gram_state * state),
    void * priv_data);

/**
 * pos MUST be zero for grams created with:
 *   * new_gram_opt, new_gram_plus, new_gram_aster,
 *   * new_gram_posla, new_gram_negla.
 *
 * pos MUST be: 0 <= pos < number of children, for grammars created with:
 *   * new_gram_alt, new_gram_cat.
 *
 * this function must not be called for other grammars.
 */
void gram_set_child(struct gram * gram, struct gram * child, int pos);

void gram_set_user_data(struct gram * gram, void * user_data);

void * gram_get_user_data(struct gram * gram);

void free_gram(struct gram * gram);

void gram_state_update_last(struct gram_state * state, int last);
int gram_state_get_last(struct gram_state * state);

#endif
