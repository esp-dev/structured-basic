# Structured BASIC 1.0

Each statement occupies one source line. LF and CRLF work. Keywords and
names are ASCII case insensitive. Names contain up to 31 characters,
begin with a letter or underscore and may end in `$`. `REM` and apostrophe
start comments. New keywords are contextual where they overlap older
identifiers. A user function can shadow a built-in of the same name.

## Values and declarations

`INTEGER` is signed 32-bit and the default. `SINGLE` is IEEE 754 binary32.
A decimal point or exponent makes a numeric literal SINGLE. `STRING`
stores bytes, including NUL when constructed with `CHR$(0)`. Use double
quotes for string literals and double a quote inside a literal. The default
string limit is 255 bytes. An identifier ending in `$` defaults to STRING.

`[LET] name = expression` writes a scalar. A main-level assignment declares
a global if necessary. Inside a routine an assignment resolves to a
parameter, local or declared global. Locals and parameters shadow globals.
`LOCAL name [AS type]` declares function-scoped cells initialized on entry.
`DIM name [AS type]` declares a scalar. `DIM name(count) [AS type]` allocates a
zero-initialized one-dimensional array at execution. Main-level DIM is global.
DIM inside a routine is local. Reexecuting DIM replaces the allocation. Index
from zero through count minus one. Bounds, allocation and type errors abort
the current run. A `CONST name [AS type] = expression` at main level initializes
an immutable scalar when execution reaches the declaration.

INTEGER promotes to SINGLE in mixed arithmetic and SINGLE assignments.
Use `CINT` or `INT` explicitly in the opposite direction. Array indices,
sizes and FOR control values require INTEGER. STRING and numeric values
require explicit conversions. Addition concatenates strings. Comparisons
of strings use unsigned byte lexicographic order.

Operators, from tightest to loosest, are unary sign, `* / MOD`, `+ -`,
comparisons `= <> < <= > >=`, `NOT`, `AND`, and `OR`. Binary operators within
one priority associate left to right. AND and OR short circuit. Zero is
false and a nonzero number is true. Logical and comparison results are
INTEGER zero or one. INTEGER division truncates toward zero, and MOD has
the dividend's sign. Floating MOD is a type error. Arithmetic overflow,
division by zero and invalid conversions return errors.

## Statements and routines

| Form | Behavior |
| --- | --- |
| `PRINT [item [; item or , item]…]` | Print values. A semicolon joins fields, a comma inserts a space and a trailing separator suppresses the newline. |
| `INPUT ["prompt";] target[, target…]` | Read one line per scalar target using its declared type. |
| `IF expr THEN`, `ELSEIF expr THEN`, `ELSE`, `END IF` | Execute the first true clause or the optional fallback. |
| `SELECT CASE expr`, `CASE expr[, expr…]`, `CASE ELSE`, `END SELECT` | Evaluate the selector once. Execute the first matching clause or fallback. |
| `WHILE expr`, `WEND` | Test before each iteration. |
| `FOR var = start TO limit [STEP step]`, `NEXT [var]` | Evaluate bounds and step once. The step defaults to 1 and cannot be zero. Supports either sign. Iteration stops at the boundary, including when advancing would exceed INTEGER range. |
| `DO [WHILE expr or UNTIL expr]`, `LOOP [WHILE expr or UNTIL expr]` | Test at the start or end, or repeat until exited. Use at most one test. |
| `REPEAT`, `UNTIL expr` | Test at the end and repeat while false. |
| `EXIT FOR`, `EXIT DO`, `EXIT WHILE` | Leave the nearest enclosing loop of the named kind. |
| `END`, `STOP` | End execution successfully. |

`FUNCTION name(arg [AS type], …) [AS type]` and
`SUB name(arg [AS type], …)` define top-level routines closed by
`END FUNCTION` and `END SUB`. Functions take `RETURN expression`,
produce a typed value, and default to zero or empty text on fallthrough.
SUB accepts bare `RETURN` or fallthrough. Call routines as `CALL name(…)`
or `name(…)` at statement level. Functions also appear in expressions.
Arguments are evaluated left to right and passed by value. Array elements
can be passed as values, but passing an entire array is rejected. Call
frames are limited and support forward, direct and mutual calls.

## Built-ins

- `ABS`, `MIN`, `MAX` and `SGN` take numbers. `INT` rounds down to INTEGER
  and `CINT` truncates toward zero. `CSNG` converts to SINGLE.
- `RND()` returns a SINGLE in [0,1). `SEED(integer)` resets its sequence
  within the current run.
- `LEN` counts bytes. `LEFT$(s,n)` and `RIGHT$(s,n)` clip lengths to the
  string. `MID$(s,start[,n])` starts at byte 1 and allows one past the end
  for an empty result. Negative lengths and other out-of-range positions fail.
- `CHR$(0..255)` creates one byte. `ASC` returns the first byte and rejects
  empty strings. `UCASE$` and `LCASE$` transform ASCII letters only.
- `STR$` formats a number, and `VAL` parses a whole numeric string as SINGLE.
  Invalid or out-of-range input returns a conversion error.
- `EEREAD`, `EEWRITE`, `PINMODE`, `DIGITALREAD`, `DIGITALWRITE`,
  `ANALOGREAD`, `DELAY` and `MILLIS` call platform services.

See [PLATFORM.md](PLATFORM.md) for host/AVR behavior.
