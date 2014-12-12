#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "gramparser.h"

struct def {
  struct def * next;
  const char * name;
  struct gram * gram;
};

struct undef_ref {
  struct undef_ref * next;
  char * name; // this always refers malloced ram. Must be freed on resolve.
  struct gram * parent;
  int num_child;
};

struct gram_list {
  struct gram_list * next;
  struct gram * gram;
};

struct gramparser {
  struct def * defs;
  struct undef_ref * undef_refs;
  struct gram_list * freeable_grammars;
};

static struct gram * peggrammar = NULL;

#define ALT_GRAM 1
#define CAT_GRAM 2
#define NEGLA_GRAM 3
#define POSLA_GRAM 4
#define CUANT_GRAM 5
#define OPT_CUANT 6
#define ASTER_CUANT 7
#define PLUS_CUANT 8
#define RANGE_GRAM 9
#define NT_GRAM 10
#define STR_GRAM 11
#define CHAR_GRAM 12
#define DOT_GRAM 13

static const char escaped_codes[] = {
  'a', '\a', // (bell)
  'b', '\b', // (back space)
  't', '\t', // (horizontal tab)
  'n', '\n', // (new line)
  'v', '\v', // (vertical tab)
  'f', '\f', // (form feed)
  'r', '\r', // (return)
  0};

static struct gram * add_freeable_gram(struct gramparser * gp, struct gram * g);

void init_gramparser(void) {
  if (peggrammar) // already initialized.
    return;

  /****** GRAMMAR FOR GRAMMAR PARSING :D ******/
  // anychar = .;
  struct gram * anychar = new_gram_dot(NULL);

  // backslash = '\\';
  struct gram * backslash = new_gram_string(NULL, "\\");

  // blanks = (' ' / '\t' / '\n' / '\v' / '\f' / '\r' / '#' (!'\n' .)*)*;  # <- comments
  struct gram * blanks_gram = new_gram_aster(NULL,
      new_gram_alt(NULL,
	  new_gram_string(NULL, " "),
	  new_gram_range(NULL, '\t', '\r'),
	  new_gram_cat(NULL, //comment
	      new_gram_string(NULL, "#"),
	      new_gram_aster(NULL,
		  new_gram_cat(NULL,
		      new_gram_negla(NULL,
			  new_gram_string(NULL, "\n")),
		      anychar)))));

  // dot = '.';
  struct gram * dot_gram = new_gram_string((void*)DOT_GRAM, ".");

  // '\'' ('\\' . / .) '\'';
  struct gram * char_gram;
  {
    struct gram * a = new_gram_string(NULL, "'");
    char_gram = new_gram_cat((void*)CHAR_GRAM,
	a,
	new_gram_alt(NULL,
	    new_gram_cat(NULL, backslash, anychar),
	    anychar),
	a);
  }

  // str = '"' ('\\' . / !'"' .)* '"';
  struct gram * str_gram;
  {
    struct gram * quote = new_gram_string(NULL, "\"");
    str_gram = new_gram_cat((void*)STR_GRAM,
	quote,
	new_gram_aster(NULL,
	    new_gram_alt(NULL,
		new_gram_cat(NULL, backslash, anychar),
		new_gram_cat(NULL, new_gram_negla(NULL, quote), anychar))),
	quote);
  }

  // nt = ('A'..'Z' / 'a'..'z' / '_') ('A'..'Z' / 'a'..'z' / '_' / '0'..'9')*;
  struct gram * nt_gram;
  {
    // hidden grammar t = ('A'..'Z' / 'a'..'z' / '_');
    struct gram * t = new_gram_alt(NULL,
	new_gram_range(NULL, 'A', 'Z'),
	new_gram_range(NULL, 'a', 'z'),
	new_gram_string(NULL, "_"));
    // then, nt = t (t / '0'..'9')*;
    nt_gram = new_gram_cat((void*)NT_GRAM,
	t,
	new_gram_aster(NULL,
	    new_gram_alt(NULL,
		t,
		new_gram_range(NULL, '0', '9'))));
  }

  // range = char blanks ".." blanks char;
  struct gram * range_gram = new_gram_cat((void*)RANGE_GRAM,
      char_gram,
      blanks_gram,
      new_gram_string(NULL, ".."),
      char_gram);

  struct gram * tmp_gram; // this tmp var is used to keep a reference so we
			  // can set the cyclic reference.
  // atom = range / char / str / nt / '(' alt ')' / dot;
  struct gram * atom_gram = new_gram_alt(NULL,
      range_gram,
      char_gram,
      str_gram,
      nt_gram,
      tmp_gram = new_gram_cat(NULL,
	  new_gram_string(NULL, "("),
	  (void*)(-1), // here goes reference to alt_gram
	  new_gram_string(NULL, ")")),
      dot_gram);

  // cuant = atom blanks (cuantopt / cuantaster / cuantplus)?;
  struct gram * cuant_gram = new_gram_cat((void*)CUANT_GRAM,
      atom_gram,
      blanks_gram,
      new_gram_opt(NULL,
	  new_gram_alt(NULL,
	      new_gram_string((void*)OPT_CUANT, "?"),
	      new_gram_string((void*)ASTER_CUANT, "*"),
	      new_gram_string((void*)PLUS_CUANT, "+"))));

  // negla = '!' blanks cuant;
  struct gram * negla_gram = new_gram_cat((void*)NEGLA_GRAM,
      new_gram_string(NULL, "!"),
      blanks_gram,
      cuant_gram);

  // posla = '&' blanks cuant;
  struct gram * posla_gram = new_gram_cat((void*)POSLA_GRAM,
      new_gram_string(NULL, "&"),
      blanks_gram,
      cuant_gram);

  // la = negla / posla / cuant;
  struct gram * la_gram = new_gram_alt(NULL,
      negla_gram, posla_gram, cuant_gram, NULL);

  // cat = (la blanks)+; # use "" for epsilon productions
  struct gram * cat_gram = new_gram_plus((void*)CAT_GRAM,
      new_gram_cat(NULL,
	  la_gram,
	  blanks_gram));

  // alt = blanks cat blanks ('/' blanks cat blanks)*
  struct gram * alt_gram = new_gram_cat((void*)ALT_GRAM,
      blanks_gram,
      cat_gram,
      blanks_gram,
      new_gram_aster(NULL,
	  new_gram_cat(NULL,
	      new_gram_string(NULL, "/"),
	      blanks_gram,
	      cat_gram,
	      blanks_gram)));

  // add cyclic reference for infinite grammar:
  gram_set_child(tmp_gram, alt_gram, 1);

  // root non-terminal for grammars is = alt !.
  peggrammar = new_gram_cat(NULL,
      alt_gram,
      new_gram_negla(NULL, anychar));
}

