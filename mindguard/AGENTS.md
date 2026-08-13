# MindGuard Agent Instructions

## Communication

- All user-facing messages, progress reports, explanations, and final reports **must be in Russian**. This `AGENTS.md` file is written in English.
- API names, code identifiers, file paths, CLI flags, and code examples remain in English.

## Plan and Execution State

- Before making any change, read `plan.md` and `status.md`.
- `plan.md` is the source of truth for product and architecture decisions. `status.md` is the source of truth for the current execution state.
- If task requirements conflict with `plan.md`, do not silently change the architecture. Record the discrepancy and ask the user for a decision.
- If either required document is unavailable, report that fact and obtain the user's direction before proceeding when the task permits.
- After every meaningful milestone, update `status.md` with only work actually completed and checks actually run. Follow the designated status owner's workflow; do not alter the file merely to claim unverified progress.

## Product Scope

- The current product is a local-only application protection SDK consisting of an in-process runtime SDK and an external Rust CLI protector.
- A web panel, dashboard, and mandatory backend are out of scope. Do not introduce them as required components or imply that they exist.

## Security Requirements

- Never commit, embed, log, fixture, or provide example private signing keys. Never place private keys in binary artifacts. Public keys are permitted only where the protocol explicitly requires them.
- Do not promise absolute or unbreakable protection. Tie every security claim to the documented threat model and verified tests.
- Every change to the protection protocol must include negative/tamper test coverage and compatibility coverage for both independent Rust and C++ SDKs.

## Documentation and Safe Changes

- When a task concerns a library, framework, SDK, API, CLI tool, or cloud service, consult current documentation through Context7 before relying on library-specific knowledge.
- Do not use destructive operations or overwrite user changes without explicit confirmation.
