/* mote/mote-x — developer: SYFaren — kilo-style HLDB for popular languages */
#include "hl.h"
#include <ctype.h>
#include <string.h>

#define F_STR 1u
#define F_NUM 2u
#define F_HASH_SL 4u /* single-line # comment (shell/py/make/yaml) */
#define F_MARKUP 8u  /* HTML/XML-ish tags */
#define F_MD 16u     /* markdown light */

struct HlSyntax {
  const char *name;
  const char **filematch;
  const char **keywords; /* types end with '|' */
  const char *sl;        /* "//" or NULL; "#" via F_HASH_SL */
  const char *mls, *mle;
  unsigned flags;
};

/* ---- keyword tables (types marked with trailing |) ---- */

static const char *C_KW[] = {
    "switch", "if", "while", "for", "break", "continue", "return", "else",
    "struct", "union", "typedef", "static", "enum", "class", "case", "default",
    "do", "goto", "sizeof", "typeof", "alignof", "inline", "restrict",
    "namespace", "template", "typename", "using", "public", "private",
    "protected", "virtual", "friend", "operator", "new", "delete", "try",
    "catch", "throw", "this", "constexpr", "nullptr", "concept", "requires",
    "int|", "long|", "double|", "float|", "char|", "unsigned|", "signed|",
    "void|", "short|", "auto|", "const|", "bool|", "size_t|", "ssize_t|",
    "uint8_t|", "uint16_t|", "uint32_t|", "uint64_t|", "int8_t|", "int16_t|",
    "int32_t|", "int64_t|", "true|", "false|", "NULL|", NULL};

static const char *PY_KW[] = {
    "and", "as", "assert", "async", "await", "break", "class", "continue",
    "def", "del", "elif", "else", "except", "finally", "for", "from", "global",
    "if", "import", "in", "is", "lambda", "nonlocal", "not", "or", "pass",
    "raise", "return", "try", "while", "with", "yield", "match", "case",
    "True|", "False|", "None|", "self|", "int|", "str|", "float|", "bool|",
    "list|", "dict|", "tuple|", "bytes|", NULL};

static const char *JS_KW[] = {
    "if", "else", "for", "while", "do", "switch", "case", "break", "continue",
    "return", "function", "var", "let", "const", "class", "extends", "new",
    "this", "super", "import", "export", "from", "default", "try", "catch",
    "finally", "throw", "typeof", "instanceof", "in", "of", "async", "await",
    "yield", "void", "delete", "debugger", "with", "enum", "implements",
    "interface", "package", "private", "protected", "public", "static",
    "true|", "false|", "null|", "undefined|", "NaN|", "Infinity|", "number|",
    "string|", "boolean|", "any|", "void|", "never|", "object|", "symbol|",
    "bigint|", "type|", "keyof|", "readonly|", "as|", "satisfies|", NULL};

static const char *GO_KW[] = {
    "break", "case", "chan", "const", "continue", "default", "defer", "else",
    "fallthrough", "for", "func", "go", "goto", "if", "import", "interface",
    "map", "package", "range", "return", "select", "struct", "switch", "type",
    "var", "true|", "false|", "nil|", "iota|", "bool|", "string|", "error|",
    "int|", "int8|", "int16|", "int32|", "int64|", "uint|", "uint8|", "uint16|",
    "uint32|", "uint64|", "uintptr|", "byte|", "rune|", "float32|", "float64|",
    "complex64|", "complex128|", "any|", NULL};

static const char *RS_KW[] = {
    "as", "async", "await", "break", "const", "continue", "crate", "dyn",
    "else", "enum", "extern", "false", "fn", "for", "if", "impl", "in", "let",
    "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self",
    "Self", "static", "struct", "super", "trait", "true", "type", "unsafe",
    "use", "where", "while", "i8|", "i16|", "i32|", "i64|", "i128|", "isize|",
    "u8|", "u16|", "u32|", "u64|", "u128|", "usize|", "f32|", "f64|", "bool|",
    "char|", "str|", "String|", "Option|", "Result|", "Vec|", "Box|", NULL};

static const char *JAVA_KW[] = {
    "abstract", "assert", "break", "case", "catch", "class", "const",
    "continue", "default", "do", "else", "enum", "extends", "final", "finally",
    "for", "goto", "if", "implements", "import", "instanceof", "interface",
    "native", "new", "package", "private", "protected", "public", "return",
    "static", "strictfp", "super", "switch", "synchronized", "this", "throw",
    "throws", "transient", "try", "volatile", "while", "var", "yield", "record",
    "sealed", "permits", "non-sealed", "true|", "false|", "null|", "boolean|",
    "byte|", "char|", "double|", "float|", "int|", "long|", "short|", "void|",
    "String|", "Object|", NULL};

