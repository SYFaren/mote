#!/bin/sh
# Publish mote + mote-site to GitHub (repos, Pages, Releases).
# Run yourself:  sh ~/Projects/mote/publish-github.sh
#
# Needs: git, gh (logged in), existing dist-release/ binaries.

set -eu

MOTE_DIR="${MOTE_DIR:-$HOME/Projects/mote}"
SITE_DIR="${SITE_DIR:-$HOME/Projects/mote-site}"
OWNER="${OWNER:-SYFaren}"
MOTE_REPO="${MOTE_REPO:-mote}"
SITE_REPO="${SITE_REPO:-mote-site}"
TAG="${TAG:-v0.1.0}"
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
[ -d "$MOTE_DIR/dist-release" ] || die "missing $MOTE_DIR/dist-release (build first)"

ASSETS="
$MOTE_DIR/dist-release/mote-linux
$MOTE_DIR/dist-release/mote-linux-packed
$MOTE_DIR/dist-release/mote-windows.exe
$MOTE_DIR/dist-release/mote-windows-packed.exe
$MOTE_DIR/dist-release/mote-x-linux
$MOTE_DIR/dist-release/mote-x-linux-packed
$MOTE_DIR/dist-release/mote-x-windows.exe
$MOTE_DIR/dist-release/mote-x-windows-packed.exe
"
for f in $ASSETS; do
  [ -f "$f" ] || die "missing asset: $f"
done

echo "=== plan ==="
echo "  owner:     $OWNER"
echo "  editor:    $MOTE_DIR  -> github.com/$OWNER/$MOTE_REPO"
echo "  site:      $SITE_DIR  -> github.com/$OWNER/$SITE_REPO"
echo "  pages:     https://$(echo "$OWNER" | tr '[:upper:]' '[:lower:]').github.io/$SITE_REPO/"
echo "  release:   $TAG  (assets from dist-release/)"
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
  # empty repo / first commit: ensure something is staged
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
      echo "  repo exists, adding origin..."
      git remote add origin "https://github.com/$OWNER/$name.git"
    else
      echo "  creating github.com/$OWNER/$name ..."
      gh repo create "$OWNER/$name" --public --source=. --remote=origin --description "$desc"
    fi
  fi

  branch="$(git branch --show-current 2>/dev/null || true)"
  if [ -z "$branch" ]; then
    git branch -M main
    branch=main
  fi

  echo "  pushing $branch ..."
  git push -u origin "$branch"
}

echo
echo "=== 1/4  mote (editor) ==="
commit_if_needed "$MOTE_DIR" "Initial mote and mote-x."
ensure_remote_and_push "$MOTE_DIR" "$MOTE_REPO" "Tiny GUI text editors (mote + mote-x)"

echo
echo "=== 2/4  mote-site ==="
commit_if_needed "$SITE_DIR" "mote download site with real screenshots."
ensure_remote_and_push "$SITE_DIR" "$SITE_REPO" "Landing page + screenshots for mote"

echo
echo "=== 3/4  GitHub Pages ==="
if gh api "repos/$OWNER/$SITE_REPO/pages" >/dev/null 2>&1; then
  echo "  Pages already enabled."
else
  # legacy static from branch
  if gh api -X POST "repos/$OWNER/$SITE_REPO/pages" \
      -f build_type=legacy \
      -f source[branch]=main \
      -f source[path]=/ >/dev/null 2>&1; then
    echo "  Pages enabled (main /)."
  else
    echo "  WARN: could not enable Pages via API."
    echo "  Do it in UI: $SITE_REPO → Settings → Pages → Deploy from branch → main → /"
  fi
fi
SITE_URL="https://$(echo "$OWNER" | tr '[:upper:]' '[:lower:]').github.io/$SITE_REPO/"
echo "  site URL: $SITE_URL"

echo
echo "=== 4/4  Release $TAG ==="
if gh release view "$TAG" --repo "$OWNER/$MOTE_REPO" >/dev/null 2>&1; then
  echo "  release $TAG already exists."
  ask "Upload/replace assets on existing release?" || {
    echo "  skipped release upload."
    echo
    echo "Done."
    echo "  repo:  https://github.com/$OWNER/$MOTE_REPO"
    echo "  site:  $SITE_URL"
    exit 0
  }
  # clobber assets on existing release
  # shellcheck disable=SC2086
  gh release upload "$TAG" $ASSETS --repo "$OWNER/$MOTE_REPO" --clobber
else
  # shellcheck disable=SC2086
  gh release create "$TAG" $ASSETS \
    --repo "$OWNER/$MOTE_REPO" \
    --title "$RELEASE_TITLE" \
    --notes "First public release: mote + mote-x (Linux / Windows, packed and unpacked)."
fi

echo
echo "=== done ==="
echo "  editor:  https://github.com/$OWNER/$MOTE_REPO"
echo "  site:    $SITE_URL"
echo "  release: https://github.com/$OWNER/$MOTE_REPO/releases/tag/$TAG"
echo
echo "Download buttons on the site appear after the release assets are public."
