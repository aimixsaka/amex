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

This project serves as a platform for me to practice and experiment with new ideas and concepts, such as `Data Oriented Design`, `Arena allocator`...

That said, it's a project just for fun.

There are several problems in this interpreter:

- Did too many things at runtime, say, e.g, why change argument number at runtime ?
- Should not use jmp_buf and longjump.
- Several num restriction, say, e.g, 255 arguments.
- Security problem.

But at least it did one thing right: Don't leak memory.

## Reference

- [Janet](https://github.com/janet-lang/janet)

  A small and beautiful programming language & bytecode vm

- [Crafting Interpreters](https://craftinginterpreters.com/contents.html)

  Great book for learning to write interpreter