static const char *SH_KW[] = {
    "if", "then", "else", "elif", "fi", "case", "esac", "for", "while", "until",
    "do", "done", "in", "function", "select", "time", "coproc", "true|",
    "false|", "export", "local", "return", "exit", "shift", "set", "unset",
    "readonly", "declare", "typeset", "alias", "unalias", "source", NULL};

static const char *SQL_KW[] = {
    "select", "from", "where", "and", "or", "not", "insert", "into", "values",
    "update", "set", "delete", "create", "table", "drop", "alter", "index",
    "join", "left", "right", "inner", "outer", "on", "as", "group", "by",
    "order", "having", "limit", "offset", "union", "all", "distinct", "null",
    "is", "in", "like", "between", "exists", "case", "when", "then", "else",
    "end", "primary", "key", "foreign", "references", "constraint", "default",
    "unique", "check", "view", "trigger", "begin", "commit", "rollback",
    "int|", "integer|", "varchar|", "text|", "boolean|", "bool|", "date|",
    "timestamp|", "float|", "real|", "numeric|", "serial|", NULL};

static const char *PHP_KW[] = {
    "abstract", "and", "as", "break", "callable", "case", "catch", "class",
    "clone", "const", "continue", "declare", "default", "do", "echo", "else",
    "elseif", "empty", "enddeclare", "endfor", "endforeach", "endif",
    "endswitch", "endwhile", "extends", "final", "finally", "fn", "for",
    "foreach", "function", "global", "goto", "if", "implements", "include",
    "include_once", "instanceof", "insteadof", "interface", "isset", "list",
    "match", "namespace", "new", "or", "print", "private", "protected",
    "public", "readonly", "require", "require_once", "return", "static",
    "switch", "throw", "trait", "try", "unset", "use", "var", "while", "xor",
    "yield", "true|", "false|", "null|", "array|", "string|", "int|", "float|",
    "bool|", "object|", "mixed|", "void|", "never|", NULL};

static const char *CSS_KW[] = {
    "important", "media", "keyframes", "from", "to", "supports", "charset",
    "import", "namespace", "font-face", "page", "rgba|", "rgb|", "hsl|",
    "hsla|", "url|", "var|", "calc|", "true|", "false|", NULL};

static const char *C_EXT[] = {".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".cxx",
                              ".hxx", NULL};
static const char *PY_EXT[] = {".py", ".pyw", ".pyi", NULL};
static const char *JS_EXT[] = {".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx",
                               NULL};
static const char *GO_EXT[] = {".go", NULL};
static const char *RS_EXT[] = {".rs", NULL};
static const char *JAVA_EXT[] = {".java", NULL};
static const char *SH_EXT[] = {".sh", ".bash", ".zsh", ".ksh", NULL};
static const char *SQL_EXT[] = {".sql", NULL};
static const char *PHP_EXT[] = {".php", ".phtml", NULL};
static const char *JSON_EXT[] = {".json", ".jsonc", NULL};
static const char *HTML_EXT[] = {".html", ".htm", ".xhtml", NULL};
static const char *XML_EXT[] = {".xml", ".svg", ".xsl", ".plist", NULL};
static const char *CSS_EXT[] = {".css", ".scss", NULL};
static const char *MD_EXT[] = {".md", ".markdown", ".mdown", NULL};
static const char *YAML_EXT[] = {".yml", ".yaml", NULL};
static const char *MAKE_EXT[] = {"Makefile", "makefile", "GNUmakefile", ".mk",
                                 NULL};

static const HlSyntax HLDB[] = {
    {"c/c++", C_EXT, C_KW, "//", "/*", "*/", F_STR | F_NUM},
    {"python", PY_EXT, PY_KW, NULL, NULL, NULL, F_STR | F_NUM | F_HASH_SL},
    {"js/ts", JS_EXT, JS_KW, "//", "/*", "*/", F_STR | F_NUM},
    {"go", GO_EXT, GO_KW, "//", "/*", "*/", F_STR | F_NUM},
    {"rust", RS_EXT, RS_KW, "//", "/*", "*/", F_STR | F_NUM},
    {"java", JAVA_EXT, JAVA_KW, "//", "/*", "*/", F_STR | F_NUM},
    {"shell", SH_EXT, SH_KW, NULL, NULL, NULL, F_STR | F_NUM | F_HASH_SL},
    {"sql", SQL_EXT, SQL_KW, "--", "/*", "*/", F_STR | F_NUM},
    {"php", PHP_EXT, PHP_KW, "//", "/*", "*/", F_STR | F_NUM | F_HASH_SL},
    {"json", JSON_EXT, NULL, NULL, NULL, NULL, F_STR | F_NUM},
    {"html", HTML_EXT, NULL, NULL, "<!--", "-->", F_STR | F_MARKUP},
    {"xml", XML_EXT, NULL, NULL, "<!--", "-->", F_STR | F_MARKUP},
    {"css", CSS_EXT, CSS_KW, NULL, "/*", "*/", F_STR | F_NUM},
    {"markdown", MD_EXT, NULL, NULL, NULL, NULL, F_MD | F_STR},
    {"yaml", YAML_EXT, NULL, NULL, NULL, NULL, F_STR | F_NUM | F_HASH_SL},
    {"make", MAKE_EXT, NULL, NULL, NULL, NULL, F_STR | F_HASH_SL},
};

