#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "gramparser.h"

struct gram * get_having_eq_gram(void) {
  struct gramparser * gp = new_gramparser();
  gramparser_add(gp, "main", "' '* name ' '* '=' ' '* rest");
  gramparser_add(gp, "name", "('A'..'Z'/'a'..'z'/'0'..'9'/'_')+");
  gramparser_add(gp, "rest", "(!'\n'.)*");
  return gramparser_get_gram(gp, "main");
}

void print(void * userdata) {
  if (!userdata)
    printf("(null)\n");
  else
    printf("\"%s\"\n", (char*)userdata);
}

int main(void) {
  init_gramparser();
  char linebuffer[1024];
  int pos;
  char * rootnt = NULL;
  struct gram * having_eq_gram = get_having_eq_gram();
  struct gramparser * gp = new_gramparser();
  printf("Enter grammar with lines like: ntname = definition\n"
      "and an empty line after the last non-terminal to compile and use it.\n"
      "Note: The first non-terminal will be the root one.\n");
  while (!feof(stdin)) {
    pos = 0;
    printf("gram > ");
    fflush(stdout);
    if (!fgets(linebuffer, 1024, stdin))
      continue;
    if (linebuffer[0]=='\n' && linebuffer[1]=='\0') {
      if (!gramparser_is_complete(gp))
	printf("Grammar is not complete yet... Press ^C if you really want to quit...\n");
      else
	break;
    }
    struct ast * ast = parse(linebuffer, having_eq_gram, &pos);
    if (ast) {
      struct ast * purged_ast = purge_ast(ast);
      free_ast(ast);
      //dump_ast(purged_ast, 0, (void(*)(void*))&puts);
      char * name = malloc(purged_ast->children[0]->len);
      strncpy(name, linebuffer + purged_ast->children[0]->from,
	  purged_ast->children[0]->len);
      name[purged_ast->children[0]->len] = 0;
      char * rest = linebuffer + purged_ast->children[1]->from;
      rest[purged_ast->children[1]->len] = 0;
      free_ast(purged_ast);
      printf("main=«%s», rest=«%s»\n", name, rest);
      gramparser_add(gp, name, rest);
      if (!rootnt)
	rootnt = name;
    } else {
      printf("Syntax error near char %d\n", pos);
    }
  }
  if (!rootnt)
    return 0;
  struct gram * gram = gramparser_get_gram(gp, rootnt);
  while (!feof(stdin)) {
    pos = 0;
    printf("parse > ");
    fflush(stdout);
    if (!fgets(linebuffer, 1024, stdin))
      continue;
    struct ast * ast = parse(linebuffer, gram, &pos);
    if (ast) {
      struct ast * purged_ast = purge_ast(ast);
      //dump_ast(ast, 0, (void(*)(void*))&print);
      free_ast(ast);
      dump_ast(purged_ast, 0, (void(*)(void*))&puts);
      free_ast(purged_ast);
    } else {
      printf("Input line didn't match with grammar. Last pos = %d\n", pos);
    }
  }
  return 0;
}
