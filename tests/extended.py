"""BASIC 1.0 acceptance tests. Expected results are independent of the VM."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import math

exe=Path(sys.argv[1]).resolve()
results=[]
def case(name,source,expected='',code=0,input_='',args=()):
    with tempfile.TemporaryDirectory(prefix='basic10-',dir=exe.parent) as folder:
        path=Path(folder)/'case.bas'
        path.write_bytes(source.encode('utf-8'))
        run=subprocess.run([str(exe),str(path),*map(str,args)],input=input_.encode(),capture_output=True,timeout=12)
    output=run.stdout.decode('utf-8',errors='replace').replace('\r\n','\n')
    errors=run.stderr.decode('utf-8',errors='replace').replace('\r\n','\n')
    output_matches=run.stdout==expected if isinstance(expected,bytes) else output==expected
    good=run.returncode==code and output_matches and (not errors if not code else f'E{code} line ' in errors)
    results.append(dict(name=name,passed=good,exit=run.returncode,stdout=output,stderr=errors))
    if not good: print('FAIL',name,run.returncode,repr(output),repr(errors))

case('single_declarations','DIM X AS SINGLE\nX=1.5\nPRINT X+2\nPRINT CSNG(7)/2\n','3.5\n3.5\n')
case('legacy_context_names','NEXT=2\nSTOP=3\nDIM=4\nNOT=5\nINPUT=6\nFOR=7\nPRINT NEXT+STOP+DIM+NOT+INPUT+FOR\n','27\n')
case('legacy_function_builtin_name','FUNCTION ABS(A,B)\nRETURN A+B\nEND FUNCTION\nPRINT ABS(2,3)\n','5\n')
case('specified_acceptance',(Path(__file__).resolve().parents[1]/'examples/acceptance.bas').read_text(),
     ''.join(f'{i} => {math.factorial(i)}\n' for i in range(1,9))+''.join(f'A({i}) = {i*i}\n' for i in range(10))+'Finished\n')
case('single_promotion','DIM X AS SINGLE\nX=7\nPRINT X\nPRINT 1.5<2\nPRINT ABS(-1.25)\n','7\n1\n1.25\n')
case('single_lexing','PRINT .5+1.\nPRINT 1e2+2.5e-1\n','1.5\n100.25\n')
case('conversions','PRINT CINT(-1.9)\nPRINT INT(-1.1)\nPRINT VAL("2.5")+1\nPRINT STR$(12)+"!"\n','-1\n-2\n3.5\n12!\n')
case('minmaxsgn','PRINT MIN(3,2)\nPRINT MAX(3.5,2)\nPRINT SGN(-4)\nPRINT SGN(0)\nPRINT SGN(3.25)\n','2\n3.5\n-1\n0\n1\n')
case('constant','CONST N=2+3\nPRINT N\n','5\n')
case('constant_string','CONST GREETING$="Hi"\nPRINT GREETING$\n','Hi\n')
case('string_assignment','DIM S AS STRING\nS="a"+"b"\nPRINT S\nS=""\nPRINT LEN(S)\n','ab\n0\n')
case('string_quotes','S$="Say ""hi"""\nPRINT S$\n','Say "hi"\n')
case('byte_zero','S$=CHR$(0)+CHR$(255)\nPRINT LEN(S$)\nPRINT ASC(S$)\nPRINT ASC(RIGHT$(S$,1))\n','2\n0\n255\n')
case('output_bytes','PRINT CHR$(0);CHR$(255);CHR$(13);\nPRINT\n',b'\x00\xff\r\n')
case('string_compare','PRINT "a"<"b"\nPRINT "ab">"a"\nPRINT ""=""\nPRINT "x"<>"X"\n','1\n1\n1\n1\n')
case('string_functions','S$="abcDEF"\nPRINT LEFT$(S$,2)\nPRINT RIGHT$(S$,2)\nPRINT MID$(S$,2,3)\nPRINT MID$(S$,4)\nPRINT UCASE$(S$)\nPRINT LCASE$(S$)\n','ab\nEF\nbcD\nDEF\nABCDEF\nabcdef\n')
case('empty_substrings','PRINT LEN(LEFT$("abc",0))\nPRINT LEN(RIGHT$("",5))\nPRINT LEN(MID$("abc",4))\nPRINT LEFT$("a",10)\n','0\n0\n0\na\n')
case('print_layout','PRINT "A";1;":";2\nPRINT "B";\nPRINT "C",3\nPRINT\n','A1:2\nBC 3\n\n')
case('logical_short_circuit','G=0\nFUNCTION HIT()\nG=G+1\nRETURN 1\nEND FUNCTION\nPRINT 0 AND HIT()\nPRINT 1 OR HIT()\nPRINT G\nPRINT 1 AND HIT()\nPRINT 0 OR HIT()\nPRINT G\n','0\n1\n0\n1\n1\n2\n')
case('logical_precedence','PRINT NOT 1=2 AND 3<4 OR 0\nPRINT NOT NOT 5\nPRINT 1 OR (1/0)\nPRINT 0 AND (1/0)\n','1\n1\n1\n0\n')
case('sub_calls','G=0\nSUB ADD(X,Y)\nLOCAL T\nT=X+Y\nG=G+T\nRETURN\nEND SUB\nCALL ADD(2,3)\nADD(4,5)\nPRINT G\n','14\n')
case('string_by_value','S$="global"\nSUB CHANGE(S$)\nS$="local"\nPRINT S$\nEND SUB\nCHANGE(S$)\nPRINT S$\n','local\nglobal\n')
case('typed_return','FUNCTION TWICE(X AS SINGLE) AS SINGLE\nRETURN X*2\nEND FUNCTION\nPRINT TWICE(1.5)\n','3\n')
case('recursive_strings','FUNCTION F$(N)\nLOCAL S$\nS$="x"\nIF N=0 THEN\nRETURN ""\nEND IF\nRETURN S$+F$(N-1)\nEND FUNCTION\nPRINT F$(8)\n','xxxxxxxx\n')
case('integer_array','DIM A(10)\nFOR I=0 TO 9\nA(I)=I*I\nNEXT I\nPRINT A(0)\nPRINT A(9)\n','0\n81\n')
case('single_array','DIM A(2) AS SINGLE\nA(0)=1.25\nA(1)=2\nPRINT A(0)+A(1)\n','3.25\n')
case('string_array','DIM S$(2)\nS$(0)="one"\nS$(1)=S$(0)+"two"\nS$(0)="changed"\nPRINT S$(1)\n','onetwo\n')
case('local_array_shadow','DIM A(2)\nA(0)=99\nFUNCTION F()\nDIM A(2)\nA(0)=7\nRETURN A(0)\nEND FUNCTION\nPRINT F()\nPRINT A(0)\n','7\n99\n')
case('recursive_local_arrays','FUNCTION F(N)\nLOCAL A$(2)\nA$(0)="x"\nIF N=0 THEN\nRETURN LEN(A$(0))\nEND IF\nRETURN LEN(A$(0))+F(N-1)\nEND FUNCTION\nPRINT F(8)\n','9\n')
case('for_negative','FOR I=5 TO 1 STEP -2\nPRINT I\nNEXT\n','5\n3\n1\n')
case('for_empty','FOR I=1 TO 0\nPRINT 9\nNEXT\nPRINT I\n','1\n')
case('for_end_evaluated_once','G=3\nFOR I=1 TO G\nG=0\nPRINT I\nNEXT\n','1\n2\n3\n')
case('for_edge_no_overflow','FOR I=2147483647 TO 2147483647\nPRINT I\nNEXT\nFOR J=-2147483648 TO -2147483648 STEP -1\nPRINT J\nNEXT\n','2147483647\n-2147483648\n')
for name,opening,closing in [('do_while','DO WHILE I<3','LOOP'),('do_until','DO UNTIL I=3','LOOP'),('loop_while','DO','LOOP WHILE I<3'),('loop_until','DO','LOOP UNTIL I=3'),('repeat','REPEAT','UNTIL I=3')]:
    case(name,f'I=0\n{opening}\nI=I+1\n{closing}\nPRINT I\n','3\n')