struct gramparser * new_gramparser(void) {
  init_gramparser(); // make sure it's initialized...
  struct gramparser * gp = malloc(sizeof (struct gramparser));
  gp->defs = NULL;
  gp->undef_refs = NULL;
  gp->freeable_grammars = NULL;
  return gp;
}

#ifdef DEBUG
static void print_string(void * gramptr) {
  switch ((intptr_t)gramptr) {
    case 0: puts("(nil)"); break;
    case ALT_GRAM: puts("ALT_GRAM"); break;
    case CAT_GRAM: puts("CAT_GRAM"); break;
    case NEGLA_GRAM: puts("NEGLA_GRAM"); break;
    case POSLA_GRAM: puts("POSLA_GRAM"); break;
    case CUANT_GRAM: puts("CUANT_GRAM"); break;
    case OPT_CUANT: puts("OPT_CUANT"); break;
    case ASTER_CUANT: puts("ASTER_CUANT"); break;
    case PLUS_CUANT: puts("PLUS_CUANT"); break;
    case RANGE_GRAM: puts("RANGE_GRAM"); break;
    case NT_GRAM: puts("NT_GRAM"); break;
    case STR_GRAM: puts("STR_GRAM"); break;
    case CHAR_GRAM: puts("CHAR_GRAM"); break;
    case DOT_GRAM: puts("DOT_GRAM"); break;
    default: puts("duhh!"); break;
  }
}
#endif

