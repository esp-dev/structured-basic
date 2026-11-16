# Structured BASIC 1.0 grammar (EBNF)

This grammar describes the source accepted by the lexer, declaration pass and
statement compiler in [compiler.c](../src/compiler.c). The predicates below
cover name resolution, arity and other checks that depend on the enclosing
program. Code that compiles can still report a type, bounds or arithmetic error
when it runs. See [LANGUAGE.md](LANGUAGE.md) for execution semantics.

In the notation below, `[ … ]` means optional and `{ … }` means zero or more.
A comma between grammar terms means sequence. Quoted text denotes source
tokens. A question-mark pair describes a terminal or lexical condition.
Whitespace and comments are removed before parsing, except that a line feed
produces a `newline` token. The STM32, Windows and AVR shell commands belong
to the [platform interfaces](PLATFORM.md).

## Program and statements

```ebnf
program          = { newline | top-level, newline }, [ top-level ], eof ;
top-level        = const-declaration | function-definition | sub-definition
                 | statement ;
body             = { newline | statement, newline } ;

statement        = assignment | invocation-statement | print-statement
                 | input-statement | dim-declaration | local-declaration
                 | if-block | select-block | while-block | for-block
                 | do-block | repeat-block | exit-statement | return-statement
                 | "END" | "STOP" ;

assignment       = [ "LET" ], target, "=", expression ;
target           = identifier, [ "(", expression, ")" ] ;
invocation-statement
                 = [ "CALL" ], application ;

print-statement  = "PRINT",
                   [ expression, { print-separator, expression }, [ print-separator ] ] ;
print-separator  = ";" | "," ;
input-statement  = "INPUT", [ string-literal, ";" ], identifier, { ",", identifier } ;

const-declaration = "CONST", identifier, [ type-clause ], "=", expression ;
dim-declaration   = "DIM", declaration, { ",", declaration } ;
local-declaration = "LOCAL", declaration, { ",", declaration } ;
declaration       = identifier, [ "(", expression, ")" ], [ type-clause ] ;
type-clause       = "AS", ( "INTEGER" | "SINGLE" | "STRING" ) ;

function-definition
                 = "FUNCTION", identifier, parameter-list, [ type-clause ],
                   newline, body, "END", "FUNCTION" ;
sub-definition   = "SUB", identifier, parameter-list, [ "AS", "INTEGER" ],
                   newline, body, "END", "SUB" ;
parameter-list   = "(", [ parameter, { ",", parameter } ], ")" ;
parameter        = identifier, [ type-clause ] ;
return-statement = "RETURN", [ expression ] ;

if-block         = "IF", expression, "THEN", newline, body,
                   { "ELSEIF", expression, "THEN", newline, body },
                   [ "ELSE", newline, body ], "END", "IF" ;
select-block     = "SELECT", "CASE", expression, newline, { newline },
                   ( case-arm, { case-arm }, [ else-arm ] | else-arm ),
                   "END", "SELECT" ;
case-arm         = "CASE", expression, { ",", expression }, newline, body ;
else-arm         = "CASE", "ELSE", newline, body ;

while-block      = "WHILE", expression, newline, body, "WEND" ;
for-block        = "FOR", identifier, "=", expression, "TO", expression,
                   [ "STEP", expression ], newline, body, "NEXT", [ identifier ] ;
do-block         = "DO", ( loop-test, newline, body, "LOOP"
                          | newline, body, "LOOP", [ loop-test ] ) ;
loop-test        = ( "WHILE" | "UNTIL" ), expression ;
repeat-block     = "REPEAT", newline, body, "UNTIL", expression ;
exit-statement   = "EXIT", ( "FOR" | "DO" | "WHILE" ) ;
```

### Placement rules

- A blank line, comment-only line or empty source is valid. A final line feed
  is optional. Each statement, header and closing clause has its own line.
  A colon, a line number, a single-line IF or `ELSE IF` cannot take the place
  of the corresponding grammar tokens.
- `CONST`, `FUNCTION` and `SUB` are top-level constructs. `LOCAL` and `RETURN`
  require an enclosing routine. `RETURN` requires an expression in a FUNCTION
  and takes no expression in a SUB. `END` and `STOP` can appear in either scope
  or inside a block. Even lines after them are compiled.
