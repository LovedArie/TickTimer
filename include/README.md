# include/

Public header files (`.h` or `.hpp`) live here.

## What goes in a header (Stroustrup, Ch. 15 §15.2.2)

- Type definitions (`struct`, `class`)
- Function *declarations* (`extern int strlen(const char*);`)
- `inline` and `constexpr` function definitions
- Template declarations and definitions
- `const` and `constexpr` variable definitions
- Type aliases (`using value_type = long;`)
- Enumerations
- `#include` directives and macro definitions

## What does NOT go in a header

- Ordinary function definitions (put these in `.cpp`)
- Plain data definitions like `int a;` (put these in `.cpp`)
- `using namespace Foo;` directives (these pollute every file that includes
  the header)
- Unnamed namespaces

## Always use include guards

Every header should start with `#pragma once` (modern, simpler) OR the
classic `#ifndef / #define / #endif` pattern. This prevents the header from
being included multiple times in the same translation unit.

```cpp
#pragma once

namespace MyApp {
    void greet();
}
```