static char decode_char(const char * def, struct ast * ast) {
  assert(def);
  assert(ast);
  assert(CHAR_GRAM == (intptr_t)ast->user_data);
  assert(ast->len == 3 || (ast->len == 4 && def[ast->from+1] == '\\'));
  if (ast->len == 3)
    return def[ast->from + 1];
  int i;
  char c = def[ast->from + 2];
  for (i = 0; escaped_codes[i]; i += 2)
    if (c == escaped_codes[i])
      return escaped_codes[i+1];
  return c;
}

struct gram_thunk {
  struct gram * gram;
  struct undef_ref * undef_ref;
};

static struct gram_thunk gram_from_ast(struct gramparser * gp, const char * def, struct ast * ast) {
  int children_count = 0;
  int i;
  int type = (intptr_t)ast->user_data;
  while (ast->children[children_count])
    children_count++;
  // first, if there's only a single child for a few cases, just recurse.
  if (children_count == 1 && (type == ALT_GRAM || type == CAT_GRAM
	|| type == CUANT_GRAM)) {
    return gram_from_ast(gp, def, ast->children[0]);
  }
#ifdef DEBUG
  print_string(ast->user_data);
#endif
  switch (type) {
    case 0:
      for (i = 0; i < children_count; i++) {
	struct gram_thunk t = gram_from_ast(gp, def, ast->children[i]);
	if (!t.gram && !t.undef_ref)
	  continue;
	for (i++; i < children_count; i++) {
	  struct gram_thunk t2 = gram_from_ast(gp, def, ast->children[i]);
	  if (t2.gram || t2.undef_ref) {
	    fprintf(stderr, "INTERNAL ERROR: unnamed nodes on purged ast "
		"while parsing grammars don't match\nrequired conditions.\n");
	    // this means that given the ast (or subast in any case) with
	    // an unnamed root, if we'd remove every child of named nodes,
	    // then in that tree only one named node should remain.
	    exit(1);
	  }
	}
	return t;
      }
      return (struct gram_thunk){NULL, NULL};
    case ALT_GRAM:
    case CAT_GRAM:
      {
	struct gram * children[children_count + 1];
	struct gram ** parent_ptrs[children_count];
	int parent_ptrs_count = 0;
	for (i = 0; ast->children[i]; i++) {
	  struct gram_thunk gt = gram_from_ast(gp, def, ast->children[i]);
	  if (gt.gram) {
	    children[i] = gt.gram;
	  } else {
	    parent_ptrs[parent_ptrs_count++] = &(gt.undef_ref->parent);
	    gt.undef_ref->num_child = i;
	    children[i] = (struct gram *)-1;
	  }
	}
	children[children_count] = NULL;
	struct gram * res;
	if (type == ALT_GRAM)
	  res = new_gram_alt_arr(NULL, children);
	else
	  res = new_gram_cat_arr(NULL, children);
	add_freeable_gram(gp, res);
	// assign parent to all the undefined thunks
	for (i = 0; i < parent_ptrs_count; i++)
	  *(parent_ptrs[i]) = res;
	return (struct gram_thunk){res, NULL};
      }
    // cases with only one child:
    case NEGLA_GRAM:
    case POSLA_GRAM:
    case CUANT_GRAM:
      {
	struct gram * res, * child;
	struct gram_thunk gt = gram_from_ast(gp, def, ast->children[0]);
	if (gt.gram) {
	  child = gt.gram;
	} else {
	  gt.undef_ref->num_child = 0;
	  child = (struct gram *)-1;
	}
	if (type == NEGLA_GRAM)
	  res = new_gram_negla(NULL, child);
	else if (type == POSLA_GRAM)
	  res = new_gram_posla(NULL, child);
	else if ((intptr_t)(ast->children[1]->user_data) == OPT_CUANT)
	  res = new_gram_opt(NULL, child);
	else if ((intptr_t)(ast->children[1]->user_data) == ASTER_CUANT)
	  res = new_gram_aster(NULL, child);
	else if ((intptr_t)(ast->children[1]->user_data) == PLUS_CUANT)
	  res = new_gram_plus(NULL, child);
	else {
	  fprintf(stderr, "INTERNAL ERROR: Construction not allowed for cuant's children.\n");
	  exit(1);
	}
	add_freeable_gram(gp, res);
	if (gt.undef_ref)
	  gt.undef_ref->parent = res;
	return (struct gram_thunk){res, NULL};
      }
    case OPT_CUANT:
    case ASTER_CUANT:
    case PLUS_CUANT:
      fprintf(stderr, "INTERNAL ERROR: *_CUANT should never appear by themselves.\n");
      exit(1);
    // two children: (both are char)
    case RANGE_GRAM:
      if (children_count != 2) {
	fprintf(stderr, "INTERNAL ERROR: RANGE_GRAM must have two children.\n");
	exit(1);
      } else {
	char from = decode_char(def, ast->children[0]);
	char to = decode_char(def, ast->children[1]);
	struct gram * res = new_gram_range(NULL, from, to);
	add_freeable_gram(gp, res);
	return (struct gram_thunk){res, NULL};
      }
    // zero children:
    case NT_GRAM:
      {
	char buff[ast->len + 1];
	strncpy(buff, def + ast->from, ast->len);
	buff[ast->len] = 0;
	// look in defs:
	struct def * def = gp->defs;
	while (def) {
	  if (!strcmp(def->name, buff))
	    return (struct gram_thunk){def->gram, NULL};
	  def = def->next;
	}
	struct undef_ref * undef_ref = malloc(sizeof (struct undef_ref));
	memset(undef_ref, 0, sizeof (struct undef_ref));
	undef_ref->name = malloc(ast->len + 1);
	strcpy(undef_ref->name, buff);
	undef_ref->next = gp->undef_refs;
	gp->undef_refs = undef_ref;
	return (struct gram_thunk){NULL, undef_ref};
      }
    case STR_GRAM:
      {
	int outchars = 0;
	for (i = 1; i < ast->len - 1; i++) {
	  outchars++;
	  if (def[ast->from + i] == '\\')
	    i++;
	}
	char * str = malloc(outchars+1);
	outchars = 0;
	for (i = 1; i < ast->len - 1; i++) {
	  char c = def[ast->from + i];
	  if (c == '\\') {
	    c = def[ast->from + ++i];
	    for (i = 0; escaped_codes[i]; i += 2)
	      if (c == escaped_codes[i])
		c = escaped_codes[i+1];
	  }
	  str[outchars++] = c;
	}
	str[outchars] = 0;
	struct gram * g = new_gram_string(NULL, str);
	add_freeable_gram(gp, g);
	free(str);
	return (struct gram_thunk){g, NULL};
      }
    case CHAR_GRAM:
      {
	char str[2];
	str[0] = decode_char(def, ast);
	str[1] = 0;
	struct gram * g = new_gram_string(NULL, str);
	add_freeable_gram(gp, g);
	return (struct gram_thunk){g, NULL};
      }
    case DOT_GRAM:
      return (struct gram_thunk){add_freeable_gram(gp, new_gram_dot(NULL)), NULL};
    default:
      // if this happens check that:
      //   * this function is being called only from a purged ast.
      //   * purge_ast is working properly.
      //   * this switch contemplates al the cases (if you added syntax sugar to grammars)
      fprintf(stderr, "Internal error at gramparser.c:gram_from_ast (something is missing).\n");
      exit(1);
  }
  fprintf(stderr, "INTERNAL ERROR: reaching not reachable code at gram_from_ast.\n");
  exit(1);
}

