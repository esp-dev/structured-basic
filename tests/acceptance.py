"""Black-box acceptance, negative, resource and integer oracle tests."""
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

EXE = Path(sys.argv[1]).resolve()
RESULTS = []


def case(name, source, expected='', code=0, line=None, budget=None):
    with tempfile.TemporaryDirectory(prefix='sbasic-', dir=EXE.parent) as folder:
        path = Path(folder) / 'test.bas'
        path.write_bytes(source.encode('ascii'))
        command = [str(EXE), str(path)] + ([] if budget is None else [str(budget)])
        run = subprocess.run(command, capture_output=True, text=True, timeout=8)
    good = (run.returncode == code and run.stdout == expected and
            ((not run.stderr) if code == 0 else f'E{code} line ' in run.stderr) and
            (line is None or f'line {line}:' in run.stderr))
    RESULTS.append({'name': name, 'pass': good, 'returncode': run.returncode,
                    'stdout': run.stdout, 'stderr': run.stderr})
    if not good:
        print(f'FAIL {name}: expected code={code} output={expected!r}; got {run.returncode} {run.stdout!r} {run.stderr!r}')


case('precedence', 'PRINT 2+3*4\nPRINT (2+3)*4\nPRINT 20/3*2\nPRINT 20 MOD 3+1\n', '14\n20\n12\n3\n')
case('division_truncates', 'PRINT -7/3\nPRINT 7/-3\nPRINT -7 MOD 3\nPRINT 7 MOD -3\n', '-2\n-2\n-1\n1\n')
case('unary', 'PRINT --2\nPRINT -(-3)\nPRINT +++4\nPRINT 2*-3\n', '2\n3\n4\n-6\n')
case('comparisons', 'PRINT 2=2\nPRINT 2<>2\nPRINT 1<2\nPRINT 1<=1\nPRINT 2>1\nPRINT 2>=3\n', '1\n0\n1\n1\n1\n0\n')
case('integer_edges', 'PRINT -2147483648\nPRINT 2147483647\nPRINT -2147483648/1\nPRINT -2147483648*1\nPRINT 0*-2147483648\n', '-2147483648\n2147483647\n-2147483648\n-2147483648\n0\n')
longname = 'abcdefghijklmnopqrstuvwxyz12345'
case('names_31_case_boundaries', f'let {longname}=19\nLET IFVALUE=2\nlet PRINTABLE=3\nLET MODULUS=4\nPRINT {longname.upper()}+ifvalue+printable+modulus\n', '28\n')
case('prefix_distinction', f'{longname}=1\n{longname[:-1]}6=2\nPRINT {longname}\n', '1\n')
case('comments_crlf_no_final_newline', "REM ignored\r\nLET X=9 'comment\r\n' full line\r\nPRINT X", '9\n')
case('empty', '')
case('end', 'PRINT 1\nEND\nPRINT 2\n', '1\n')
case('if_nested', 'IF 1 THEN\nIF 0 THEN\nPRINT 0\nELSEIF 3<2 THEN\nPRINT 9\nELSEIF 2=2 THEN\nPRINT 7\nELSE\nPRINT 8\nEND IF\nELSE\nPRINT 6\nEND IF\n', '7\n')
for v in range(4):
    case(f'elseif_branch_{v}', f'X={v}\nIF X=0 THEN\nPRINT 10\nELSEIF X=1 THEN\nPRINT 11\nELSEIF X=2 THEN\nPRINT 12\nELSE\nPRINT 13\nEND IF\n', f'{10+v}\n')
