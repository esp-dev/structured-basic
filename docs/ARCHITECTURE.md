# Compiler and VM architecture

The two-pass compiler first discovers lexical declarations and then
lowers expressions and statements into bytecode. The expression operator
stack implements precedence without C recursion. A block stack records
pending branches, exits and control targets. AND and OR lower to conditional
branches around the right operand. Functions resolve by symbol offset,
including forward calls.

The VM validates symbol records, instruction widths, and referenced
instruction boundaries before dispatch. It checks operand, slot, array,
object and frame bounds during execution. Runtime errors abort one run.
Strings use immutable references, and array operations check allocation,
index and element type. See [MEMORY.md](MEMORY.md).

The core consists of compiler, validator, VM, heap, value operations,
built-ins, diagnostics and optional platform storage helpers. The standard
shell in `terminal.c` is separately selectable. See [EMBEDDING.md](EMBEDDING.md).