static int is_sep(int c) {
  return !isalnum((unsigned char)c) && c != '_';
}

static int push_span(HlSpan *out, int n, int max, size_t start, size_t end,
                     HlKind kind) {
  if (!out || max <= 0 || n >= max || end <= start || start > 0xFFFF ||
      end > 0xFFFF)
    return n;
  out[n].start = (uint16_t)start;
  out[n].len = (uint16_t)(end - start);
  out[n].kind = (uint8_t)kind;
  return n + 1;
}

static int match_kw(const char **kws, const char *s, size_t n, HlKind *kind) {
  int i;
  if (!kws) return 0;
  for (i = 0; kws[i]; i++) {
    const char *k = kws[i];
    size_t kn = strlen(k);
    int is_type = 0;
    if (kn && k[kn - 1] == '|') {
      is_type = 1;
      kn--;
    }
    if (kn == n && strncmp(k, s, n) == 0) {
      *kind = is_type ? HL_TYPE : HL_KEYWORD;
      return 1;
    }
  }
  return 0;
}

static int starts_with(const char *s, size_t len, size_t i, const char *p) {
  size_t n;
  if (!p || !p[0]) return 0;
  n = strlen(p);
  if (i + n > len) return 0;
  return memcmp(s + i, p, n) == 0;
}

const HlSyntax *hl_select(const char *path) {
  const char *base, *slash, *dot;
  size_t i, j;
  if (!path || !path[0]) return NULL;
  slash = strrchr(path, '/');
#ifdef _WIN32
  {
    const char *b = strrchr(path, '\\');
    if (b && (!slash || b > slash)) slash = b;
  }
#endif
  base = slash ? slash + 1 : path;
  for (i = 0; i < sizeof HLDB / sizeof HLDB[0]; i++) {
    const char **fm = HLDB[i].filematch;
    for (j = 0; fm[j]; j++) {
      if (fm[j][0] == '.') {
        dot = strrchr(base, '.');
        if (dot && strcmp(dot, fm[j]) == 0) return &HLDB[i];
      } else if (strcmp(base, fm[j]) == 0) {
        return &HLDB[i];
      }
    }
  }
  return NULL;
}

const char *hl_lang_name(const HlSyntax *syn) {
  return syn ? syn->name : "plain";
}

HlKind hl_kind_at(const HlSpan *spans, int nspans, size_t off) {
  int i;
  for (i = 0; i < nspans; i++) {
    size_t a = spans[i].start, b = a + spans[i].len;
    if (off >= a && off < b) return (HlKind)spans[i].kind;
  }
  return HL_NORMAL;
}

