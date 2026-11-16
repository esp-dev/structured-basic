"""Regression probes for the source forms documented in docs/GRAMMAR.md."""

from pathlib import Path
import subprocess
import sys
import tempfile

exe = Path(sys.argv[1]).resolve()
cases = {
    "empty": (b"", 0),
    "blank and comments": (b"\r\nREM comment\x00\xff\r\n'comment\x00\xff", 0),
    "cr skipped": (b"PRINT\r 1", 0),
    "cr not newline": (b"PRINT 1\rPRINT 2", 1),
    "ascii utf8 string": (b'PRINT "x\xc3\xb3"', 0),
    "binary string literal": (b'PRINT "\xff\t"', 0),
    "nul token": (b"PRINT \x00", 1),
    "nul in a string": (b'PRINT "\x00"', 1),
    "raw carriage return in string": (b'PRINT "a\rb"', 1),
    "utf8 bom": (b'\xef\xbb\xbfPRINT "hi"', 1),
    "doubled quotes": ('PRINT "a""b"', 0),
    "whole word rem": ('REMARK = 1\nLET REM$ = "x"\nPRINT REMARK;REM$ REM trailing comment', 0),
    "name limit with suffix": ('A' * 30 + '$ = ""', 0),
    "name too long": ('A' * 31 + '$ = ""', 2),
    "reserved old keyword": ("IF = 1", 1),
    "contextual variable": ("NEXT=3\nPRINT NEXT", 0),
    "contextual call": ("NEXT()\nSUB NEXT()\nEND SUB", 0),
    "statement lookahead skips tabs": ("NEXT\t= 3", 0),
    "statement lookahead keeps cr": (b"NEXT\r= 3", 1),
    "not shadowed by variable": ("NOT=8\nPRINT NOT", 0),
    "not shadowed by routine": ("FUNCTION NOT()\nRETURN 5\nEND FUNCTION\nPRINT NOT()", 0),
    "nested block bodies": ("IF 1 THEN\nWHILE 0\nWEND\nELSEIF 0 THEN\nELSE\nEND IF", 0),
    "if compact": ("IF 1 THEN PRINT 9", 1),
    "else if spaced": ("IF 0 THEN\nELSE IF 1 THEN\nEND IF", 1),
    "select list": ("SELECT CASE 2\nCASE 1,2\nPRINT 1\nCASE ELSE\nEND SELECT", 0),
    "select else only": ("SELECT CASE 1\nCASE ELSE\nEND SELECT", 0),
    "select empty": ("SELECT CASE 1\nEND SELECT", 1),
    "select first statement": ("SELECT CASE 1\nPRINT 1\nCASE ELSE\nEND SELECT", 1),
    "case parentheses reclassified": ("SELECT CASE 1\nCASE (1)\nEND SELECT", 1),
    "case parentheses in later item": ("SELECT CASE (1)\nCASE 1,(2)\nEND SELECT", 0),
    "for negative step": ("FOR I=3 TO 1 STEP -1\nNEXT I", 0),
    "for counter mismatch": ("I=0\nJ=0\nFOR I=1 TO 2\nNEXT J", 1),
    "next optional name": ("FOR I=3 TO 1\nNEXT", 0),
    "pretested do": ("DO WHILE (1)\nEXIT DO\nLOOP", 0),
    "posttested do": ("DO\nLOOP UNTIL (1)", 0),
    "duplicate do conditions": ("DO WHILE 1\nLOOP UNTIL 0", 1),
    "repeat exit do": ("REPEAT\nEXIT DO\nUNTIL 0", 0),
    "repeat parenthesized condition": ("REPEAT\nUNTIL (1)", 4),
    "repeat exit repeat": ("REPEAT\nEXIT REPEAT\nUNTIL 1", 1),
    "while exit while": ("WHILE 1\nEXIT WHILE\nWEND", 0),
    "exit without loop": ("EXIT FOR", 1),
    "local list and array": ("SUB S(A AS SINGLE)\nLOCAL N, T$(2)\nT$(1) = STR$(A)\nRETURN\nEND SUB\nCALL S(2)", 0),
    "nested const": ("IF 1 THEN\nCONST X=2\nEND IF", 1),
    "const array": ("CONST X(2)=2", 16),
    "assignment to const": ("CONST X=1\nX=2", 16),
    "dim list": ("DIM A(5), T AS SINGLE\nA(2)=4", 0),
    "array as value": ("DIM A(2)\nPRINT A", 16),
    "array parameter": ("SUB S(A(2))\nEND SUB", 16),
    "built-in array write with let": ("DIM ABS(1)\nLET ABS(0)=3", 0),
    "built-in array write without let": ("DIM ABS(1)\nABS(0)=3", 1),
    "declared local at top level": ("LOCAL A", 1),
    "sub integer annotation": ("SUB S$() AS INTEGER\nRETURN\nEND SUB\nCALL S$()", 0),
    "sub string suffix": ("SUB S$()\nEND SUB", 16),
    "sub wrong type annotation": ("SUB S() AS STRING\nEND SUB", 16),
    "function overridden suffix": ("FUNCTION F$() AS INTEGER\nRETURN 9\nEND FUNCTION", 0),
    "function bare return": ("FUNCTION F()\nRETURN\nEND FUNCTION", 1),
    "sub return value": ("SUB S()\nRETURN 1\nEND SUB", 1),
    "sub as operand": ("SUB S()\nEND SUB\nPRINT S()", 16),
    "built-in statement call": ("CALL ABS(1)\nABS(1)", 0),
    "call needs parentheses": ("CALL ABS", 1),
    "call tail": ("CALL ABS(1)+2", 1),
    "zero arg plus": ("PRINT RND(+ +)", 0),
    "zero arg minus": ("PRINT RND(-)", 1),
    "zero arg plus before comma": ("PRINT RND(+,)", 1),
    "trailing argument separator": ("PRINT ABS(1,)", 1),
    "built-in arity": ('PRINT MID$("abc",2,1)', 0),
    "wrong built-in arity": ('PRINT MID$("abc")', 6),
    "empty grouping": ('PRINT (+)', 1),
    "literal input prompt": ('INPUT "text"; N, S$', 0),
    "computed input prompt": ('INPUT "a"+"b"; N', 1),
    "no input prompt": ('INPUT X,Y', 0),
    "input array": ('DIM A(2)\nINPUT A', 16),
    "bare print": ('PRINT', 0),
    "print trailing separator": ('PRINT 1,2;', 0),
    "missing print item": ('PRINT 1,,2', 1),
    "single spelling forms": ('PRINT .5, 1., 1e-3, 1.0e+2', 0),
    "invalid single exponent": ('PRINT 1.e+', 3),
    "unsigned min magnitude": ('PRINT 2147483648', 3),
    "signed min literal": ('PRINT -+2147483648', 0),
    "min magnitude parenthesized": ('PRINT -(2147483648)', 3),
    "unary plus on string": ('PRINT +"x"', 0),
    "line number": ('10 PRINT 1', 1),
    "colon separators": ('PRINT 1:PRINT 2', 1),
    "syntax after halt": ('STOP\nPRINT (', 1),
}

with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp) / "grammar.bas"
    for name, (source, expected) in cases.items():
        raw = source.encode("ascii") if isinstance(source, str) else source
        # Compile the whole source, then halt before semantic side effects.
        file.write_bytes(b"END\n" + raw)
        run = subprocess.run([str(exe), str(file)], capture_output=True, text=True, timeout=5)
        assert run.returncode == expected, (name, expected, run.returncode, run.stdout, run.stderr)
    file.write_text("PRINT 2 * NOT 0 + 1\nPRINT 1 < 2 = 1\n", encoding="ascii")
    results = subprocess.check_output([str(exe), str(file)], text=True)
    assert results == "0\n1\n", results
print(f"Grammar: {len(cases)} compile cases and 2 operator results passed")