case('infinite_do_exit','DO\nPRINT 1\nEXIT DO\nLOOP\n','1\n')
case('exit_for','FOR I=1 TO 9\nIF I=3 THEN\nEXIT FOR\nEND IF\nPRINT I\nNEXT\nPRINT 7\n','1\n2\n7\n')
case('exit_while_nested_for','WHILE 1\nFOR I=1 TO 2\nSELECT CASE I\nCASE 1\nEXIT WHILE\nEND SELECT\nNEXT\nWEND\nPRINT 7\n','7\n')
case('exit_do_nested_select','DO\nSELECT CASE "x"\nCASE "x"\nEXIT DO\nCASE ELSE\nPRINT 1\nEND SELECT\nLOOP\nPRINT 8\n','8\n')
case('select_nested','SELECT CASE 3\nCASE 1,2\nPRINT 1\nCASE 3\nIF 1 THEN\nSELECT CASE "b"\nCASE "a"\nPRINT 2\nCASE ELSE\nPRINT 3\nEND SELECT\nEND IF\nCASE ELSE\nPRINT 4\nEND SELECT\n','3\n')
case('select_no_match','SELECT CASE 3\nCASE 1\nPRINT 1\nEND SELECT\nPRINT 2\n','2\n')
case('select_return_cleanup','FUNCTION F()\nFOR I=1 TO 2\nSELECT CASE "x"\nCASE "x"\nRETURN 9\nEND SELECT\nNEXT\nEND FUNCTION\nLOCALERROR=0\n',code=4)
case('select_return_cleanup_valid','FUNCTION F()\nLOCAL I\nFOR I=1 TO 2\nSELECT CASE "x"\nCASE "x"\nRETURN 9\nEND SELECT\nNEXT\nEND FUNCTION\nPRINT F()\nPRINT F()\n','9\n9\n')
case('stop','PRINT "before"\nSTOP\nPRINT "after"\n','before\n')
case('input_typed','DIM F AS SINGLE\nINPUT I,S$,F\nPRINT I;":";S$;":";F\n','-7:hello:1.25\n',input_='-7\nhello\n1.25\n')
case('input_prompt','INPUT "Name: ";S$\nPRINT S$\n','Name: Bob\n',input_='Bob\n')
case('gpio_mock','PINMODE(0,1)\nDIGITALWRITE(0,1)\nPRINT DIGITALREAD(0)\nPRINT ANALOGREAD(7)\nDELAY(25)\nPRINT MILLIS()\n','1\n889\n25\n')
case('eeprom_mock','PRINT EEREAD(4095)\nEEWRITE(4095,7)\nPRINT EEREAD(4095)\n','255\n7\n')
case('rng_repeatable','SEED(123)\nDIM A AS SINGLE\nA=RND()\nSEED(123)\nPRINT A=RND()\nPRINT RND()>=0 AND RND()<1\n','1\n1\n')
case('fibonacci','FUNCTION F(N)\nIF N<2 THEN\nRETURN N\nEND IF\nRETURN F(N-1)+F(N-2)\nEND FUNCTION\nPRINT F(10)\n','55\n')
case('string_churn','S$=""\nFOR I=1 TO 255\nS$=S$+"x"\nNEXT\nPRINT LEN(S$)\n','255\n')
case('string_max_literal','PRINT LEN("'+'x'*255+'")\n','255\n')
case('repeated_string_frames','FUNCTION F$(S$)\nLOCAL A$(3)\nA$(0)=S$\nRETURN A$(0)+"!"\nEND FUNCTION\nFOR I=1 TO 1000\nS$=F$("a")\nNEXT\nPRINT S$\n','a!\n')