int hl_line(const HlSyntax *syn, const char *line, size_t len, int in_ml,
            HlSpan *out, int max_out, int *out_ml) {
  size_t i = 0;
  int n = 0;
  unsigned flags = syn ? syn->flags : 0;

  if (out_ml) *out_ml = in_ml;
  if (!line) return 0;

  /* markdown: ATX headers */
  if (syn && (flags & F_MD) && len && line[0] == '#') {
    size_t k = 0;
    while (k < len && line[k] == '#') k++;
    if (k && (k == len || line[k] == ' '))
      return push_span(out, 0, max_out, 0, len, HL_PREPROC);
  }

  while (i < len) {
    if (syn && syn->mls && syn->mle && in_ml) {
      size_t j = i;
      size_t el = strlen(syn->mle);
      while (j < len) {
        if (starts_with(line, len, j, syn->mle)) {
          j += el;
          in_ml = 0;
          break;
        }
        j++;
      }
      n = push_span(out, n, max_out, i, j, HL_COMMENT);
      i = j;
      continue;
    }

    if (syn && syn->mls && syn->mle && starts_with(line, len, i, syn->mls)) {
      size_t j = i + strlen(syn->mls);
      size_t el = strlen(syn->mle);
      in_ml = 1;
      while (j < len) {
        if (starts_with(line, len, j, syn->mle)) {
          j += el;
          in_ml = 0;
          break;
        }
        j++;
      }
      n = push_span(out, n, max_out, i, j, HL_COMMENT);
      i = j;
      continue;
    }

    if (syn && syn->sl && starts_with(line, len, i, syn->sl)) {
      n = push_span(out, n, max_out, i, len, HL_COMMENT);
      break;
    }
    if ((flags & F_HASH_SL) && line[i] == '#' &&
        (i == 0 || is_sep((unsigned char)line[i - 1]) ||
         (syn && syn->name && strcmp(syn->name, "php") == 0))) {
      /* avoid HTML/CSS colors #rgb: only if not followed by hex-only short token
         in css — for css F_HASH_SL is off. shell/py/yaml/make OK */
      n = push_span(out, n, max_out, i, len, HL_COMMENT);
      break;
    }

    if ((flags & F_MARKUP) && line[i] == '<') {
      size_t j = i + 1;
      HlKind k = HL_KEYWORD;
      if (j < len && line[j] == '/') j++;
      while (j < len && line[j] != '>' && line[j] != ' ' && line[j] != '\t' &&
             line[j] != '\n')
        j++;
      while (j < len && line[j] != '>') {
        if (line[j] == '"' || line[j] == '\'') {
          char q = line[j++];
          n = push_span(out, n, max_out, i, j - 1, k);
          i = j - 1;
          while (j < len && line[j] != q) {
            if (line[j] == '\\' && j + 1 < len) j += 2;
            else j++;
          }
          if (j < len) j++;
          n = push_span(out, n, max_out, i, j, HL_STRING);
          i = j;
          k = HL_KEYWORD;
          continue;
        }
        j++;
      }
      if (j < len) j++;
      n = push_span(out, n, max_out, i, j, HL_KEYWORD);
      i = j;
      continue;
    }

    if ((flags & F_STR) && (line[i] == '"' || line[i] == '\'' ||
                            ((flags & F_MD) && line[i] == '`'))) {
      char q = line[i];
      size_t j = i + 1;
      int trip = 0;
      if (syn && syn->name && strcmp(syn->name, "python") == 0 &&
          i + 2 < len && line[i + 1] == q && line[i + 2] == q) {
        trip = 1;
        j = i + 3;
      }
      while (j < len) {
        if (!trip && line[j] == '\\' && j + 1 < len) {
          j += 2;
          continue;
        }
        if (trip && j + 2 < len && line[j] == q && line[j + 1] == q &&
            line[j + 2] == q) {
          j += 3;
          break;
        }
        if (!trip && line[j] == q) {
          j++;
          break;
        }
        j++;
      }
      n = push_span(out, n, max_out, i, j, HL_STRING);
      i = j;
      continue;
    }

    if (line[i] == '#' && syn && syn->sl && strcmp(syn->sl, "//") == 0 &&
        (flags & F_STR) /* C preprocessor */) {
      /* handled below as identifier/# */
    }
    if (line[i] == '#' && syn && !(flags & F_HASH_SL) && !(flags & F_MARKUP) &&
        !(flags & F_MD)) {
      size_t j = i + 1;
      while (j < len && line[j] != ' ' && line[j] != '\t' && isalnum((unsigned char)line[j]))
        j++;
      /* #include etc — whole directive rest of interesting tokens: paint #word */
      j = i + 1;
      while (j < len && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
      n = push_span(out, n, max_out, i, j, HL_PREPROC);
      i = j;
      continue;
    }

    if ((flags & F_NUM) && (isdigit((unsigned char)line[i]) ||
                            (line[i] == '.' && i + 1 < len &&
                             isdigit((unsigned char)line[i + 1])))) {
      size_t j = i;
      if (line[j] == '0' && j + 1 < len &&
          (line[j + 1] == 'x' || line[j + 1] == 'X')) {
        j += 2;
        while (j < len && isxdigit((unsigned char)line[j])) j++;
      } else {
        while (j < len && (isdigit((unsigned char)line[j]) || line[j] == '.' ||
                           line[j] == '_' || line[j] == 'e' || line[j] == 'E' ||
                           line[j] == 'x' || line[j] == 'X' ||
                           (line[j] >= 'a' && line[j] <= 'f') ||
                           (line[j] >= 'A' && line[j] <= 'F')))
          j++;
      }
      n = push_span(out, n, max_out, i, j, HL_NUMBER);
      i = j;
      continue;
    }

    if (isalpha((unsigned char)line[i]) || line[i] == '_') {
      size_t j = i + 1;
      HlKind k = HL_NORMAL;
      while (j < len && (isalnum((unsigned char)line[j]) || line[j] == '_'))
        j++;
      if (syn && match_kw(syn->keywords, line + i, j - i, &k))
        n = push_span(out, n, max_out, i, j, k);
      i = j;
      continue;
    }

    i++;
  }

  if (out_ml) *out_ml = in_ml;
  return n;
}
