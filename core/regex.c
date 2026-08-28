/* mote core — compact backtracking regex (. * + ? ^ $ [] \d \w \s) */
#include "regex.h"
#include <ctype.h>

#define RE_MAX_STEPS 50000

static char buf_c(const Buf *b, size_t i) { return buf_at(b, i); }

static int ci_eq(mote_bool ci, char a, char b) {
  if (ci) {
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
  }
  return a == b;
}

static const char *pat_skip(const char *pat) {
  if (!pat || !*pat) return pat;
  if (*pat == '\\' && pat[1]) return pat + 2;
  if (*pat == '[') {
    pat++;
    if (*pat == '^') pat++;
    while (*pat && *pat != ']') {
      if (pat[1] == '-' && pat[2] && pat[2] != ']') pat += 3;
      else pat++;
    }
    if (*pat == ']') pat++;
    return pat;
  }
  return pat + 1;
}

static const char *pat_after_atom(const char *pat) {
  const char *p = pat_skip(pat);
  if (*p == '*' || *p == '+' || *p == '?') p++;
  return p;
}

static int re_class(const char **pp, char c, mote_bool ci) {
  const char *p = *pp;
  int neg = 0, ok = 0;
  if (*p == '^') {
    neg = 1;
    p++;
  }
  while (*p && *p != ']') {
    if (p[1] == '-' && p[2] && p[2] != ']') {
      char a = *p, b = p[2];
      if (ci) {
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      }
      if (c >= a && c <= b) ok = 1;
      p += 3;
    } else {
      if (ci_eq(ci, *p, c)) ok = 1;
      p++;
    }
  }
  if (*p == ']') p++;
  *pp = p;
  return neg ? !ok : ok;
}

static size_t re_atom(const Buf *b, size_t pos, size_t end, const char *pat,
                      mote_bool ci) {
  if (!*pat) return pos;
  if (pos >= end) return end + 1;
  if (*pat == '\\') {
    char c = buf_c(b, pos);
    pat++;
    if (!*pat) return end + 1;
    if (*pat == 'd') {
      if (!isdigit((unsigned char)c)) return end + 1;
      return pos + 1;
    }
    if (*pat == 'w') {
      if (!isalnum((unsigned char)c) && c != '_') return end + 1;
      return pos + 1;
    }
    if (*pat == 's') {
      if (c != ' ' && c != '\t' && c != '\r') return end + 1;
      return pos + 1;
    }
    if (*pat == 'n') {
      if (c != '\n') return end + 1;
      return pos + 1;
    }
    if (*pat == 't') {
      if (c != '\t') return end + 1;
      return pos + 1;
    }
    if (!ci_eq(ci, *pat, c)) return end + 1;
    return pos + 1;
  }
  if (*pat == '.') {
    if (buf_c(b, pos) == '\n') return end + 1;
    return pos + 1;
  }
  if (*pat == '[') {
    const char *p = pat + 1;
    char c = buf_c(b, pos);
    if (!re_class(&p, c, ci)) return end + 1;
    return pos + 1;
  }
  if (!ci_eq(ci, *pat, buf_c(b, pos))) return end + 1;
  return pos + 1;
}

static size_t re_seq(const Buf *b, size_t pos, size_t end, const char *pat,
                     mote_bool ci, int *steps);

static size_t re_repeat(const Buf *b, size_t pos, size_t end, const char *pat,
                        char q, mote_bool ci, int *steps) {
  const char *rest = pat_after_atom(pat);
  size_t t;
  if (q == '?') {
    t = re_seq(b, pos, end, rest, ci, steps);
    if (t <= end) return t;
    t = re_atom(b, pos, end, pat, ci);
    if (t > end) return end + 1;
    return re_seq(b, t, end, rest, ci, steps);
  }
  if (q == '+') {
    t = re_atom(b, pos, end, pat, ci);
    if (t > end) return end + 1;
    pos = t;
    for (;;) {
      size_t n;
      if (++(*steps) > RE_MAX_STEPS) return end + 1;
      n = re_seq(b, pos, end, rest, ci, steps);
      if (n <= end) return n;
      t = re_atom(b, pos, end, pat, ci);
      if (t > end || t == pos) break;
      pos = t;
    }
    return end + 1;
  }
  for (t = pos;;) {
    size_t n;
    if (++(*steps) > RE_MAX_STEPS) return end + 1;
    n = re_seq(b, t, end, rest, ci, steps);
    if (n <= end) return n;
    if (t >= end) break;
    t = re_atom(b, t, end, pat, ci);
    if (t > end || t == pos) break;
  }
  return end + 1;
}

static size_t re_seq(const Buf *b, size_t pos, size_t end, const char *pat,
                     mote_bool ci, int *steps) {
  const char *nxt;
  size_t t;
  while (*pat) {
    if (++(*steps) > RE_MAX_STEPS) return end + 1;
    if (*pat == '$' && !pat[1]) return pos == end ? pos : end + 1;
    nxt = pat_skip(pat);
    if (*nxt == '*') return re_repeat(b, pos, end, pat, '*', ci, steps);
    if (*nxt == '+') return re_repeat(b, pos, end, pat, '+', ci, steps);
    if (*nxt == '?') return re_repeat(b, pos, end, pat, '?', ci, steps);
    t = re_atom(b, pos, end, pat, ci);
    if (t > end) return end + 1;
    pos = t;
    pat = nxt;
  }
  return pos;
}

size_t re_match_buf(const Buf *b, size_t pos, const char *pat, mote_bool caseless) {
  size_t end, hit;
  int steps = 0;
  if (!b || !pat || !pat[0]) return 0;
  end = buf_len(b);
  if (pos > end) return 0;
  if (*pat == '^') {
    hit = re_seq(b, pos, end, pat + 1, caseless, &steps);
    return (hit > pos && hit <= end) ? hit - pos : 0;
  }
  hit = re_seq(b, pos, end, pat, caseless, &steps);
  return (hit > pos && hit <= end) ? hit - pos : 0;
}
