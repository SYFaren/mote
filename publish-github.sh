#!/bin/sh
# Publish mote + mote-site to GitHub (repos, Pages, Releases).
# Run:  sh publish-github.sh
# Needs: git, gh (logged in), and `make release` (fills dist-release/).

set -eu

MOTE_DIR="${MOTE_DIR:-$HOME/Projects/mote}"
SITE_DIR="${SITE_DIR:-$HOME/Projects/mote-site}"
OWNER="${OWNER:-SYFaren}"
MOTE_REPO="${MOTE_REPO:-mote}"
SITE_REPO="${SITE_REPO:-mote-site}"
TAG="${TAG:-v2.0.0}"
RELEASE_TITLE="${RELEASE_TITLE:-$TAG}"

die() { echo "error: $*" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "need '$1' in PATH"; }
ask() {
  printf '%s [y/N] ' "$1"
  read -r ans || ans=
  case "$ans" in y|Y|yes|YES) return 0 ;; *) return 1 ;; esac
}

need git
need gh
gh auth status >/dev/null 2>&1 || die "gh not logged in (run: gh auth login)"

[ -d "$MOTE_DIR/.git" ] || die "not a git repo: $MOTE_DIR"
[ -d "$SITE_DIR/.git" ] || die "not a git repo: $SITE_DIR"
[ -f "$MOTE_DIR/dist-release/SHA256SUMS" ] || die "missing dist-release (run: make release)"
[ -f "$MOTE_DIR/dist-release/mote-all-platforms.zip" ] || die "missing mote-all-platforms.zip"

FLAT="$MOTE_DIR/dist-release/flat"
ASSETS="$MOTE_DIR/dist-release/mote-all-platforms.zip $MOTE_DIR/dist-release/SHA256SUMS"
for f in \
  mote-linux-x11 mote-linux-x11.upx \
  mote-linux-wayland mote-linux-wayland.upx \
  mote-linux-sdl2 mote-linux-sdl2.upx \
  mote-linux-sdl3 \
  mote-linux-console mote-linux-console.upx \
  mote-linux-fbdev mote-linux-fbdev.upx \
  mote-windows-gui.exe mote-windows-gui.upx.exe \
  mote-windows-console.exe mote-windows-console.upx.exe \
  mote-dos.exe mote-dos.upx.exe \
  mote.html mote.js mote.wasm mote.data
do
  p="$FLAT/$f"
  if [ -f "$p" ]; then
    ASSETS="$ASSETS $p"
  fi
done

echo "=== plan ==="
echo "  owner:     $OWNER"
echo "  editor:    $MOTE_DIR  -> github.com/$OWNER/$MOTE_REPO"
echo "  site:      $SITE_DIR  -> github.com/$OWNER/$SITE_REPO"
echo "  pages:     https://$(echo "$OWNER" | tr '[:upper:]' '[:lower:]').github.io/$SITE_REPO/"
echo "  release:   $TAG"
echo "  assets:"
for a in $ASSETS; do echo "    - $(basename "$a")"; done
echo
ask "Continue?" || { echo "aborted."; exit 0; }

commit_if_needed() {
  dir="$1"
  msg="$2"
  cd "$dir"
  git add -A
  if git diff --cached --quiet && [ -z "$(git ls-files --others --exclude-standard)" ]; then
    if git rev-parse --verify HEAD >/dev/null 2>&1; then
      echo "  (no new changes in $dir)"
      return 0
    fi
  fi
  git add -A
  if git diff --cached --quiet; then
    die "nothing to commit in $dir"
  fi
  git commit -m "$msg"
}

ensure_remote_and_push() {
  dir="$1"
  name="$2"
  desc="$3"
  cd "$dir"

  if git remote get-url origin >/dev/null 2>&1; then
    echo "  remote origin already set: $(git remote get-url origin)"
  else
    if gh repo view "$OWNER/$name" >/dev/null 2>&1; then
      git remote add origin "https://github.com/$OWNER/$name.git"
    else
      gh repo create "$OWNER/$name" --public --source=. --remote=origin --description "$desc"
    fi
  fi

  branch="$(git branch --show-current 2>/dev/null || true)"
  if [ -z "$branch" ]; then
    git branch -M main
    branch=main
  fi
  git push -u origin "$branch"
}

echo
echo "=== 1/4  mote (editor) ==="
commit_if_needed "$MOTE_DIR" "mote: platform overlays, categorized release layout."
ensure_remote_and_push "$MOTE_DIR" "$MOTE_REPO" "Tiny multi-platform text editor — C89 core + overlays"

echo
echo "=== 2/4  mote-site ==="
commit_if_needed "$SITE_DIR" "mote download site — platform gallery."
ensure_remote_and_push "$SITE_DIR" "$SITE_REPO" "Landing page + screenshots for mote"

echo
echo "=== 3/4  GitHub Pages ==="
if gh api "repos/$OWNER/$SITE_REPO/pages" >/dev/null 2>&1; then
  echo "  Pages already enabled."
else
  gh api -X POST "repos/$OWNER/$SITE_REPO/pages" \
      -f build_type=legacy \
      -f source[branch]=main \
      -f source[path]=/ >/dev/null 2>&1 || \
    echo "  WARN: enable Pages in repo settings (main /)."
fi
SITE_URL="https://$(echo "$OWNER" | tr '[:upper:]' '[:lower:]').github.io/$SITE_REPO/"
echo "  site URL: $SITE_URL"

echo
echo "=== 4/4  Release $TAG ==="
NOTES="$(cat <<EOF
## mote $TAG

Prefer **\`mote-all-platforms.zip\`** — binaries sorted by OS/backend:

\`\`\`
by-platform/linux/{x11,wayland,sdl2,console,fbdev}/
by-platform/windows/{gui,console}/
by-platform/dos/
by-platform/web/wasm/
\`\`\`

Individual assets under \`flat/\` names are also attached for direct download.

| Asset | Platform |
|-------|----------|
| \`mote-linux-x11\` | Linux X11 |
| \`mote-linux-wayland\` | Linux Wayland |
| \`mote-linux-sdl2\` | Linux SDL2 |
| \`mote-linux-console\` | Unix TTY |
| \`mote-linux-fbdev\` | Linux \`/dev/fb0\` |
| \`mote-windows-gui.exe\` | Windows GUI |
| \`mote-windows-console.exe\` | Windows console |
| \`mote-dos.exe\` | FreeDOS / DOSBox |
| \`mote.html\` + \`.js\` + \`.wasm\` | WebAssembly |
| \`*.upx\` | UPX-packed variants |

ANSI C89 core; overlay chosen at compile time.
EOF
)"

if gh release view "$TAG" --repo "$OWNER/$MOTE_REPO" >/dev/null 2>&1; then
  echo "  release $TAG already exists."
  ask "Upload/replace assets on existing release?" || {
    echo "  skipped release upload."
    echo "Done.  repo: https://github.com/$OWNER/$MOTE_REPO"
    exit 0
  }
  # shellcheck disable=SC2086
  gh release upload "$TAG" $ASSETS --repo "$OWNER/$MOTE_REPO" --clobber
else
  # shellcheck disable=SC2086
  gh release create "$TAG" $ASSETS \
    --repo "$OWNER/$MOTE_REPO" \
    --title "$RELEASE_TITLE" \
    --notes "$NOTES"
fi

echo
echo "=== done ==="
echo "  editor:  https://github.com/$OWNER/$MOTE_REPO"
echo "  site:    $SITE_URL"
echo "  release: https://github.com/$OWNER/$MOTE_REPO/releases/tag/$TAG"
