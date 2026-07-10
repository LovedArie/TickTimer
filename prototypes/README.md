# prototypes/

Throwaway proof-of-concept code.

Larman lists "Prototypes and proof-of-concepts" as inception artifacts in
*Applying UML and Patterns* (Ch. 4 §4.3, Table 4.1). Their job is to
**clarify the vision** and **validate technical ideas** — for example,
answering "Does library X actually do what its docs claim?"

Prototypes are **not production code**. They live here, separate from `src/`,
so:

1. You don't accidentally ship them.
2. You feel free to write ugly throwaway code without polluting the real codebase.

## Typical use cases

- Testing if a third-party library works for your scenario before committing to it.
- Spike experiments to estimate how hard a feature will be.
- UI mockups or sketches.

## When to delete a prototype

Once it has answered its question. Don't keep them around "just in case."
