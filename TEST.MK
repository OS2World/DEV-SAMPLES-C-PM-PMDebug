.c.obj:
   cl -c -Zi -G2w -Gs  $*.c

test.obj: test.c

.asm.obj:
   masm -ml  $*.asm;

test.exe: test.obj def.
   link /cod/align:16  test ,test,nul,os2,def.;