negative={
 'implicit_float_to_int':('X=1.25\n',16), 'bad_string_conversion':('PRINT VAL("12x")\n',18),
 'val_empty':('PRINT VAL("")\n',18), 'val_zero_byte':('PRINT VAL("1"+CHR$(0))\n',18),
 'cint_range':('PRINT CINT(2147483648.0)\n',18), 'single_literal_range':('PRINT 1e100\n',3),
 'single_overflow':('PRINT 3.4e38*2.0\n',11), 'single_div_zero':('PRINT 1.0/0.0\n',10),
 'single_mod':('PRINT 1.5 MOD 2\n',16), 'string_numeric_mix':('PRINT "a"+1\n',16),
 'string_subtract':('PRINT "a"-"b"\n',16), 'logical_string':('PRINT NOT ""\n',16),
 'constant_write':('CONST N=3\nN=4\n',16), 'constant_input':('CONST N=3\nINPUT N\n',16),
 'abs_min':('PRINT ABS(-2147483648)\n',11), 'asc_empty':('PRINT ASC("")\n',19),
 'chr_range':('PRINT CHR$(256)\n',19), 'left_negative':('PRINT LEFT$("a",-1)\n',19),
 'mid_bad_start':('PRINT MID$("a",0)\n',19), 'mid_past_end':('PRINT MID$("a",3)\n',19),
 'builtin_arity':('PRINT LEFT$("a")\n',6), 'array_negative':('DIM A(1)\nPRINT A(-1)\n',17),
 'array_past_end':('DIM A(1)\nPRINT A(1)\n',17), 'array_empty':('DIM A(0)\nPRINT A(0)\n',17),
 'array_float_index':('DIM A(2)\nPRINT A(1.0)\n',16), 'array_negative_size':('DIM A(-1)\n',19),
 'array_pool':('DIM A(2147483647)\n',7), 'array_as_argument':('DIM A(2)\nFUNCTION F(X)\nRETURN X\nEND FUNCTION\nPRINT F(A)\n',16),
 'array_parameter':('SUB F(A())\nEND SUB\n',16), 'sub_in_expression':('SUB S()\nEND SUB\nPRINT S()\n',16),
 'call_expression_tail':('SUB S()\nEND SUB\nCALL S()+1\n',1),
 'string_return_type':('FUNCTION F$()\nRETURN 1\nEND FUNCTION\nPRINT F$()\n',16),
 'string_limit':('S$="'+'x'*255+'"\nS$=S$+"x"\n',7),
 'for_zero_step':('FOR I=1 TO 3 STEP 0\nNEXT\n',19), 'next_mismatch':('FOR I=1 TO 3\nNEXT J\nJ=0\n',1),
 'exit_no_loop':('EXIT FOR\n',1), 'do_both_conditions':('DO WHILE 1\nLOOP UNTIL 1\n',1),
 'case_without_select':('CASE 1\n',1), 'case_after_else':('SELECT CASE 1\nCASE ELSE\nCASE 1\nEND SELECT\n',1),
 'select_no_case':('SELECT CASE 1\nPRINT 1\nEND SELECT\n',1),
 'eeprom_bad_address':('PRINT EEREAD(4096)\n',21), 'gpio_uart_reserved':('PINMODE(24,1)\n',21),
 'gpio_not_output':('DIGITALWRITE(0,1)\n',21), 'adc_bad_channel':('PRINT ANALOGREAD(8)\n',21),
 'delay_bad_range':('DELAY(-1)\n',19),
}
for name,(source,code) in negative.items(): case(name,source,code=code)

