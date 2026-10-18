# Agent Output

- Store transient validation evidence, raw command logs, manifests, diff bundles, and review packets under the project-root `logs/` directory.
- Do not create generated evidence under the project-root `evidence/` directory. The root `.gitignore` ignores both locations, along with the legacy `log/` directory.
