#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "sdcard.h"
#include "fat32.h"
#include "fat32.c"
int main(int argc,char**argv){
    fat32_file_t d,f; int i; static uint8_t buf[4096];
    fat32_init(); if(!fat32_is_ready()){fprintf(stderr,"not ready %s\n",fat32_error_string(fat32_get_status()));return 1;}
    fat32_error_t e=fat32_dir_create(&d,"hp2000access"); fprintf(stderr,"mkdir %s\n",fat32_error_string(e));
    for(i=1;i<argc;i++){
        char path[256]; const char*base=strrchr(argv[i],'/'); base=base?base+1:argv[i];
        snprintf(path,sizeof path,"hp2000access/%s",base);
        FILE*in=fopen(argv[i],"rb"); if(!in){perror(argv[i]);return 1;}
        e=fat32_create(&f,path); if(e){fprintf(stderr,"create %s: %s\n",path,fat32_error_string(e));return 1;}
        size_t n,put; unsigned long tot=0;
        while((n=fread(buf,1,sizeof buf,in))>0){ e=fat32_write(&f,buf,n,&put); if(e||put!=n){fprintf(stderr,"write err %s\n",fat32_error_string(e));return 1;} tot+=n;}
        fat32_close(&f); fclose(in); fprintf(stderr,"%s %lu\n",path,tot);
    }
    return 0;
}