def terminal_case(name,script,expected,diagnostic=''):
    run=subprocess.run([str(exe)],input=script.encode(),capture_output=True,timeout=12)
    good=run.returncode==0 and run.stdout.decode().replace('\r\n','\n')==expected and diagnostic in run.stderr.decode()
    if not diagnostic: good=good and not run.stderr
    results.append(dict(name=name,passed=good,exit=run.returncode,stdout=run.stdout.decode(),stderr=run.stderr.decode()))
    if not good: print('FAIL',name,run.returncode,repr(run.stdout),repr(run.stderr))
terminal_case('terminal_new_run','PRINT   7\nRUN\nRUN\nNEW\nPRINT 9\nRUN\nQUIT\n','7\n7\n9\n')
terminal_case('terminal_list','PRINT 7\nLIST\nQUIT\n','PRINT 7\n')
terminal_case('terminal_eeprom','PRINT 8\nSAVE\nNEW\nLOAD\nRUN\nQUIT\n','8\n')
terminal_case('terminal_error_recovery','PRINT 1/0\nRUN\nNEW\nPRINT 9\nRUN\nQUIT\n','9\n','E10 line 1:')
with tempfile.TemporaryDirectory(prefix='basic10-files-',dir=exe.parent) as folder:
    path=(Path(folder)/'saved program.bas').as_posix()
    terminal_case('terminal_file_save_load',f'PRINT 13\nSAVE "{path}"\nNEW\nLOAD "{path}"\nRUN\nQUIT\n','13\n')
passed=sum(x['passed'] for x in results)
print(f'BASIC 1.0: {passed}/{len(results)} PASS')
if os.environ.get('SB_EXT_REPORT'): Path(os.environ['SB_EXT_REPORT']).write_text(json.dumps(results,indent=2)+'\n')
sys.exit(passed!=len(results))
