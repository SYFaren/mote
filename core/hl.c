/* mote core — hl.c (packed pool + offset HLDB, runtime views) */
#include "hl.h"
#include <ctype.h>
#include <string.h>

#define F_STR 1u
#define F_NUM 2u
#define F_HASH_SL 4u
#define F_MARKUP 8u
#define F_MD 16u
#define HNONE 0xFFFFu

struct HlSyntax {
  const char *name;
  const char *exts;
  const char *kws;
  const char *sl;
  const char *mls, *mle;
  unsigned flags;
};

typedef struct {
  unsigned short name, exts, kws, sl, mls, mle, flags;
} HlDef;

static const char HPOOL[] =
    "//\0/*\0*/\0--\0<!--\0-->\0c/c++\0.c\0.h\0.cpp\0.hpp\0.cc\0.hh\0."
    "cxx\0.hxx\0\0switch\0if\0while\0for\0break\0continue\0return\0els"
    "e\0struct\0union\0typedef\0static\0enum\0class\0case\0default\0do"
    "\0goto\0sizeof\0typeof\0alignof\0inline\0restrict\0namespace\0tem"
    "plate\0typename\0using\0public\0private\0protected\0virtual\0frie"
    "nd\0operator\0new\0delete\0try\0catch\0throw\0this\0constexpr\0nu"
    "llptr\0concept\0requires\0int|\0long|\0double|\0float|\0char|\0un"
    "signed|\0signed|\0void|\0short|\0auto|\0const|\0bool|\0size_t|\0s"
    "size_t|\0uint8_t|\0uint16_t|\0uint32_t|\0uint64_t|\0int8_t|\0int1"
    "6_t|\0int32_t|\0int64_t|\0true|\0false|\0NULL|\0\0python\0.py\0.p"
    "yw\0.pyi\0\0and\0as\0assert\0async\0await\0break\0class\0continue"
    "\0def\0del\0elif\0else\0except\0finally\0for\0from\0global\0if\0i"
    "mport\0in\0is\0lambda\0nonlocal\0not\0or\0pass\0raise\0return\0tr"
    "y\0while\0with\0yield\0match\0case\0True|\0False|\0None|\0self|\0"
    "int|\0str|\0float|\0bool|\0list|\0dict|\0tuple|\0bytes|\0\0js/ts\0"
    ".js\0.jsx\0.mjs\0.cjs\0.ts\0.tsx\0\0if\0else\0for\0while\0do\0swi"
    "tch\0case\0break\0continue\0return\0function\0var\0let\0const\0cl"
    "ass\0extends\0new\0this\0super\0import\0export\0from\0default\0tr"
    "y\0catch\0finally\0throw\0typeof\0instanceof\0in\0of\0async\0awai"
    "t\0yield\0void\0delete\0debugger\0with\0enum\0implements\0interfa"
    "ce\0package\0private\0protected\0public\0static\0true|\0false|\0n"
    "ull|\0undefined|\0NaN|\0Infinity|\0number|\0string|\0boolean|\0an"
    "y|\0void|\0never|\0object|\0symbol|\0bigint|\0type|\0keyof|\0read"
    "only|\0as|\0satisfies|\0\0go\0.go\0\0break\0case\0chan\0const\0co"
    "ntinue\0default\0defer\0else\0fallthrough\0for\0func\0go\0goto\0i"
    "f\0import\0interface\0map\0package\0range\0return\0select\0struct"
    "\0switch\0type\0var\0true|\0false|\0nil|\0iota|\0bool|\0string|\0"
    "error|\0int|\0int8|\0int16|\0int32|\0int64|\0uint|\0uint8|\0uint1"
    "6|\0uint32|\0uint64|\0uintptr|\0byte|\0rune|\0float32|\0float64|\0"
    "complex64|\0complex128|\0any|\0\0rust\0.rs\0\0as\0async\0await\0b"
    "reak\0const\0continue\0crate\0dyn\0else\0enum\0extern\0false\0fn\0"
    "for\0if\0impl\0in\0let\0loop\0match\0mod\0move\0mut\0pub\0ref\0re"
    "turn\0self\0Self\0static\0struct\0super\0trait\0true\0type\0unsaf"
    "e\0use\0where\0while\0i8|\0i16|\0i32|\0i64|\0i128|\0isize|\0u8|\0"
    "u16|\0u32|\0u64|\0u128|\0usize|\0f32|\0f64|\0bool|\0char|\0str|\0"
    "String|\0Option|\0Result|\0Vec|\0Box|\0\0java\0.java\0\0abstract\0"
    "assert\0break\0case\0catch\0class\0const\0continue\0default\0do\0"
    "else\0enum\0extends\0final\0finally\0for\0goto\0if\0implements\0i"
    "mport\0instanceof\0interface\0native\0new\0package\0private\0prot"
    "ected\0public\0return\0static\0strictfp\0super\0switch\0synchroni"
    "zed\0this\0throw\0throws\0transient\0try\0volatile\0while\0var\0y"
    "ield\0record\0sealed\0permits\0non-sealed\0true|\0false|\0null|\0"
    "boolean|\0byte|\0char|\0double|\0float|\0int|\0long|\0short|\0voi"
    "d|\0String|\0Object|\0\0shell\0.sh\0.bash\0.zsh\0.ksh\0\0if\0then"
    "\0else\0elif\0fi\0case\0esac\0for\0while\0until\0do\0done\0in\0fu"
    "nction\0select\0time\0coproc\0true|\0false|\0export\0local\0retur"
    "n\0exit\0shift\0set\0unset\0readonly\0declare\0typeset\0alias\0un"
    "alias\0source\0\0sql\0.sql\0\0select\0from\0where\0and\0or\0not\0"
    "insert\0into\0values\0update\0set\0delete\0create\0table\0drop\0a"
    "lter\0index\0join\0left\0right\0inner\0outer\0on\0as\0group\0by\0"
    "order\0having\0limit\0offset\0union\0all\0distinct\0null\0is\0in\0"
    "like\0between\0exists\0case\0when\0then\0else\0end\0primary\0key\0"
    "foreign\0references\0constraint\0default\0unique\0check\0view\0tr"
    "igger\0begin\0commit\0rollback\0int|\0integer|\0varchar|\0text|\0"
    "boolean|\0bool|\0date|\0timestamp|\0float|\0real|\0numeric|\0seri"
    "al|\0\0php\0.php\0.phtml\0\0abstract\0and\0as\0break\0callable\0c"
    "ase\0catch\0class\0clone\0const\0continue\0declare\0default\0do\0"
    "echo\0else\0elseif\0empty\0enddeclare\0endfor\0endforeach\0endif\0"
    "endswitch\0endwhile\0extends\0final\0finally\0fn\0for\0foreach\0f"
    "unction\0global\0goto\0if\0implements\0include\0include_once\0ins"
    "tanceof\0insteadof\0interface\0isset\0list\0match\0namespace\0new"
    "\0or\0print\0private\0protected\0public\0readonly\0require\0requi"
    "re_once\0return\0static\0switch\0throw\0trait\0try\0unset\0use\0v"
    "ar\0while\0xor\0yield\0true|\0false|\0null|\0array|\0string|\0int"
    "|\0float|\0bool|\0object|\0mixed|\0void|\0never|\0\0json\0.json\0"
    ".jsonc\0\0html/xml\0.html\0.htm\0.xhtml\0.xml\0.svg\0.xsl\0.plist"
    "\0\0css\0.css\0.scss\0\0important\0media\0keyframes\0from\0to\0su"
    "pports\0charset\0import\0namespace\0font-face\0page\0rgba|\0rgb|\0"
    "hsl|\0hsla|\0url|\0var|\0calc|\0true|\0false|\0\0markdown\0.md\0."
    "markdown\0.mdown\0\0yaml\0.yml\0.yaml\0\0make\0Makefile\0makefile"
    "\0GNUmakefile\0.mk\0\0";

