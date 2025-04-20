## Overview

- Janet inspired syntax (nearly subset lol)
- Stack-based VM
- Learning purpose toy language
- Lisp explanation of `amex`

## Status

**A lot** can be done: more compact data(data oriented design)
, optimization(computed goto, nan boxing...)...

Always WIP...

## Summary
There are several problems in this interpreter:

- Did too many things at runtime, say, e.g, why change argument number at runtime ?
- Should not use jmp_buf and longjump.
- Several num restriction, say, e.g, 255 arguments.
- Security problem.

But at least it did one thing right: Don't leak memory.
## Reference

- [Janet](https://github.com/janet-lang/janet)
- [Crafting Interpreters](https://craftinginterpreters.com/contents.html)
