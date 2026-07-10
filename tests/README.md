# tests/

Unit and integration tests live here.

## Choosing a framework

Your books don't cover C++ test frameworks (they're tooling, not language).
The three commonly used options today are:

- **doctest** — single header file, easiest for beginners. Recommended start.
- **Catch2** — also single header, slightly more features.
- **GoogleTest** — Google's framework, used in many large codebases.

Pick one when you're ready. Add it via CMake's `FetchContent` (search
"CMake FetchContent doctest").

## Empty for now

This folder exists in the template so the *structure* is there even before
you start writing tests. As soon as you have one function worth testing,
add a test file here.