- An optional name on `NEXT` must resolve to the counter of the FOR being
  closed. `EXIT FOR` and `EXIT WHILE` require the corresponding enclosing loop.
  `EXIT DO` can exit either a DO or a REPEAT because both use the same compiler
  block kind. An EXIT targets the nearest enclosing block of the named kind.
- An INPUT prompt is a single string literal followed immediately by a
  semicolon at token level. The declaration pass rejects an expression in that
  position, including a concatenation. Targets are one or more scalar names.
- `CASE ELSE` can be the only arm. The CASE token must have its statement
  meaning. The same applies to the closing UNTIL of REPEAT. See the
  contextual-keyword rule below for the effect of an immediately following
  opening parenthesis.

## Expressions

```ebnf
expression       = prefixed-primary, { binary-operator, prefixed-primary } ;
prefixed-primary = { "+" | "-" | "NOT" }, primary ;
primary          = integer-literal | single-literal | string-literal
                 | identifier | application | "(", expression, ")" ;
application      = identifier, "(", ( argument-list | { "+" } ), ")" ;
argument-list    = expression, { ",", expression } ;
binary-operator  = "+" | "-" | "*" | "/" | "MOD"
                 | "=" | "<>" | "<" | "<=" | ">" | ">=" | "AND" | "OR" ;
```

A parenthesized expression must contain an operand. The `{ "+" }` in the
application rule reflects the behavior of the actual parser for zero-argument
calls. It skips prefix plus tokens, so both `F()` and `F(+)` call a declared
zero-argument F. An empty argument after a comma and a trailing argument
comma are syntax errors.

The expression production describes token order. The operator stack
determines grouping as follows, from strongest to weakest.

| Binding | Operators |
| --- | --- |
| 7 | Prefix `-` |
| 6 | `*`, `/`, `MOD` |
| 5 | Binary `+`, `-` |
| 4 | `=`, `<>`, `<`, `<=`, `>`, `>=` |
| 3 | Prefix `NOT` |
| 2 | `AND` |
| 1 | `OR` |

Prefix plus is ignored. Other prefixes are pushed when the parser expects an
operand. Before pushing a binary operator, the parser pops operators from the
top of the current group while their precedence is at least that of the
incoming operator. End of expression, comma between arguments or closing
parenthesis flushes the operators in that group. Binary operators of equal
precedence therefore associate left to right, including chained comparisons.
Prefix NOT is legal after a binary operator, even an arithmetic one. For
example, `2 * NOT 0 + 1` evaluates as `2 * (NOT (0 + 1))`. AND and OR skip
the right operand at runtime when the left operand determines the result.

### Names, declarations and arity

- A scalar target has no index. An array target or read requires exactly one
  index. A bare array is not a value or argument. Parameters are scalar.
  The compiler checks symbol kind. It checks many value types only at runtime,
  including the evaluated array index, array size and FOR start, limit and step.
- A plain scalar assignment, a FOR counter or an INPUT target outside a
  routine can introduce a global. The declaration pass makes globals and
  routine declarations visible before their source position. Inside a
  routine, variables must resolve to parameters, locals or globals.
  Locals and parameters shadow globals. DIM in a routine declares a local.
  Repeated explicit declaration in the same scope is an error. A global
  variable cannot share its name with a routine. CONST names cannot be written
  by assignment, INPUT or FOR.
- The default type is INTEGER, except that a name ending in `$` defaults to
  STRING. For variable declarations and parameters a name ending in `$`
  requires STRING. A routine header can override the suffix with an
  explicit return type. A SUB must resolve to INTEGER, so a SUB name ending
  in `$` requires the accepted explicit `AS INTEGER` clause.
- An application in an expression resolves in this order: user routine,
  built-in, array variable. A SUB is allowed only as the outermost
  statement call. Statement calls can also invoke a FUNCTION or built-in,
  and discard the result. If a built-in name conflicts with an array
  name, bare `name(index) = …` is dispatched as a call and rejected. Use
  `LET name(index) = …` to target that array.
