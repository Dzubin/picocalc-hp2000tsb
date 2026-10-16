import struct,sys
TOTAL=600000; SPC=8; RES=32
fat=(TOTAL//SPC//128)+2
bs=bytearray(512)
bs[0:3]=b'\xEB\x58\x90'; bs[3:11]=b'MSDOS5.0'
struct.pack_into('<HBHBHHBHHHII',bs,11,512,SPC,RES,2,0,0,0xF8,0,63,255,0,TOTAL)
struct.pack_into('<IHHIHH',bs,36,fat,0,0,2,1,6)
bs[64]=0x80; bs[66]=0x29; bs[67:71]=b'\x01\x02\x03\x04'; bs[71:82]=b'NO NAME    '; bs[82:90]=b'FAT32   '
bs[510]=0x55; bs[511]=0xAA
img=bytearray((RES+2*fat+8)*512)
img[0:512]=bs; img[6*512:7*512]=bs
fs=bytearray(512)
struct.pack_into('<I',fs,0,0x41615252); struct.pack_into('<I',fs,484,0x61417272)
struct.pack_into('<III',fs,488,16000,3,0); struct.pack_into('<I',fs,508,0xAA550000)
img[512:1024]=fs
f=RES*512
struct.pack_into('<III',img,f,0x0FFFFFF8,0x0FFFFFFF,0x0FFFFFFF)
struct.pack_into('<III',img,f+fat*512,0x0FFFFFF8,0x0FFFFFFF,0x0FFFFFFF)
fh=open('sd.img','wb'); fh.write(img); fh.truncate(TOTAL*512); fh.close()