case('if_false_no_else', 'IF 0 THEN\nPRINT 1\nEND IF\nPRINT 2\n', '2\n')
case('loop_nested', 'S=0\nI=0\nWHILE I<3\nJ=0\nWHILE J<4\nS=S+I+J\nJ=J+1\nWEND\nI=I+1\nWEND\nPRINT S\nWHILE 0\nPRINT 99\nWEND\n', '30\n')
case('three_args_forward', 'PRINT SUM(2,3,4)\nFUNCTION SUM(A,B,C)\nRETURN A+B*C\nEND FUNCTION\n', '14\n')
case('args_evaluated_left_to_right', 'G=0\nPRINT SUM(NEXT(),NEXT(),NEXT())\nFUNCTION NEXT()\nG=G+1\nRETURN G\nEND FUNCTION\nFUNCTION SUM(A,B,C)\nRETURN A*100+B*10+C\nEND FUNCTION\n', '123\n')
case('nested_calls', 'FUNCTION ADD(A,B)\nRETURN A+B\nEND FUNCTION\nPRINT ADD(ADD(1,2),ADD(3,ADD(4,5)))\n', '15\n')
case('locals_shadow_globals', 'X=99\nG=7\nFUNCTION F(X,Y,Z)\nLOCAL G,T\nT=X+Y+Z+G\nG=20\nRETURN T\nEND FUNCTION\nPRINT F(1,2,3)\nPRINT X\nPRINT G\n', '6\n99\n7\n')
case('local_function_scope', 'G=10\nFUNCTION F()\nT=T+1\nLOCAL T\nRETURN T\nEND FUNCTION\nPRINT F()\nPRINT F()\n', '1\n1\n')
case('global_write_and_late_declaration', 'FUNCTION F()\nX=X+3\nRETURN X\nEND FUNCTION\nPRINT F()\nX=10\nPRINT F()\nPRINT X\n', '3\n13\n13\n')
case('gcd', 'FUNCTION GCD(A,B)\nLOCAL T\nWHILE B<>0\nT=A MOD B\nA=B\nB=T\nWEND\nRETURN A\nEND FUNCTION\nPRINT GCD(1071,462)\nPRINT GCD(54,24)\nPRINT GCD(0,9)\n', '21\n6\n9\n')
case('factorial', 'FUNCTION FACT(N)\nIF N<=1 THEN\nRETURN 1\nEND IF\nRETURN N*FACT(N-1)\nEND FUNCTION\nPRINT FACT(12)\n', '479001600\n')
case('mutual_recursion', 'PRINT EVEN(12)\nFUNCTION EVEN(N)\nIF N=0 THEN\nRETURN 1\nEND IF\nRETURN ODD(N-1)\nEND FUNCTION\nFUNCTION ODD(N)\nIF N=0 THEN\nRETURN 0\nEND IF\nRETURN EVEN(N-1)\nEND FUNCTION\n', '1\n')
case('repeat_calls_release_frames', 'FUNCTION F(A)\nLOCAL X\nX=X+A\nRETURN X\nEND FUNCTION\nI=0\nS=0\nWHILE I<1000\nS=S+F(2)\nI=I+1\nWEND\nPRINT S\n', '2000\n')
case('implicit_return_zero', 'FUNCTION F()\nLOCAL X\nX=42\nEND FUNCTION\nPRINT F()\n', '0\n')
case('return_exits_nested_blocks', 'FUNCTION F()\nWHILE 1\nIF 1 THEN\nRETURN 42\nEND IF\nWEND\nEND FUNCTION\nPRINT F()\n', '42\n')

