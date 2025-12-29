# Documentation

This `docs/` directory contains targeted documentation for two primary audiences:

- **Consumers** — people who want to use `typr-io` in their applications (quickstart, examples, API usage, runtime caveats).
- **Developers** — contributors and maintainers who need build instructions, architecture notes, testing guidance, and contribution guidelines.

If you're not sure where to start, pick the audience that best matches your intent.

## Quick links

- Consumers docs: `docs/consumers/README.md` — Quickstart, usage patterns, examples.
- Developers docs: `docs/developers/README.md` — Build, test, architecture, and contributing.
- Main project README: `README.md` — Platform-specific behaviour and overview.
- Examples: `examples/` — Example programs demonstrating typical usage.
- Test consumer: `test_consumer/` — Lightweight test harness used by CI and local checks.
- Public API headers: `include/typr-io/` (e.g., `include/typr-io/core.hpp`, `include/typr-io/keyboard/common.hpp`, `include/typr-io/keyboard/sender.hpp`, `include/typr-io/keyboard/listener.hpp`).

## Which doc should I read?

- Using the library (embedding, calling APIs, runtime caveats): start with `docs/consumers/README.md`.
- Developing the library (building, debugging, adding backends, contributing): start with `docs/developers/README.md`.

## Contributing docs

When you make changes that affect behavior, API, or developer workflows, please:

1. Update the appropriate doc (`consumers` or `developers`) under `docs/`.
2. Add or update examples in `examples/` when relevant.
3. Add tests or update `test_consumer/` where applicable.
4. Open a pull request describing the change and link the updated docs.

Doc contributions should be written in clear, concise Markdown and aim to help the intended audience complete common tasks quickly.

## Reporting issues & feedback

Please file bugs, feature requests, or questions in the project's issue tracker. For issues related to keyboard layouts or platform-specific behavior, include reproduction steps and relevant platform details (OS version, keyboard layout, permissions state).

## License

See `LICENSE` at the project root for license terms.

---

Thanks for helping make `typr-io` easier to use — contributions to both code and documentation are very welcome!
