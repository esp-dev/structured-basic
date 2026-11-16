# Contributing to Structured BASIC

Run the host tests in [BUILD.md](docs/BUILD.md) before opening a pull request.
For a language or compiler issue, include a small BASIC program and its
output or diagnostic. For an adapter issue, include the board, toolchain
and build profile.

Extend [the grammar tests](tests/grammar.py) when a syntax change affects
accepted source or operator grouping. Update [GRAMMAR.md](docs/GRAMMAR.md) for
syntax and [LANGUAGE.md](docs/LANGUAGE.md) for execution semantics. Keep the
core separate from platform I/O as described in [EMBEDDING.md](docs/EMBEDDING.md).
