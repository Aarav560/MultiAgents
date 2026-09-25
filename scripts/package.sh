#!/usr/bin/env bash
# Builds the upload packages for Claude Cowork / claude.ai from the repository sources:
#   dist/hivemind-plugin.zip  the whole plugin (skill + agents + commands + hook)
#   dist/hive-skill.zip       only the hive skill (a hive/ folder with SKILL.md at its top)
# Run from anywhere: ./scripts/package.sh
set -euo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist="$repo/dist"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

command -v zip >/dev/null || { echo "package.sh: needs 'zip'" >&2; exit 1; }
mkdir -p "$dist"

# Plugin: only what the plugin needs (no examples, demos or tests).
plugin="$work/hivemind"
mkdir -p "$plugin"
cp -R "$repo/.claude-plugin" "$repo/skills" "$repo/agents" "$repo/commands" "$repo/hooks" "$plugin/"
cp "$repo/LICENSE" "$plugin/"
cp "$repo/GUIDE.md" "$plugin/README.md"
rm -f "$plugin/.claude-plugin/marketplace.json"
find "$plugin" -name '__pycache__' -type d -prune -exec rm -rf {} +
rm -f "$dist/hivemind-plugin.zip"
(cd "$plugin" && zip -qr -X "$dist/hivemind-plugin.zip" .)

# Skill: the hive folder itself, so SKILL.md sits at hive/SKILL.md inside the zip.
cp -R "$repo/skills/hive" "$work/hive"
find "$work/hive" -name '__pycache__' -type d -prune -exec rm -rf {} +
rm -f "$dist/hive-skill.zip"
(cd "$work" && zip -qr -X "$dist/hive-skill.zip" hive)

echo "built:"
ls -l "$dist"/*.zip | awk '{print "  " $NF "  (" $5 " bytes)"}'
