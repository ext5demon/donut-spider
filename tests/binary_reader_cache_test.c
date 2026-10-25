#include "binary_reader.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    FILE* file=tmpfile();assert(file);
    unsigned char source[8192],cache[128],out[350];
    for(unsigned i=0;i<sizeof(source);++i)source[i]=(i*13+i/11)&255;
    assert(fwrite(source,1,sizeof(source),file)==sizeof(source));rewind(file);
    BinaryReader br=BinaryReader_create(file,sizeof(source));
    BinaryReader_setFileCache(&br,cache,sizeof(cache));
    for(unsigned i=0;i<1000;++i) {
        BinaryReader_seek(&br,(i%20)*4);
        uint32_t n=BinaryReader_readUint32(&br),offset=(i%20)*4;
        assert(n==((uint32_t)source[offset]|(uint32_t)source[offset+1]<<8|(uint32_t)source[offset+2]<<16|(uint32_t)source[offset+3]<<24));
    }
    assert(br.fileCacheFills==1);
    BinaryReader_seek(&br,120);BinaryReader_readBytes(&br,out,sizeof(out));
    assert(memcmp(out,source+120,sizeof(out))==0 && BinaryReader_getPosition(&br)==470);
    unsigned char* other=BinaryReader_readBytesAt(&br,7000,250);
    assert(memcmp(other,source+7000,250)==0 && BinaryReader_getPosition(&br)==470);free(other);
    BinaryReader_skip(&br,12);assert(BinaryReader_readUint8(&br)==source[482]);
    BinaryReader_seek(&br,sizeof(source)-1);assert(BinaryReader_readUint8(&br)==source[sizeof(source)-1]);
    assert(BinaryReader_getPosition(&br)==sizeof(source));fclose(file);
    puts("PASS: bounded lazy read window, pointer-table seeks, boundary reads, seek restore and EOF");
}