static const HlDef HLDEF[] = {
    {21, 27, 62, 0, 3, 6, 3u},
    {550, 557, 572, 65535, 65535, 65535, 7u},
    {832, 838, 867, 0, 3, 6, 3u},
    {1310, 1313, 1318, 0, 3, 6, 3u},
    {1654, 1659, 1664, 0, 3, 6, 3u},
    {1989, 1994, 2001, 0, 3, 6, 3u},
    {2428, 2434, 2455, 65535, 65535, 65535, 7u},
    {2642, 2646, 2652, 9, 3, 6, 3u},
    {3081, 3085, 3098, 0, 3, 6, 7u},
    {3630, 3635, 65535, 65535, 65535, 65535, 3u},
    {3649, 3658, 65535, 65535, 12, 17, 9u},
    {3699, 3703, 3715, 65535, 3, 6, 3u},
    {3850, 3859, 65535, 65535, 65535, 65535, 17u},
    {3881, 3886, 65535, 65535, 65535, 65535, 7u},
    {3898, 3903, 65535, 65535, 65535, 65535, 5u}
};

#define HL_N (sizeof HLDEF / sizeof HLDEF[0])

static HlSyntax HLVIEW[sizeof HLDEF / sizeof HLDEF[0]];
static unsigned char hl_ready;

static const char *hp(unsigned short off) {
  return off == HNONE ? NULL : HPOOL + off;
}

static void hl_boot(void) {
  size_t i;
  if (hl_ready) return;
  for (i = 0; i < HL_N; i++) {
    HLVIEW[i].name = hp(HLDEF[i].name);
    HLVIEW[i].exts = hp(HLDEF[i].exts);
    HLVIEW[i].kws = hp(HLDEF[i].kws);
    HLVIEW[i].sl = hp(HLDEF[i].sl);
    HLVIEW[i].mls = hp(HLDEF[i].mls);
    HLVIEW[i].mle = hp(HLDEF[i].mle);
    HLVIEW[i].flags = HLDEF[i].flags;
  }
  hl_ready = 1;
}

