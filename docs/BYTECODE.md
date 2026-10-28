# Private bytecode and symbol encoding

All fields are byte encoded, little endian. Addresses are absolute u16 arena
offsets, independent of host pointers. Code occupies `[code_start, program_end)`.
The compiler appends HALT even when the source has no END. Each nonblank source
statement emits LINE before its actual operation. Metadata and bytecode remain
in the same pool during execution. No bytecode file importer is provided.

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
| 23 | PRINT | none | Pop value and invoke output callback |

For `PRINT 2 + 3 * 4` on source line 1, the operations are
`LINE 1; CONST 2; CONST 3; CONST 4; MUL; ADD; PRINT; HALT`.
Calls evaluate arguments in source order and emit CALL only after all arguments.
Functions are surrounded by a main-execution JUMP over their body. Their symbol
entry points are patched during pass two; forward and recursive calls use the
same symbol offset and need no runtime name lookup. Function fallthrough emits
`CONST 0; RETURN`.

Symbol records are contiguous and use 11+name_length bytes:

| Offset | Encoding | Meaning |
| ---: | --- | --- |
| 0 | u8 | Kind: 1 global, 2 function, 3 local/parameter |
| 1 | u8 | Name length excluding terminator |
| 2 | u16 | Owner function symbol offset; 65535 for globals/functions |
| 4 | u8 | Global or local slot |
| 5 | u8 | Function parameter count |
| 6 | u8 | Function total local-slot count, including parameters |
| 7 | u8 | Reserved zero |
| 8 | u16 | Function bytecode entry |
| 10 | bytes | Uppercase ASCII name followed by NUL |

Record traversal uses its stored name length. Parameters are local symbols
allocated before LOCAL symbols, with slots in declaration order. Nonapplicable
record fields are zero. Symbol records are immutable after successful compilation.

Error codes are stable for this first stage: 0 OK, 1 syntax, 2 identifier length,
3 literal range, 4 undeclared symbol, 5 duplicate declaration, 6 arity,
7 pool memory, 8 compiler limit, 9 VM stack, 10 division by zero,
11 arithmetic overflow, 12 invalid bytecode, 13 instruction budget,
14 output callback, 15 no compiled program. The host CLI prints
`E<code> line <line>: <English diagnostic>` to stderr and exits with that code.
Host file/usage failures use 66/64. There is no result output before a compilation
error; a runtime error can follow already completed PRINT callbacks.

Runtime checks detect invalid/truncated operands and invalid slot/target ranges.
They are defensive checks on compiler output, not a complete proof for arbitrary
mutated byte streams. Applications must not treat SBasic.pool as an untrusted
executable import format.
