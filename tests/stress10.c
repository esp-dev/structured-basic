#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sbasic.h"
#include "mock.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { uint8_t a[16]; SBasic b; uint8_t z[16]; } Guard;
static Guard guard;
static SBMock mock;
static unsigned writes,polls,break_after;
static uint32_t random_state=UINT32_C(0x2468ace1);
static char source[4096];
#ifdef SB_SANITIZED
#include <sanitizer/common_interface_defs.h>
static unsigned current_iteration;
static void crash_record(void) {
    FILE *f;
    fprintf(stderr,"Reproduce: seed=0x2468ace1 iteration=%u; fuzz-crash.bas and fuzz-crash.bin\n",current_iteration);
    f=fopen("build/fuzz-crash.bas","wb"); if (f) { fwrite(source,1,strlen(source),f); fclose(f); }
    f=fopen("build/fuzz-crash.bin","wb"); if (f) { fwrite(&guard.b,1,sizeof(guard.b),f); fclose(f); }
}
#endif
static int reader(void *user,uint32_t offset) { return ((uint8_t *)user)[offset]; }
static int sink(void *user,const uint8_t *data,uint16_t n) { (void)user; (void)data; writes+=n; return writes>8192; }
static int poller(void *user) { (void)user; return break_after && ++polls>=break_after; }
static uint32_t random_number(void) {
    random_state^=(random_state&UINT32_C(0x7ffff))<<13;
    random_state^=random_state>>17;
    random_state^=(random_state&UINT32_C(0x7ffffff))<<5;
    return random_state;
}
static void canaries(void) {
    unsigned i;
    for (i=0;i<16;++i) { assert(guard.a[i]==0xa5); assert(guard.z[i]==0x5a); }
}
static SBError compile(const char *s) {
    SBSource input={reader,(void *)s,0}; input.length=(uint32_t)strlen(s);
    return sb_compile(&guard.b,input);
}
static SBError run(SBPlatform *p) { writes=polls=0; return sb_execute(&guard.b,p,1000,123); }
int main(void) {
    SBPlatform p; SBSource input; SBError e; uint8_t loaded[64]; uint16_t length;
    unsigned i,j,accepted=0,rejected=0,mutated=0;
#ifdef SB_SANITIZED
    __sanitizer_set_death_callback(crash_record);
#endif
    memset(&guard,0,sizeof(guard)); memset(guard.a,0xa5,16); memset(guard.z,0x5a,16);
    sb_mock_init(&mock); memset(&p,0,sizeof(p)); p.user=&mock; p.write=sink; p.poll=poller;
    p.device=sb_mock_device; p.nvread=sb_mock_read; p.nvwrite=sb_mock_write; p.nvsize=4096;
    input.read=reader; input.user=(void *)"PRINT 7\n"; input.length=8;
    assert(sb_save(&p,input)==SB_OK); memset(loaded,0xcc,sizeof(loaded));
    assert(sb_load(&p,loaded,sizeof(loaded),&length)==SB_OK && length==8 && !memcmp(loaded,input.user,8));
    mock.eeprom[12]^=1; assert(sb_load(&p,loaded,sizeof(loaded),&length)==SB_STORAGE && !length);
    assert(sb_save(&p,input)==SB_OK); mock.eeprom[4]=99; assert(sb_load(&p,loaded,sizeof(loaded),&length)==SB_STORAGE);
    assert(sb_save(&p,input)==SB_OK); assert(sb_load(&p,loaded,2,&length)==SB_STORAGE);
    input.length=4096; assert(sb_save(&p,input)==SB_STORAGE); input.length=8;
    /* Every interrupted write position must leave an invalid/uncommitted image. */
    for (i=0;i<=20;++i) {
        sb_mock_init(&mock); mock.fail_after=(int32_t)i;
        assert(sb_save(&p,input)==SB_IO); mock.fail_after=-1;
        assert(sb_load(&p,loaded,sizeof(loaded),&length)==SB_STORAGE);
    }
    sb_mock_init(&mock);
    assert(compile("WHILE 1\nWEND\n")==SB_OK); break_after=30;
    assert(run(&p)==SB_BREAK); break_after=0;
    assert(compile("PRINT 7\n")==SB_OK && run(&p)==SB_OK);
    assert(compile("DIM A$(2)\nA$(0)=\"x\"\nA$(1)=A$(0)\nPRINT A$(1)\n")==SB_OK);
    for (i=0;i<100;++i) { assert(run(&p)==SB_OK); canaries(); }
    for (i=0;i<50000;++i) {
#ifdef SB_SANITIZED
        current_iteration=i;
#endif
        unsigned category=i%10u;
        if (category==0) {
            unsigned n=(unsigned)(random_number()%100u);
            snprintf(source,sizeof(source),"DIM A(5)\nFOR I=0 TO 4\nA(I)=I+%u\nNEXT\nPRINT A(4)\n",n);
        } else if (category==1) {
            unsigned n=(unsigned)(random_number()%20u);
            snprintf(source,sizeof(source),"FUNCTION F$(N)\nLOCAL S$\nS$=\"x\"\nIF N=0 THEN\nRETURN \"\"\nEND IF\nRETURN S$+F$(N-1)\nEND FUNCTION\nPRINT LEN(F$(%u))\n",n);
        } else if (category==2) {
            unsigned n=(unsigned)(random_number()%4096u);
            snprintf(source,sizeof(source),"DIM A$(%u)\nA$(0)=\"a\"+\"b\"\nPRINT A$(0)\n",n);
        } else if (category==3) {
            snprintf(source,sizeof(source),"DIM X AS SINGLE\nX=%u.25\nSELECT CASE CINT(X)\nCASE 1,2\nPRINT UCASE$(\"x\")\nCASE ELSE\nPRINT X\nEND SELECT\n",(unsigned)(random_number()%8u));
        } else if (category==4) {
            strcpy(source,"I=0\nDO\nI=I+1\nIF I=3 THEN\nEXIT DO\nEND IF\nLOOP\nPRINT I\n");
        } else if (category==5) {
            unsigned n=(unsigned)(random_number()%270u);
            strcpy(source,"S$=\"");
            for (j=0;j<n;++j) strcat(source,"x");
            strcat(source,"\"\nPRINT LEN(S$+S$)\n");
        } else if (category==6) {
            static const char *const tokens[]={"IF ","THEN\n","END IF\n","PRINT ","DIM A(","LOCAL ","FUNCTION F(","RETURN ","NEXT\n","FOR ","SELECT CASE ","CASE ","END SELECT\n","S$", "1.5", "\"x\"", "(",")",",","=","+","/","\n","0"};
            source[0]=0;
            for (j=0;j<35;++j) strcat(source,tokens[random_number()%(sizeof(tokens)/sizeof(tokens[0]))]);
        } else if (category==7) {
            unsigned n=(unsigned)(random_number()%500u);
            for (j=0;j<n;++j) source[j]=(char)(1u+random_number()%127u);
            source[n]=0;
        } else if (category==8) {
            strcpy(source,"SUB S(N)\nLOCAL A$(2),I\nFOR I=0 TO N\nA$(0)=STR$(I)\nNEXT\nEND SUB\nCALL S(5)\nCALL S(2)\n");
        } else strcpy(source,"DIM A(3)\nPRINT A(-1)\n");
        e=compile(source);
        if (e==SB_OK) {
            ++accepted; e=run(&p); assert(e>=SB_OK && e<=SB_STORAGE);
            canaries();
            /* Independent bytecode/header mutation target. Every run validates
             * structure before interpreting bytes, then uses a step budget. */
            if (category<=4) {
                uint16_t end=guard.b.program_end;
                uint16_t offset=(uint16_t)(random_number()%end);
                guard.b.pool[offset]=(uint8_t)(random_number()&255u);
                run(&p); ++mutated; canaries();
            }
        } else ++rejected;
        canaries();
    }
    printf("STRESS PASS: 50000 attempts; seed=0x2468ace1; accepted=%u rejected=%u bytecode_mutations=%u; EEPROM faults, recovery and BREAK PASS\n",accepted,rejected,mutated);
    return 0;
}