static int is_sep(int c) {
  return !isalnum((unsigned char)c) && c != '_';
}

static int push_span(HlSpan *out, int n, int max, size_t start, size_t end,
                     HlKind kind) {
  if (!out || max <= 0 || n >= max || end <= start || start > 0xFFFF ||
      end > 0xFFFF)
    return n;
  out[n].start = (mote_u16)start;
  out[n].len = (mote_u16)(end - start);
  out[n].kind = (unsigned char)kind;
  return n + 1;
}

static int match_kw(const char *kws, const char *s, size_t n, HlKind *kind) {
  const char *k;
  if (!kws) return 0;
  for (k = kws; *k; k += strlen(k) + 1) {
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

static int ascii_tolower(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
  return c;
}

static int str_eq_ci(const char *a, const char *b) {
  while (*a && *b) {
    if (ascii_tolower((unsigned char)*a) != ascii_tolower((unsigned char)*b))
      return 0;
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

const HlSyntax *hl_select(const char *path) {
  const char *base, *slash, *bslash, *dot, *fm;
  size_t i;
  hl_boot();
  if (!path || !path[0]) return NULL;
  slash = strrchr(path, '/');
  bslash = strrchr(path, '\\');
  if (bslash && (!slash || bslash > slash)) slash = bslash;
  base = slash ? slash + 1 : path;
  for (i = 0; i < HL_N; i++) {
    for (fm = HLVIEW[i].exts; fm && *fm; fm += strlen(fm) + 1) {
      if (fm[0] == '.') {
        dot = strrchr(base, '.');
        /* DOS / some FS fold extensions to UPPER — match case-insensitively. */
        if (dot && str_eq_ci(dot, fm)) return &HLVIEW[i];
      } else if (str_eq_ci(base, fm)) {
        return &HLVIEW[i];
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

    if (line[i] == '#' && syn && !(flags & F_HASH_SL) && !(flags & F_MARKUP) &&
        !(flags & F_MD)) {
      size_t j = i + 1;
      size_t kw_end;
      while (j < len && (isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
      kw_end = j;
      n = push_span(out, n, max_out, i, kw_end, HL_PREPROC);
      /* #include "..." / <...> */
      if (kw_end - (i + 1) == 7 && strncmp(line + i + 1, "include", 7) == 0) {
        while (j < len && (line[j] == ' ' || line[j] == '\t')) j++;
        if (j < len && (line[j] == '"' || line[j] == '<')) {
          char open = line[j], close = (open == '"') ? '"' : '>';
          size_t k = j + 1;
          while (k < len && line[k] != close && line[k] != '\n') k++;
          if (k < len) k++;
          n = push_span(out, n, max_out, j, k, HL_STRING);
          j = k;
        }
      }
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
      if (syn && match_kw(syn->kws, line + i, j - i, &k))
        n = push_span(out, n, max_out, i, j, k);
      else if (syn && (flags & F_STR) && j < len) {
        size_t k2 = j;
        while (k2 < len && (line[k2] == ' ' || line[k2] == '\t')) k2++;
        /* foo( → treat as call / type-ish accent (cyan in themes). */
        if (k2 < len && line[k2] == '(')
          n = push_span(out, n, max_out, i, j, HL_TYPE);
      }
      i = j;
      continue;
    }

    /* Operators / punctuation — keeps dense C looking less "plain". */
    if (syn && (flags & F_STR)) {
      char c = line[i];
      if (c == '{' || c == '}' || c == '(' || c == ')' || c == '[' || c == ']') {
        n = push_span(out, n, max_out, i, i + 1, HL_BRACKET);
        i++;
        continue;
      }
      if (c == '=' || c == '!' || c == '<' || c == '>' || c == '&' || c == '|' ||
          c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '^' ||
          c == '~' || c == '?' || c == ':' || c == ',') {
        size_t j = i + 1;
        if (j < len) {
          char nch = line[j];
          if ((c == '=' && nch == '=') || (c == '!' && nch == '=') ||
              (c == '<' && (nch == '=' || nch == '<')) ||
              (c == '>' && (nch == '=' || nch == '>')) ||
              (c == '&' && nch == '&') || (c == '|' && nch == '|') ||
              (c == '+' && nch == '+') || (c == '-' && nch == '-') ||
              (c == '-' && nch == '>') || (c == '<' && nch == '-'))
            j++;
        }
        n = push_span(out, n, max_out, i, j, HL_PREPROC);
        i = j;
        continue;
      }
    }

    i++;
  }

  if (out_ml) *out_ml = in_ml;
  return n;
}
