#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "sdcard.h"
static FILE *img;
unsigned long sim_reads, sim_writes;
static void need(void){ if(!img){ const char*p=getenv("SDIMG"); img=fopen(p?p:"sd.img","r+b"); if(!img){perror("sd.img");exit(9);} } }
void sleep_ms(uint32_t ms){(void)ms;}
void sd_init(void){}
sd_error_t sd_card_init(void){return SD_OK;}
bool sd_card_present(void){return true;}
bool sd_is_sdhc(void){return true;}
sd_error_t sd_read_block(uint32_t b, uint8_t *buf){ need(); sim_reads++; fseek(img,(long)b*512,SEEK_SET); return fread(buf,1,512,img)==512?SD_OK:SD_ERROR_READ_FAILED; }
static unsigned long fail_count;
sd_error_t sd_write_block(uint32_t b, const uint8_t *buf){ need(); sim_writes++; { const char*e=getenv("SD_FAIL_EVERY"); if (e && atoi(e)>0 && ++fail_count % (unsigned long)atoi(e) == 0) return SD_ERROR_WRITE_FAILED; } fseek(img,(long)b*512,SEEK_SET); size_t n=fwrite(buf,1,512,img); fflush(img); return n==512?SD_OK:SD_ERROR_WRITE_FAILED; }
sd_error_t sd_read_blocks(uint32_t s,uint32_t n,uint8_t*buf){ while(n--){ sd_error_t e=sd_read_block(s++,buf); if(e)return e; buf+=512;} return SD_OK;}
sd_error_t sd_write_blocks(uint32_t s,uint32_t n,const uint8_t*buf){ while(n--){ sd_error_t e=sd_write_block(s++,buf); if(e)return e; buf+=512;} return SD_OK;}
const char *sd_error_string(sd_error_t e){(void)e;return "sd";}
