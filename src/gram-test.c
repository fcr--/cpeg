#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gram.h"

void test1(void) {
  // gram = 'a'*!.;
  struct gram * gram;
  gram = new_gram_cat(NULL,
      new_gram_aster(NULL,
	  new_gram_string(NULL, "a")),
      new_gram_negla(NULL,
	  new_gram_dot(NULL)),
      NULL);
  assert(gram);
  int i, last = -42;
  struct ast * ast = parse("aaa", gram, &last);
  // debug
  printf("matching \"aaa\"\nast=%p, last = %d\n", ast, last);
  dump_ast(ast, 0, NULL);
  // assertions
  assert(last == 3);
  assert(ast);
  assert(ast->children[0] && ast->children[1] && !ast->children[2]);
  assert(ast->children[0]->from == 0 && ast->children[0]->len == 3);
  assert(ast->children[1]->from == 3 && ast->children[1]->len == 0);
  for (i = 0; i < 3; i++)
    assert(ast->children[0]->children[i]->from == i
	&& ast->children[0]->children[i]->len == 1
	&& !ast->children[0]->children[i]->children[0]);
  // debug
  last = -42;
  ast = parse("aaa!", gram, &last);
  printf("matching \"aaa!\"\nast=%p, last = %d\n", ast, last);
  dump_ast(ast, 0, NULL);
  // assertions
  assert(last == 3);
  assert(!ast);
  printf("test1 passed!\n\n");
}

void test2(void) {
  struct gram * gram, * tmp;
  struct ast * ast;
  int last = -42;
  // gram = ('[' gram ']')+ / "";
  gram = new_gram_alt((void*)0xa,
      new_gram_plus((void*)0x35,
	tmp = new_gram_cat((void*)0xc,
	  new_gram_string(NULL, "["),
	  (struct gram *)(-1),
	  new_gram_string(NULL, "]"),
	  NULL)),
      new_gram_string((void*)0x1337, ""),
      NULL);
  assert(gram);
  // add cyclic reference by setting child:
  gram_set_child(tmp, gram, 1);
  // debug
  ast = parse("[[][]x]", gram, &last);
  printf("matching \"[[][]x]\"\nast=%p, last = %d\n", ast, last);
  dump_ast(ast, 0, NULL);
  // assertions
  assert(ast);
  assert("[[][]x]"[last] == 'x');
  assert(ast->children[0]);
  assert(ast->children[0]->user_data == (void*)0x1337);
  assert(!ast->children[1]);
  printf("test2 passed!\n\n");
}

void test3(void) {
  int last = -42;
  struct ast * ast;
  struct gram * gram;
  // gram = int !.;
  gram = new_gram_cat(NULL,
      new_gram_int(NULL),
      new_gram_negla(NULL,
	new_gram_dot(NULL)),
      NULL);
  const char * text = "42";
  // debug:
  ast = parse(text, gram, &last);
  printf("matching \"42\"\nast=%p, last = %d\n", ast, last);
  dump_ast(ast, 0, NULL);
  // assertions:
  assert(ast);
  assert(ast->children[0]);
  assert(ast->children[0]->from == 0);
  assert(ast->children[0]->len == 2);
  assert(strtol(text + ast->children[0]->from, NULL, 0) == 42);
  assert(ast->children[1]);
  assert(last == strlen(text));
  // debug:
  last = -42;
  text = " 0xfoo";
  ast = parse(text, gram, &last);
  printf("matching \" 0xfoo\"\nast=%p, last = %d\n", ast, last);
  dump_ast(ast, 0, NULL);
  // assertions:
  assert(!ast);
  assert(last == 4);
  printf("test3 passed!\n\n");
}

void test4(void) {
  int last = -42;
  struct ast * ast;
  struct gram * gram;
  // gram == ('a'/'b')+!.
  gram = new_gram_cat(NULL,
      new_gram_aster(NULL,
	  new_gram_alt(NULL, 
	      new_gram_string((void*)0xa, "a"),
	      new_gram_string((void*)0xb, "b"),
	      NULL)),
      new_gram_negla(NULL,
	  new_gram_dot(NULL)),
      NULL);
  const char * text = "baba";
  ast = parse(text, gram, &last);
  // debug:
  dump_ast(ast, 0, NULL);
  assert(ast);
  ast = purge_ast(ast);
  printf("after cleanup:\n");
  dump_ast(ast, 0, NULL);
  // assertions:
  assert(ast);
  assert(ast->children[0] && ast->children[1]
      && ast->children[2] && ast->children[3]);
  assert(!ast->children[4]);
  printf("test4 passed!\n\n");
}

int main(void) {
  test1();
  test2();
  test3();
  test4();
  return 0;
}
