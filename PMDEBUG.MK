link=/cod

pmdebug.res: pmdebug.rc
   rc -r pmdebug.rc
 
.c.obj:
   cl -c -Zi -G2w -Gs -AS $*.c

pmdebug.obj: pmdebug.c

pmdebug.exe: pmdebug.obj pmdebug.def pmdebug.mk
   link $(link)/align:16  pmdebug ,pmdebug,nul,os2,pmdebug.def;
   rc pmdebug.res pmdebug.exe

pmdebug.exe: pmdebug.res
   rc pmdebug.res pmdebug.exe