negative = {
    'missing_rhs': ('LET X=\n', 1), 'numbered_line': ('10 PRINT 1\n', 1),
    'long_name': ('A'*32+'=1\n', 2), 'huge_literal': ('PRINT 99999999999999999999999\n', 3),
    'positive_out_of_range': ('PRINT 2147483648\n', 3),
    'unknown_read': ('PRINT X\n', 4), 'unknown_function': ('PRINT F(1)\n', 4),
    'undeclared_function_write': ('FUNCTION F()\nX=1\nRETURN X\nEND FUNCTION\n', 4),
    'local_not_global': ('FUNCTION F()\nLOCAL X\nEND FUNCTION\nPRINT X\n', 4),
    'duplicate_function': ('FUNCTION F()\nEND FUNCTION\nFUNCTION F()\nEND FUNCTION\n', 5),
    'duplicate_parameter': ('FUNCTION F(X,X)\nEND FUNCTION\n', 5),
    'duplicate_local': ('FUNCTION F(X)\nLOCAL X\nEND FUNCTION\n', 5),
    'duplicate_local_list': ('FUNCTION F()\nLOCAL X,X\nEND FUNCTION\n', 5),
    'global_function_collision': ('X=1\nFUNCTION X()\nEND FUNCTION\n', 5),
    'missing_argument': ('FUNCTION F(A,B,C)\nRETURN A\nEND FUNCTION\nPRINT F(1,2)\n', 6),
    'extra_argument': ('FUNCTION F()\nEND FUNCTION\nPRINT F(1)\n', 6),
    'empty_arguments': ('FUNCTION F(A)\nEND FUNCTION\nPRINT F()\n', 6),
    'divide_zero': ('PRINT 10/0\n', 10), 'mod_zero': ('PRINT 10 MOD 0\n', 10),
    'add_overflow': ('PRINT 2147483647+1\n', 11),
    'add_underflow': ('PRINT -2147483648+-1\n', 11),
    'sub_overflow': ('PRINT 2147483647--1\n', 11),
    'sub_underflow': ('PRINT -2147483648-1\n', 11),
    'mul_overflow': ('PRINT 1073741824*2\n', 11),
    'mul_underflow': ('PRINT -1073741825*2\n', 11),
    'mul_negative_negative': ('PRINT -2147483648*-1\n', 11),
    'divide_overflow': ('PRINT -2147483648/-1\n', 11),
    'mod_overflow': ('PRINT -2147483648 MOD -1\n', 11),
    'negate_overflow': ('PRINT -(-2147483648)\n', 11),
    'unmatched_if': ('IF 1 THEN\nPRINT 1\n', 1),
    'unmatched_while': ('WHILE 1\n', 1), 'unmatched_wend': ('WEND\n', 1),
    'else_without_if': ('ELSE\n', 1), 'endif_without_if': ('END IF\n', 1),
    'elseif_after_else': ('IF 1 THEN\nELSE\nELSEIF 1 THEN\nEND IF\n', 1),
    'duplicate_else': ('IF 1 THEN\nELSE\nELSE\nEND IF\n', 1),
    'crossed_blocks': ('IF 1 THEN\nWHILE 1\nEND IF\nWEND\n', 1),
    'single_line_if_rejected': ('IF 1 THEN PRINT 1\nEND IF\n', 1),
    'missing_then': ('IF 1\nEND IF\n', 1), 'empty_condition': ('IF THEN\nEND IF\n', 1),
    'missing_paren': ('PRINT (1+2\n', 1), 'extra_paren': ('PRINT 1)\n', 1),
    'empty_paren': ('PRINT ()\n', 1), 'missing_operator': ('PRINT 1 2\n', 1),
    'trailing_comma': ('FUNCTION F(A)\nEND FUNCTION\nPRINT F(1,)\n', 1),
    'leading_comma': ('FUNCTION F(A)\nEND FUNCTION\nPRINT F(,1)\n', 1),
    'operator_before_close': ('FUNCTION F(A)\nEND FUNCTION\nPRINT F(1+)\n', 1),
    'comma_in_group': ('PRINT (1,2)\n', 1),
    'return_main': ('RETURN 1\n', 1), 'local_main': ('LOCAL X\n', 1),
    'unclosed_function': ('FUNCTION F()\nRETURN 1\n', 1),
    'endfunction_main': ('END FUNCTION\n', 1),
    'function_in_block': ('IF 1 THEN\nFUNCTION F()\nEND FUNCTION\nEND IF\n', 1),
    'nested_function': ('FUNCTION F()\nFUNCTION G()\nEND FUNCTION\nEND FUNCTION\n', 1),
    'function_block_unclosed': ('FUNCTION F()\nIF 1 THEN\nEND FUNCTION\n', 1),
    'compile_before_any_output': ('PRINT 99\nPRINT UNKNOWN\n', 4),
    'bad_character': ('PRINT @\n', 1), 'nul_byte': ('PRINT 1\x00\n', 1),
}
for name, (source, code) in negative.items():
    case(name, source, code=code)

