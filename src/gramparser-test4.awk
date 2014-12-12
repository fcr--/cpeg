#!/usr/bin/awk -f
/^@KEYWORD/ {
  for (i = 2; i <= NF; i++) {
    printf("  {" toupper($i) "_KW_TOKEN, \"")
    for (j = 1; j <= length($i); j++)
      printf("('%s'/'%s')", toupper(substr($i, j, 1)), tolower(substr($i, j, 1)))
    print "!ident_char\", FILTER_AST_KEEP},"
  }
  next
}

/^ *(#|$)/{next}

token=="" {
  token = $1
  sub(/=.*/, "", token)
  sub(/^[^=]*. */, "")
}

{mode="KEEP"}

match($0, /; *# *@(KEEP|ONLY_KEEP_CHILDREN|LEAF|DISCARD)$/) {
  mode = substr($0, RSTART)
  mode = substr(mode, index(mode,"@") + 1)
  $0 = substr($0, 1, RSTART)
}

{
  sub(/^ */, "")
  def = def $0
  sub(/;$/, "", def)
}

/;$/ {
  print "  {" toupper(token) "_TOKEN, \"" def "\", FILTER_AST_" mode "},"
  def = token = ""
}
