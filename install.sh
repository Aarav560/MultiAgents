#!/usr/bin/env bash
# install.sh - standalone installer for the hivemind Claude Code plugin.
#
# Usage:
#   ./install.sh [--project] [--uninstall] [--help]
#
# Installs into ~/.claude by default. With --project, installs into
# ./.claude in the current directory instead. Use this when you are not
# using the Claude Code plugin marketplace (aarav560/multiagents).
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" >/dev/null 2>&1 && pwd -P)"
REPO_DIR="$SCRIPT_DIR"

TARGET="$HOME/.claude"
MODE="install"

usage() {
  cat <<'EOF'
install.sh - standalone installer for the hivemind Claude Code plugin

Usage:
  ./install.sh [--project] [--uninstall] [--help]

Options:
  --project    Install into ./.claude (current directory) instead of ~/.claude
  --uninstall  Remove the files this installer copies, and nothing else
  --help       Show this help and exit

This script must be run from a local clone of the repository (it resolves
paths relative to its own location). If you got this file by piping curl,
it will not work standalone: clone the repository instead, e.g.

  git clone https://github.com/aarav560/multiagents.git
  cd multiagents
  ./install.sh
EOF
}

for arg in "$@"; do
  case "$arg" in
    --project)
      TARGET="$(pwd)/.claude"
      ;;
    --uninstall)
      MODE="uninstall"
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $arg" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if ! command -v python3 >/dev/null 2>&1; then
  echo "Warning: python3 not found on PATH. hivemind's hive.py requires python3 to run." >&2
fi

SKILL_SRC="$REPO_DIR/skills/hive"
SKILL_DST="$TARGET/skills/hive"
AGENTS_DST="$TARGET/agents"
COMMANDS_DST="$TARGET/commands"

if [ "$MODE" = "uninstall" ]; then
  echo "Uninstalling hivemind from $TARGET ..."

  if [ -d "$SKILL_DST" ]; then
    rm -rf -- "$SKILL_DST"
    echo "  removed $SKILL_DST"
  fi

  if [ -d "$AGENTS_DST" ]; then
    for f in "$AGENTS_DST"/hive-*.md; do
      [ -e "$f" ] || continue
      rm -f -- "$f"
      echo "  removed $f"
    done
  fi

  if [ -d "$COMMANDS_DST" ]; then
    for f in "$COMMANDS_DST"/hive*.md; do
      [ -e "$f" ] || continue
      rm -f -- "$f"
      echo "  removed $f"
    done
  fi

  echo "Uninstall complete."
  exit 0
fi

if [ ! -d "$SKILL_SRC" ]; then
  echo "Error: $SKILL_SRC not found. Run this script from inside a clone of the repository." >&2
  exit 1
fi

echo "Installing hivemind into $TARGET ..."

mkdir -p -- "$SKILL_DST" "$AGENTS_DST" "$COMMANDS_DST"

rm -rf -- "$SKILL_DST"
cp -R "$SKILL_SRC" "$SKILL_DST"
echo "  copied skills/hive -> $SKILL_DST"

shopt -s nullglob
agent_files=("$REPO_DIR"/agents/hive-*.md)
if [ "${#agent_files[@]}" -eq 0 ]; then
  echo "  warning: no agents/hive-*.md files found to copy" >&2
else
  for f in "${agent_files[@]}"; do
    cp "$f" "$AGENTS_DST/"
    echo "  copied $(basename -- "$f") -> $AGENTS_DST/"
  done
fi

command_files=("$REPO_DIR"/commands/hive*.md)
if [ "${#command_files[@]}" -eq 0 ]; then
  echo "  warning: no commands/hive*.md files found to copy" >&2
else
  for f in "${command_files[@]}"; do
    cp "$f" "$COMMANDS_DST/"
    echo "  copied $(basename -- "$f") -> $COMMANDS_DST/"
  done
fi
shopt -u nullglob

echo ""
echo "Install complete."
echo "Next step:"
echo "  In Claude Code: /hive <goal>  (for standalone installs set \`agents: hive\` in plan.md to use the hive-* subagents)"
