#include "picker_storage.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void u32(unsigned char* b,unsigned n) {b[0]=n;b[1]=n>>8;b[2]=n>>16;b[3]=n>>24;}
static void wad(const char* path,int chapter) {
    unsigned char b[512]={0};memcpy(b,"FORM",4);u32(b+4,504);memcpy(b+8,"GEN8",4);u32(b+12,128);b[17]=17;
    u32(b+16+100,256);snprintf((char*)b+256,sizeof(b)-256,"DELTARUNE Chapter %d",chapter);
    FILE* f=fopen(path,"wb");assert(f && fwrite(b,1,sizeof(b),f)==sizeof(b));fclose(f);
}
static void empty(const char* path) {FILE* f=fopen(path,"wb");assert(f);fclose(f);}
static void makeDir(const char* path) {assert(mkdir(path,0700)==0);}
int main(void) {
    char base[]="/tmp/spider-picker-XXXXXX";assert(mkdtemp(base));assert(chdir(base)==0);
    makeDir("usb");makeDir("hdd");makeDir("usb/renamed");makeDir("hdd/complete");
    wad("usb/renamed/game.win",2); // no texture bundle: found, not playable
    wad("hdd/complete/renamed.WIN",2);empty("hdd/complete/textures.bin");
    makeDir("usb/DELTARUNE");makeDir("usb/DELTARUNE/chapter1_windows");
    // Deliberately misleading folder; chapter identity comes from the WAD.
    wad("usb/DELTARUNE/chapter1_windows/data.win",4);empty("usb/DELTARUNE/chapter1_windows/TEXTURES.BIN");
    makeDir("hdd/deep");makeDir("hdd/deep/collection");
    wad("hdd/deep/collection/third.win",3);empty("hdd/deep/collection/TEXTURES.BIN");
    assert(symlink("../..","hdd/deep/collection/cycle")==0);
    empty("hdd/unrelated.win");wad("hdd/launcher.win",0);
    const char* roots[]={"usb","hdd","usb","missing"};PickerScan* scan=PickerScan_create(roots,4);assert(scan);
    const PickerChapter* c=PickerScan_chapters(scan);
    assert(c[3].ready && !c[0].path[0] && !c[1].path[0]);
    PickerScan_step(scan,1);assert(!PickerScan_finished(scan));
    unsigned frames=0;while (!PickerScan_finished(scan) && ++frames<1000) PickerScan_step(scan,1);
    assert(frames<1000 && c[1].ready && strstr(c[1].path,"hdd/complete"));
    assert(c[2].ready && c[3].ready && !c[4].ready && !c[0].ready);
    assert(PickerStorage_chapter("hdd/launcher.win")==0 && PickerStorage_chapter("hdd/unrelated.win")==0);
    assert(PickerStorage_chapter("hdd/complete/renamed.WIN")==2);
    assert(!PickerStorage_hasTextures("usb/renamed/game.win"));
    PickerScan_destroy(scan);
    wad("usb/data.win",1);empty("usb/TEXTURES.BIN");
    scan=PickerScan_create(roots,4);assert(scan);
    assert(PickerScan_chapters(scan)[0].ready);PickerScan_destroy(scan);
    unlink("usb/data.win");unlink("usb/TEXTURES.BIN");
    // Cleanup only the known fixture tree, never a recursive computed delete.
    unlink("hdd/deep/collection/cycle");unlink("hdd/deep/collection/third.win");unlink("hdd/deep/collection/TEXTURES.BIN");
    unlink("hdd/complete/renamed.WIN");unlink("hdd/complete/textures.bin");unlink("hdd/unrelated.win");unlink("hdd/launcher.win");
    unlink("usb/renamed/game.win");unlink("usb/DELTARUNE/chapter1_windows/data.win");unlink("usb/DELTARUNE/chapter1_windows/TEXTURES.BIN");
    rmdir("hdd/deep/collection");rmdir("hdd/deep");rmdir("hdd/complete");rmdir("hdd");rmdir("usb/renamed");
    rmdir("usb/DELTARUNE/chapter1_windows");rmdir("usb/DELTARUNE");rmdir("usb");assert(chdir("/")==0);rmdir(base);
    puts("PASS: bounded search, split storage, renamed copies, chapter identity, incomplete copies, duplicates and symlink cycle");
}
