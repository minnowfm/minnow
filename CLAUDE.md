# CLAUDE.md

Guidance for Claude Code (and similar coding agents) working in this repository.

## Project

Minnow is a Qt6/KDE Frameworks (KIO) file manager written in C++17. See `README.md` for
features, dependencies, and build instructions.

## graphify (optional, for saving tokens on big questions)

[graphify](https://github.com/Graphify-Labs/graphify) turns this codebase into a local,
queryable knowledge graph, so a broad question ("what connects `X` to `Y`", "what would
break if I changed `Z`") can be answered by a graph query instead of grepping/reading a
pile of files. It's not installed or wired into this session automatically — no
auto-running hook has been set up for it — so use it explicitly when it'd help:

1. Install it once per environment (it does not persist across fresh containers/sessions):
   ```sh
   uv tool install graphifyy      # or: pipx install graphifyy / pip install graphifyy
   ```
2. Build the graph for this repo (writes to `graphify-out/`, gitignored - regenerate as needed):
   ```sh
   graphify extract . --backend <gemini|kimi|claude|openai|deepseek|ollama>
   ```
   This needs an LLM backend/API key for the community-labeling step (the AST extraction
   itself is local/deterministic). Pick whichever backend has a key available in the
   environment; skip this whole section if none is available.
3. Query it instead of re-reading the whole tree:
   ```sh
   graphify query "what connects auth to the database?"
   graphify path "BrowserTab" "TaskManager"
   graphify explain "ThumbnailProxyModel"
   ```

Do not run `graphify install` / `graphify claude install` (or the equivalent for other
platforms) in this repo - those rewrite this file and install a PreToolUse hook that runs
automatically on every future tool call, which needs a human's explicit sign-off first,
not an agent's.
