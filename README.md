## Overview
* Yet another lisp like language.
* Stack-based VM.

## Architecture
```
source code ---------> Token ----------> Value(Form/Number...)
             Scanner            Parser     |
                                           | Compiler
                                           |
                                           v
                                         Function(contains Chunk(a.k.a. bytecode))
                                           |
                                           | VM
                                           |
                                           v
                                         (result)
```