int gramparser_add(struct gramparser * gp, const char * name, const char * def) {
  if (!peggrammar) {
    fprintf(stderr, "ERROR: call init_gramparser before calling gramparser_add!\n");
    exit(1);
  }
  if (!gp) {
    fprintf(stderr, "ERROR: gramparser_add: gp is NULL\n");
    exit(1);
  }
  if (!name) {
    fprintf(stderr, "ERROR: gramparser_add: name is NULL\n");
    exit(1);
  }
  if (!def) {
    fprintf(stderr, "ERROR: gramparser_add: def is NULL\n");
    exit(1);
  }
  int last = 0;
  struct ast * ast = parse(def, peggrammar, &last);
#ifdef DEBUG
  printf("last = %d\n", last);
  dump_ast(ast, 0, print_string);
#endif
  if (!ast) {
    fprintf(stderr, "gramparser.c: gramparser_add: Parsing error at char %d of nt %s.\n", last, name);
    return last;
  }
  struct ast * purged_ast = purge_ast(ast);
#ifdef DEBUG
  printf("\npurged:\n");
  dump_ast(purged_ast, 0, print_string);
#endif
  struct gram_thunk gt = gram_from_ast(gp, def, purged_ast);
  struct gram * g;
  if (gt.undef_ref) {
    g = new_gram_cat((void*)name, (struct gram *)-1);
    add_freeable_gram(gp, g);
    gt.undef_ref->parent = g;
    gt.undef_ref->num_child = 0;
  } else if (gram_get_user_data(gt.gram)) { // gt.gram: valid & named
    g = new_gram_cat((void*)name, gt.gram);
    add_freeable_gram(gp, g);
  } else { // gt.gram: valid & unnamed
    gram_set_user_data(gt.gram, (void*)name);
    g = gt.gram;
  }
  gramparser_add_gram(gp, name, g);
  return -1;
}

