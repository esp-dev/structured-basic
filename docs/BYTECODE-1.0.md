# BASIC 1.0 bytecode additions

This is an in-memory format. Opcodes 0–23 retain the operation numbers in
[BYTECODE.md](BYTECODE.md). Values are typed on the VM stack. Integer and
offset fields are little endian. SINGLE payloads use the native binary32
byte order on supported targets. The symbol header's byte 7 stores the
type (low two bits) plus ARRAY (4), CONST (8), SUB (16) and DECLARED (32).

| ID | Instruction | Operands |
| ---: | --- | --- |
| 24 | FCONST | binary32 |
| 25 | SCONST | length u16, then that many string bytes |
| 26–28 | ARRAY_GET, ARRAY_SET, DIM | symbol offset u16 |
| 29 | BUILTIN | built-in ID u8, arity u8 |
| 30–37 | DROP, WRITE, NEWLINE, SEPARATOR, NOT, BOOL, AND, OR | none |
| 38 | DUP | none |
| 39 | INPUT | symbol offset u16 |
| 40 | FOR | counter symbol u16 and exit address u16 |
| 41–44 | NEXT, SELECT, CASE, ENDSELECT | none |
| 45 | JNZ | branch target u16 |
| 46 | EXIT | control record pop count u8 and target u16 |

FOR stores loop bounds, step and body address. SELECT stores the evaluated
selector and releases it on block exit. CALL uses a symbol record and
copies arguments into a typed local frame. RETURN releases locals and
active control records before restoring the caller. Validation covers
format and range checks. Embedders must keep compiled bytes private.
