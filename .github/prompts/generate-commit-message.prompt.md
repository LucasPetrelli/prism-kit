---
name: "Generate Commit Message"
description: "Generate a repo-style commit message from the current changes using a tag: action header and optional file-aware explanation"
argument-hint: "Optional intent override or emphasis"
agent: "agent"
---
Generate a commit message for the current workspace changes.

Use this repository format:

- Required header: `tag: action`
- Optional body: explain how the action was achieved by summarizing the touched files or change areas

Requirements:

- Inspect the current changed files and their diffs before writing the message.
- Pick the single best tag for the overall intent of the change.
- Keep the header imperative and specific.
- Include the optional body only when it adds real value.
- If a body is needed, prefer either one short paragraph or flat `-` bullets.
- Mention the concrete files or subsystems changed when that improves clarity.
- Ignore unrelated workspace noise and generated artifacts.
- If there are no meaningful changes, say so instead of inventing a message.

Preferred tags:

- `feat`
- `fix`
- `refactor`
- `docs`
- `build`
- `test`
- `ci`
- `chore`

Output only the commit message text, ready to copy into git.