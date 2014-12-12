#include <assert.h>
#include <stdio.h>

#include "gramparser.h"

void test1(void) {
  struct gramparser * gp = new_gramparser();
  gramparser_add(gp, "foo", "( a * / 'b' ) ! .");
  gramparser_add(gp, "a", "'n' / \"jk\"");
  printf("is complete? %d\n", gramparser_is_complete(gp));
  struct gram * gram = gramparser_get_gram(gp, "foo");
  int last = -1;
  struct ast * ast = parse("njknnjk", gram, &last);
  printf("last=%d:\n", last);
  dump_ast(ast, 2, NULL);
}

int main(void) {
  // init_gramparser(); // not needed
  test1();
  return 0;
}
