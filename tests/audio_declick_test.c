#include "audio/openal/wave.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv) {
    int16_t data[4410];for(unsigned i=0;i<4410;++i)data[i]=10000;
    WAVFile wav={.header={.sample_rate=44100,.number_of_channels=1,.bits_per_sample=16},.data=(uint8_t*)data,.data_length=sizeof(data)};
    WAV_declickShortSample(&wav);
    assert(data[0]==0&&data[4409]==0&&data[43]==10000&&data[4366]==10000&&data[2205]==10000);
    for(unsigned i=1;i<44;++i)assert(data[i]>=data[i-1]&&data[i]-data[i-1]<=233);
    for(unsigned i=0;i<4410;++i)data[i]=10000;
    wav.header.number_of_channels=2;WAV_declickShortSample(&wav);assert(data[0]==10000&&data[4409]==10000);
    // Optional private test with the owner's actual Queen WAV samples.
    for(int i=1;i<argc;++i) {
        FILE* f=fopen(argv[i],"rb");assert(f);fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);
        uint8_t* bytes=malloc(size);assert(fread(bytes,1,size,f)==(size_t)size);fclose(f);
        WAVFile actual;assert(WAV_ParseFileData(bytes,size,&actual));
        uint8_t* copy=malloc(actual.data_length);memcpy(copy,actual.data,actual.data_length);
        WAV_declickShortSample(&actual);assert(!memcmp(copy+100,actual.data+100,actual.data_length-200));
        int16_t first,last;memcpy(&first,actual.data,2);memcpy(&last,actual.data+actual.data_length-2,2);
        assert(first==0&&last==0);free(copy);free(actual.data);free(bytes);
    }
    puts("PASS: short mono PCM edges taper smoothly; duration, interior samples and stereo remain unchanged");
}
