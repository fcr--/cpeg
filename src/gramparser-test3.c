#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "gramparser.h"
#include "gram.h"

static const char MAIN_TOKEN[] = "main";
static const char LINE_TOKEN[] = "line";
static const char WORDS_TOKEN[] = "words";
static const char WORD_TOKEN[] = "word";
static const char SUBCMD_TOKEN[] = "subcmd";
static const char BRACES_TOKEN[] = "braces";
static const char QUOTES_TOKEN[] = "quotes";
static const char SP_TOKEN[] = "sp";

struct {
  const char * name;
  const char * def;
  enum filter_ast_mode mode;
} gram_tokens[] = {
  {MAIN_TOKEN, "line ('\n' line)* !.", FILTER_AST_KEEP},
  {LINE_TOKEN, "words (';' words)* / comment", FILTER_AST_KEEP},
  {WORDS_TOKEN, "sp* (word (sp+ word)* sp*)?", FILTER_AST_KEEP},
  {WORD_TOKEN, "braces / subcmd", FILTER_AST_KEEP},
  {SUBCMD_TOKEN, "'[' words ']'", FILTER_AST_KEEP},
  {BRACES_TOKEN, "'{' ('\\\\'./braces/!'\\\\'!'{'!'}')* '}'", FILTER_AST_LEAF},
  {QUOTES_TOKEN, "'\"' (subcmd/braces/'\\\\'./!'\\\\'!'[')* '\"'", FILTER_AST_LEAF},
  {SP_TOKEN, "' '/'\t'/'\v'/'\f'", FILTER_AST_DISCARD},
  {NULL}
}, list_tokens[] {
  {WORDS_TOKEN, "sp* (word (sp+ word)* sp*)?", FILTER_AST_KEEP},
  {BRACES_TOKEN, "'{' ('\\\\'./braces/!'\\\\'!'{'!'}')* '}'", FILTER_AST_LEAF},
  {QUOTES_TOKEN, "'\"' (braces/'\\\\'./!'\\\\')* '\"'", FILTER_AST_KEEP},
  {SP_TOKEN, "' '/'\t'/'\v'/'\f'/'\r'/'\n'", FILTER_AST_DISCARD},
};

int main(void) {
  init_gramparser();
  struct gramparser * gp = new_gramparser();
  int i;
  for (i = 0; gram_tokens[i].name; i++) {
    printf("adding %s\n", gram_tokens[i].name);
    gramparser_add(gp, gram_tokens[i].name, gram_tokens[i].def);
  }
  struct gram * g = gramparser_get_gram(gp, MAIN_TOKEN);
  printf("is complete? %d\n", gramparser_is_complete(gp));
  i = 0;
  struct ast * a = parse("hello world {foo bar} taz!", g, &i);
  printf("ast=%p, last=%d\n", a, i);
  if (!a)
    return 1;
  switch ((intptr_t)a->user_data) {
    case (intptr_t)MAIN_TOKEN:
      printf("main!\n");
  }
  return 0;
}
