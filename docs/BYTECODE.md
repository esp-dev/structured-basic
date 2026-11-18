# BASIC 1.0 private bytecode and symbols

Integer and offset fields use little-endian byte encoding. SINGLE immediates
use the native binary32 byte order of the compiler and VM. Addresses are
absolute u16 arena offsets. Code occupies `[code_start, program_end)`. The
compiler appends HALT even when the source has no END and emits LINE
instructions for diagnostics. Metadata, code and runtime data share the
arena. Bytecode stays private to the interpreter instance.

| Opcode | Name | Immediate bytes | Stack effect / behavior |
| ---: | --- | --- | --- |
| 0 | HALT | none | Stop whole program successfully |
| 1 | LINE | line:u16 | Update diagnostic source line |
| 2 | CONST | value:i32 | Push one integer |
| 3 | LOAD_G | slot:u8 | Push global value |
| 4 | STORE_G | slot:u8 | Pop and store global |
| 5 | LOAD_L | slot:u8 | Push local/parameter value |
| 6 | STORE_L | slot:u8 | Pop and store local/parameter |
| 7 | ADD | none | Pop b,a; push a+b, checked |
| 8 | SUB | none | Pop b,a; push a-b, checked |
| 9 | MUL | none | Pop b,a; push a*b, checked |
| 10 | DIV | none | Pop b,a; push a/b, checked |
| 11 | MOD | none | Pop b,a; push a%b, checked |
| 12 | NEG | none | Pop a; push -a, checked |
| 13 | EQ | none | Pop b,a; push a==b |
| 14 | NE | none | Pop b,a; push a!=b |
| 15 | LT | none | Pop b,a; push a<b |
| 16 | LE | none | Pop b,a; push a<=b |
| 17 | GT | none | Pop b,a; push a>b |
| 18 | GE | none | Pop b,a; push a>=b |
| 19 | JUMP | target:u16 | Set PC to target |
| 20 | JZ | target:u16 | Pop condition; jump if zero |
| 21 | CALL | function_symbol:u16, argc:u8 | Consume args, enter frame; return one value |
| 22 | RETURN | none | Require one result; release frame, restore caller, push result |
| 23 | PRINT | none | Pop value and print it with a newline |
| 24 | FCONST | value:binary32 | Push a SINGLE |
| 25 | SCONST | length:u16, followed by length bytes | Push a STRING |
| 26 | ARRAY_GET | symbol:u16 | Pop an index and push the element |
| 27 | ARRAY_SET | symbol:u16 | Pop value and index, then store the element |
| 28 | DIM | symbol:u16 | Pop count and allocate or replace an array |
| 29 | BUILTIN | id:u8, argc:u8 | Consume arguments and push the result |
| 30 | DROP | none | Pop and release one value |
| 31 | WRITE | none | Pop value and print it |
| 32 | NEWLINE | none | Print a newline |
| 33 | SEPARATOR | none | Print a space |
| 34 | NOT | none | Pop a condition and push the negated truth value |
| 35 | BOOL | none | Pop a condition and push 0 or 1 |
| 36 | AND | none | Combine two already evaluated operands |
| 37 | OR | none | Combine two already evaluated operands |
| 38 | DUP | none | Duplicate the top operand |
| 39 | INPUT | symbol:u16 | Read and store a value via the input callback |
| 40 | FOR | counter_symbol:u16, exit:u16 | Pop step, limit and start, then enter or skip the loop |
| 41 | NEXT | none | Advance the active FOR or release its control record |
| 42 | SELECT | none | Pop and retain the selector in a control record |
| 43 | CASE | none | Compare a popped value against the selector and push 0 or 1 |
| 44 | ENDSELECT | none | Release the SELECT control record |
| 45 | JNZ | target:u16 | Pop condition and jump if nonzero |
| 46 | EXIT | control_count:u8, target:u16 | Unwind control records and jump |
| 47 | NATIVE | registry_index:u8, argc:u8 | Consume arguments and push the native result |

For `PRINT 2 + 3 * 4` on source line 1, the operations are
`LINE 1; CONST 2; CONST 3; CONST 4; MUL; ADD; PRINT; HALT`.
Calls evaluate arguments in source order. A JUMP skips a routine's body
during main execution, and the compiler records the entry point in the
routine symbol. Fallthrough returns numeric zero or the empty string.
RETURN releases locals and active control records before restoring the caller.
Expression AND and OR are compiled to conditional jumps to short-circuit the
right-hand side. A multi-item CASE uses BC_OR to combine evaluated comparisons.

Native indices are local to the associated registry. The validator checks
the index and argument count against that registry.

Symbol records are contiguous and use 11+name_length bytes:

| Offset | Encoding | Meaning |
| ---: | --- | --- |
| 0 | u8 | Kind: 1 global, 2 function, 3 local/parameter |
| 1 | u8 | Name length excluding terminator |
| 2 | u16 | Owner function symbol offset; 65535 for globals/functions |
| 4 | u8 | Global or local slot |
| 5 | u8 | Function parameter count |
| 6 | u8 | Function total local-slot count, including parameters |
| 7 | u8 | Type and flags |
| 8 | u16 | Function bytecode entry |
| 10 | bytes | Uppercase ASCII name followed by NUL |

The low two bits of byte 7 are INTEGER (0), SINGLE (1) or STRING (2).
The remaining flags are ARRAY (4), CONST (8), SUB (16) and DECLARED (32).
Record traversal uses its stored name length. Parameters are local symbols
allocated before LOCAL symbols, with slots in declaration order. Nonapplicable
record fields are zero. Symbol records are immutable after successful compilation.

The current error codes are declared in [sbasic.h](../include/sbasic.h).
The validator checks structure and ranges for compiler-produced bytecode.
Keep `SBasic.pool` private. It is an execution arena rather than a serialized
file format.
