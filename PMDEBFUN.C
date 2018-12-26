#define INCL_DOS
#define INCL_GPI
#define INCL_WIN
#include "os2.h"
#define PASFAR pascal far

writequeue(char far *txt, USHORT mode, USHORT len)
{
   SEL selector, qownersel;
   char far *p;
   char far *ptr;
   int i;
   static HQUEUE hq=NULL;
   static char queuename[] = "\\QUEUES\\PMDEBUG";
   static int pid;

   if (!hq && DosOpenQueue((USHORT*)&pid, &hq, queuename))
      return -1;

   if (DosAllocSeg( len, &selector,SEG_GIVEABLE))
      return -2;

   ptr = MAKEP(selector,0);

   for (i=0; i<len; i++)
      ptr[i] = txt[i];

   if (DosGiveSeg(selector, pid, &qownersel))
      return -3;

   ptr = MAKEP(qownersel,0);

   if(DosWriteQueue(hq ,mode, len, (PBYTE)ptr,0))
      return -4;

   DosFreeSeg(selector);
}

int PASFAR debug_puts(char far *txt)
{
   USHORT i; char far *p;

   p = txt;
   i = 0;

   while (*p)  { p++; i++; } 
   return writequeue(txt,0,i);
}

int PASFAR debug_logmessage(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2, char far *txt)
{
   QMSG q;
   int i; char far *p;
   p = txt;
   i = 0;

   while (*p)  { p++; i++; } 

   if (i = writequeue(txt,0,i)) return i;

   q.msg  = msg;
   q.hwnd = hwnd;
   q.mp1 = mp1;
   q.mp2 = mp2;

   if (i = writequeue((void far *) &q, 1, sizeof(QMSG)))
      return i;
   return 0;
}