- User routines require exactly the declared arity. A built-in shadowed by a
  user routine follows the declared arity of that routine. Otherwise the
  accepted built-in argument counts are listed below. No argument-list
  production permits implicit comma-separated omissions.

| Argument count | Built-ins |
| --- | --- |
| 0 | `RND`, `MILLIS` |
| 1 | `ABS`, `SGN`, `INT`, `CINT`, `CSNG`, `SEED`, `LEN`, `CHR$`, `ASC`, `STR$`, `VAL`, `UCASE$`, `LCASE$`, `EEREAD`, `DIGITALREAD`, `ANALOGREAD`, `DELAY` |
| 2 | `MIN`, `MAX`, `LEFT$`, `RIGHT$`, `EEWRITE`, `PINMODE`, `DIGITALWRITE` |
| 2 or 3 | `MID$` |

## Tokens

```ebnf
newline          = ? byte 0x0A ? ;
eof              = ? end of source ? ;
white-space      = ? space, horizontal tab or carriage return ? ;
comment          = ( "'" | "REM" ), { comment-byte } ;
comment-byte     = ? any byte except 0x0A ? ;

identifier       = name-start, { name-start | digit }, [ "$" ] ;
name-start       = ? ASCII letter A through Z, a through z, or underscore ? ;
digit            = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
digits           = digit, { digit } ;

integer-literal  = digits ;
single-literal   = ( digits, ".", { digit } | ".", digits ), [ exponent ]
                 | digits, exponent ;
exponent         = ( "E" | "e" ), [ "+" | "-" ], digits ;

string-literal   = quote, { string-byte | quote, quote }, quote ;
quote            = ? byte 0x22 ? ;
string-byte      = ? any byte 0x01 through 0xFF except LF, CR or 0x22 ? ;
```

Names and keywords are ASCII case insensitive. The lexer consumes the whole
name and optional suffix before recognizing a keyword. `REM` is a comment only
as a complete name token, so `REMARK` and `REM$` are identifiers. Apostrophe
and REM can start an inline comment after a statement. A comment ends at LF or
end of source. CR is skipped outside strings, including in CRLF, and does not
act as a newline by itself. Outside strings and comments a source byte of
zero or above 0x7F is rejected. The comparison pairs `<>`, `<=` and `>=`
are single adjacent-byte tokens.

These complete name tokens are reserved and cannot be used as identifiers:

```text
LET PRINT END IF THEN ELSEIF ELSE WHILE WEND FUNCTION RETURN LOCAL MOD REM
```

These are contextual tokens. The lexer recognizes them as keywords and the
parser also accepts them in identifier slots:

```text
INPUT STOP CONST DIM AS INTEGER SINGLE STRING AND OR NOT SELECT CASE FOR TO
STEP NEXT DO LOOP UNTIL REPEAT EXIT SUB CALL
```

At the start of a statement the contextual token is dispatched as an
identifier if the next non-space, non-tab byte is `=` or `(`. This
allows `NEXT=3` and the statement call `NEXT()` if a routine named NEXT
exists. It also means `CASE (1)` and the REPEAT closer `UNTIL (X)` do not
parse as those clauses. Write `CASE 1` or `UNTIL X`. The lookahead does not
skip CR. Within an expression, AND and OR are binary operators in operator
position. In operand position `NOT` acts as an operator only if no routine
or visible variable named NOT exists.

INTEGER is decimal, including leading zeroes. Outside an exponent, a sign
is an expression prefix. Positive integer literals are limited to
2,147,483,647. The magnitude 2,147,483,648 is accepted only with a pending
unary minus at the top of the current expression stack. `-2147483648`
compiles and `-(2147483648)` does not. A decimal point or exponent selects
SINGLE. The compiler rejects malformed exponents and values that fail its
finite binary32 range checks. String quotes are escaped by doubling them.
Byte zero can be constructed at runtime using `CHR$(0)`.

Default maximums from [sbasic.h](../include/sbasic.h) include 31 bytes per
identifier including `$`, 63 bytes per number token, 255 decoded bytes per
string, 65,535 as the last possible source line number, 64 expression-stack
entries and 32 open control blocks. The limits for symbol slots and shared
program storage also apply. The compiler rejects a newline on the maximum
line because it would advance to a line that cannot be represented.