case('program_capacity', 'PRINT 1\n'*1000, code=7)
case('symbol_capacity', ''.join(f'V{i}=0\n' for i in range(256)), code=8)
case('expression_capacity', 'PRINT '+'('*65+'1'+')'*65+'\n', code=8)
case('block_capacity', 'IF 1 THEN\n'*33+'END IF\n'*33, code=8)
case('call_stack_capacity', 'FUNCTION F()\nRETURN F()\nEND FUNCTION\nPRINT F()\n', code=9)
params = ','.join(f'A{i}' for i in range(129))
case('operand_stack_capacity', f'FUNCTION F({params})\nRETURN 0\nEND FUNCTION\nPRINT F('+','.join(['1']*129)+')\n', code=9)
locals_ = ','.join(f'L{i}' for i in range(100))
case('runtime_pool_capacity', f'FUNCTION F()\nLOCAL {locals_}\nRETURN F()\nEND FUNCTION\nPRINT F()\n', code=7)
# Program fits by itself but its zero-initialized globals cannot fit alongside it.
case('globals_pool_capacity', ''.join(f'G{i}=0\n' for i in range(100))+'PRINT 0\n'*610, code=7)
case('step_budget', 'WHILE 1\nWEND\n', code=13, budget=100)
case('line_diagnostic', '\n\nPRINT 1/0\n', code=10, line=3)
case('elseif_condition_line', 'IF 0 THEN\nPRINT 1\nELSEIF 1/0 THEN\nPRINT 2\nEND IF\n', code=10, line=3)
case('last_line_function', 'PRINT F()\nFUNCTION F()\nRETURN 3\nEND FUNCTION', '3\n')
case('max_block_depth', 'IF 1 THEN\n'*32+'PRINT 7\n'+'END IF\n'*32, '7\n')
case('max_expression_depth', 'PRINT '+'('*64+'1'+')'*64+'\n', '1\n')
params = ','.join(f'A{i}' for i in range(128))
case('max_operand_depth', f'FUNCTION F({params})\nRETURN A0+A127\nEND FUNCTION\nPRINT F('+','.join(['1']*128)+')\n', '2\n')

# Many valid expression trees, with independent evaluation. Bounded leaf values
# avoid overflow so these isolate parser precedence/parenthesis correctness.
rng_expr = random.Random(12345)
def tree(depth):
    if not depth or rng_expr.randrange(4) == 0:
        value = rng_expr.randint(-8, 8)
        return str(value), value
    left, a = tree(depth-1)
    right, b = tree(depth-1)
    op = rng_expr.choice(['+', '-', '*', '<', '>='])
    value = {'+': lambda: a+b, '-': lambda: a-b, '*': lambda: a*b,
             '<': lambda: int(a < b), '>=': lambda: int(a >= b)}[op]()
    return f'({left} {op} {right})', value

for index in range(80):
    expression, value = tree(4)
    case(f'expression_tree_{index}', f'PRINT {expression}\n', f'{value}\n')

# Independent Python integer oracle, including the exact overflow boundary.
rng = random.Random(271828)
edges = [-2147483648, -2147483647, -46341, -2, -1, 0, 1, 2, 46341, 2147483646, 2147483647]
for index in range(160):
    a = rng.choice(edges) if index % 2 else rng.randint(-2147483648, 2147483647)
    b = rng.choice(edges)
    op = rng.choice(['+', '-', '*', '/', 'MOD', '=', '<>', '<', '<=', '>', '>='])
    if op in ('/', 'MOD'):
        if b == 0:
            code, result = 10, None
        elif a == -2147483648 and b == -1:
            code, result = 11, None
        else:
            quotient = (abs(a)//abs(b)) * (-1 if (a < 0) != (b < 0) else 1)
            code, result = 0, quotient if op == '/' else a-quotient*b
    else:
        result = {'+': lambda: a+b, '-': lambda: a-b, '*': lambda: a*b,
                  '=': lambda: int(a == b), '<>': lambda: int(a != b),
                  '<': lambda: int(a < b), '<=': lambda: int(a <= b),
                  '>': lambda: int(a > b), '>=': lambda: int(a >= b)}[op]()
        code = 0 if -2147483648 <= result <= 2147483647 else 11
    case(f'integer_oracle_{index}', f'PRINT ({a}) {op} ({b})\n',
         '' if code else f'{result}\n', code=code)

passed = sum(row['pass'] for row in RESULTS)
print(f'Acceptance: {passed}/{len(RESULTS)} PASS')
report = os.environ.get('SB_TEST_REPORT')
if report:
    Path(report).write_text(json.dumps({'passed': passed, 'total': len(RESULTS), 'cases': RESULTS}, indent=2)+'\n')
sys.exit(passed != len(RESULTS))
