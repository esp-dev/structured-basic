#include "sbasic.h"
#include <assert.h>
#include <string.h>
typedef struct { const uint8_t *data; size_t size; } Input;
static struct { uint8_t before[16]; SBasic b; uint8_t after[16]; } guard;
static int read_byte(void *user,uint32_t offset) { Input *p=user; return offset<p->size?p->data[offset]:-1; }
static int output(void *user,const uint8_t *data,uint16_t n) { (void)user; (void)data; (void)n; return 0; }
int LLVMFuzzerTestOneInput(const uint8_t *data,size_t size) {
    Input input={data,size}; SBSource source={read_byte,&input,(uint32_t)size};
    SBPlatform platform; unsigned i;
    if (size>4096) return 0;
    memset(&platform,0,sizeof(platform)); platform.write=output;
    memset(guard.before,0xa5,16); memset(guard.after,0x5a,16);
    if (sb_compile(&guard.b,source)==SB_OK) sb_execute(&guard.b,&platform,300,1);
    for (i=0;i<16;++i) { assert(guard.before[i]==0xa5); assert(guard.after[i]==0x5a); }
    return 0;
}