static struct gram * add_freeable_gram(struct gramparser * gp, struct gram * g) {
  assert(gp);
  assert(g);
  struct gram_list * item = malloc(sizeof (struct gram_list *));
  item->next = gp->freeable_grammars;
  item->gram = g;
  gp->freeable_grammars = item;
  return g;
}

void gramparser_add_gram(struct gramparser * gp, const char * name, struct gram * g) {
  if (!gp) {
    fprintf(stderr, "ERROR: gramparser_add_gram: gp is NULL\n");
    exit(1);
  }
  if (!name) {
    fprintf(stderr, "ERROR: gramparser_add_gram: name is NULL\n");
    exit(1);
  }
  if (!g) {
    fprintf(stderr, "ERROR: gramparser_add_gram: g is NULL\n");
    exit(1);
  }
  if (gramparser_get_gram(gp, name)) {
    fprintf(stderr, "ERROR: gramparser_get_gram: there was a grammar already defined for name \"%s\".\n", name);
    exit(1);
  }
  struct def * def = malloc(sizeof (struct def));
  def->next = gp->defs;
  def->name = name;
  def->gram = g;
  gp->defs = def;
  struct undef_ref ** urefs = &gp->undef_refs;
  while (*urefs) {
    if (!strcmp((*urefs)->name, name)) {
      struct undef_ref * next = (*urefs)->next;
      free((*urefs)->name);
      gram_set_child((*urefs)->parent, g, (*urefs)->num_child);
      free(*urefs);
      *urefs = next;
    } else
      urefs = &(*urefs)->next;
  }
}

struct gram * gramparser_get_gram(struct gramparser * gp, const char * name) {
  if (!gp) {
    fprintf(stderr, "ERROR: gramparser_get_gram: gp is NULL\n");
    exit(1);
  }
  if (!name) {
    fprintf(stderr, "WARNING: gramparser_get_gram: name is NULL\n");
    return NULL;
  }
  struct def * def = gp->defs;
  while (def) {
    if (!strcmp(def->name, name))
      return def->gram;
    def = def->next;
  }
  return NULL; // not found:
}

bool gramparser_is_complete(struct gramparser * gp) {
  if (!gp) {
    fprintf(stderr, "ERROR: gramparser_is_complete: gp is NULL\n");
    exit(1);
  }
#ifdef DEBUG
  struct undef_ref * u = gp->undef_refs;
  while (u) {
    printf("undef_ref{name=%s, parent=%p, num_child=%d}\n", u->name, u->parent, u->num_child);
    u = u->next;
  }
#endif
  return !gp->undef_refs;
}

void free_gramparser(struct gramparser * gp) {
  struct def * def, * tmp_def;
  for (def = gp->defs; def; def = tmp_def) {
    tmp_def = def->next;
    free(def);
  }
  struct undef_ref * uref, * tmp_uref;
  for (uref = gp->undef_refs; uref; uref = tmp_uref) {
    tmp_uref = uref->next;
    free(uref->name);
    free(uref);
  }
  struct gram_list * fg, * tmp_fg;
  for (fg = gp->freeable_grammars; fg; fg = tmp_fg) {
    tmp_fg = fg->next;
    free_gram(fg->gram);
    free(fg);
  }
  free(gp);
}
