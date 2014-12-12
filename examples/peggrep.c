#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "gramparser.h"

static void add_gram(struct gramparser * gp, const char * name, const char * def) {
  int i, res = gramparser_add(gp, name, def);
  if (res >= 0) {
    fprintf(stderr, "Error parsing non-terminal %s:\n", name);
    fprintf(stderr, "\t%s\n\t%*s^\n", def, res, "");
    exit(1);
  }
}

void colorize(char * text, int * cursor, struct ast * ast, int depth) {
  printf("\033[3%dm", depth%6 + 1);
  struct ast ** children = (struct ast **)&ast->children;
  while (*cursor < ast->from + ast->len) {
    if (*children && *cursor >= (*children)->from) {
      colorize(text, cursor, *children, depth + 1);
      printf("\033[3%dm", depth%6 + 1);
      children++;
      continue;
    }
    putchar(text[(*cursor)++]);
  }
}

static void void_puts(void * str) {
  puts((char *)str);
}

static void parse_file(struct gram * g, int delim, bool use_colorize, bool use_ast, bool quiet, FILE * fd) {
  static char * buffer = NULL;
  size_t buffer_size = 0;
  ssize_t res;
  errno = 0;
  while ((res = getdelim(&buffer, &buffer_size, delim, fd)) != -1) {
    if (delim && res > 0 && buffer[res-1] == delim) {
      buffer[res-1] = 0;
    } else {
      buffer[res] = 0;
    }
    int last;
    struct ast * raw_ast = parse(buffer, g, &last);
    if (raw_ast) {
      struct ast * ast = purge_ast(raw_ast);
      if (!quiet) {
	if (use_colorize) {
	  int cursor = 0;
	  colorize(buffer, &cursor, ast, 0);
	  printf("\033[0m%s%c", buffer + cursor, delim);
	} else {
	  printf("%s%c", buffer, delim);
	}
      }
      if (use_ast) {
	dump_ast(ast, 2, &void_puts);
      }
      free_ast(ast);
      free_ast(raw_ast);
    }
  }
  if (errno) {
    perror("Error getting line");
    exit(1);
  }
}

void print_help(const char * arg0) {
  printf("Usage: %1$s [-z] [-c / -nc] [-ast] [-q] {-nt name def}* main_def {file}*\n"
      "Options:\n"
      "  -z        Use NUL byte as delimiter (instead of \\n).\n"
      "  -c / -nc  (Force / No) colorize.\n"
      "  -ast      Print its Abstract Syntax Tree.\n"
      "  -q        Don't print matched lines.\n"
      "Examples:\n"
      "  %1$s '\"hello\"' file;                 : starting with hello\n"
      "  %1$s '(!\"hello\".)*\"hello\"' file;     : lines containing hello\n"
      "  %1$s -nt e '\"foo\"!.' '(!e.)*e' file; : lines ending with foo\n"
      "  %1$s -nt ab \"'a' ab 'b'\" 'ab!.';     : lines like ab, aabb, aaabbb, ...\n"
      "  %1$s -nt nb '!(\"[\"/\"]\").' -nt m '\"[\" nb* m* \"]\" nb*' 'nb* m*!.'; : lines with matching brackets\n"
      "  echo '1+1*(2+1)+3' | %1$s -ast \\\n"
      "     -nt atom  \"('0'..'9')+ / '('adds')'\" \\\n"
      "     -nt mults 'atom (\"*\"atom)*' \\\n"
      "     -nt adds  'mults (\"+\"mults)*'  'adds!.'; : parse natural arithmetic expressions\n",
      arg0);
  // there are some pathological cases, for instance with exponential cpu & memory consumption:
  // echo aaaaaaaaaaaaaaaaaaaaaaaaaa | ./peggrep -nt a '"a"(e/e/.)' -nt e '!(a"j")a' e
}

int main(int argc, char * argv[]) {
  init_gramparser();
  struct gramparser * gp = new_gramparser();
  bool use_colorize = !!isatty(1);
  bool use_ast = false;
  bool quiet = false;
  int i, main_grammar_arg_index = -1;
  int delim = '\n';
  for (i = 1; i < argc; i++) {
    if (!strcmp("-nt", argv[i]) && i < argc - 2) {
      add_gram(gp, argv[i+1], argv[i+2]);
      i += 2;
    } else if (!strcmp("-z", argv[i])) {
      delim = 0;
    } else if (!strcmp("-c", argv[i])) {
      use_colorize = true;
    } else if (!strcmp("-nc", argv[i])) {
      use_colorize = false;
    } else if (!strcmp("-ast", argv[i])) {
      use_ast = true;
    } else if (!strcmp("-q", argv[i])) {
      quiet = true;
    } else if (!strcmp("--help", argv[i]) || !strcmp("-help", argv[i])) {
      print_help(*argv);
      return 0;
    } else {
      add_gram(gp, "main", argv[i]);
      main_grammar_arg_index = i;
      break;
    }
  }
  if (main_grammar_arg_index < 0) {
    print_help(*argv);
    return 1;
  }
  if (!gramparser_is_complete(gp)) {
    fprintf(stderr, "There are undefined non-terminal references.\n");
    return 1;
  }
  struct gram * g = gramparser_get_gram(gp, "main");
  if (main_grammar_arg_index == argc - 1) {
    parse_file(g, delim, use_colorize, use_ast, quiet, stdin);
  } else {
    for (i = main_grammar_arg_index + 1; i < argc; i++) {
      FILE * fd = fopen(argv[i], "r");
      if (!fd) {
	fprintf(stderr, "Failed opening file %s: %s\n", argv[i], strerror(errno));
      }
      parse_file(g, delim, use_colorize, use_ast, quiet, fd);
      fclose(fd);
    }
  }
  return 0;
}
