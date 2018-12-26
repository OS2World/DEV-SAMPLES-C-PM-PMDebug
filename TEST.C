/*-----------------------------------------------------
   PMSTUB.C -- A Program that Creates a Client Window
  -----------------------------------------------------*/

#define INCL_DOS
#define INCL_GPI
#define INCL_WIN
#include "os2.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

#define PASFAR pascal far

HAB hab;
HPS hps;
HWND hwndframe, hwndclient, hwmsgbox;

int PASFAR debug_puts(char far *txt);

MRESULT EXPENTRY wndproc (HWND, USHORT, MPARAM, MPARAM) ;

int main (void)
{
   static CHAR  name [] = "pmstub" ;
   static ULONG fl = FCF_TITLEBAR      | FCF_SYSMENU |
                               FCF_SIZEBORDER    | FCF_MINMAX  |
                               FCF_SHELLPOSITION | FCF_TASKLIST ;
   HMQ          hmq ;
   QMSG         qmsg ;

   printf("\n*** START TEST ***\n");
   hab = WinInitialize (0) ;
   hmq = WinCreateMsgQueue (hab, 0) ;

   WinRegisterClass (
                  hab,                /* Anchor block handle */
                  name,               /* Name of class being registered */
                  wndproc,            /* Window procedure for class */
                  0L,                 /* Class style */
                  0) ;                /* Extra bytes to reserve */

   hwndframe = WinCreateStdWindow (
                  HWND_DESKTOP,       /* Parent window handle */
                  WS_VISIBLE,         /* Style of frame window */
                  &fl,                /* Pointer to control data */
                  name,               /* Client window class name */
                  NULL,               /* Title bar text */
                  0L,                 /* Style of client window */
                  NULL,               /* Module handle for resources */
                  0,                  /* ID of resources */
                  &hwndclient) ;      /* Pointer to client window handle */

   while (WinGetMsg (hab, &qmsg, NULL, 0, 0))
        WinDispatchMsg (hab, &qmsg) ;

   WinDestroyWindow (hwndframe) ;
   WinDestroyMsgQueue (hmq) ;
   WinTerminate (hab) ;
   printf("\n***  END  TEST ***\n");
   return 0 ;
}

int printf(const char *txt,...)
{
   char _buffer[400], *p;
   va_list array;

   if (strchr(txt,'%'))
   {
      va_start(array,txt);
      vsprintf(_buffer, txt, array);
   }
   else
      strcpy(_buffer,txt);
   debug_puts(_buffer);
}

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

MRESULT EXPENTRY wndproc (HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   char buff[100];
         debug_logmessage(hwnd, msg, mp1,mp2,"hello   ");
   switch (msg)
   {
      case WM_CREATE:
         break;

      case WM_SIZE:
         break;

      case WM_PAINT:
         hps = WinBeginPaint(hwnd, NULL, NULL);
         GpiErase(hps);
         WinEndPaint(hps);
         break;

      case WM_CHAR:
         printf("KEY pressed !!mp1=%08lXh, mp2=%08lXh\n",mp1,mp2);
         break;
  
      case WM_TIMER:
         break;

      case WM_COMMAND:
         break;

   }
   return WinDefWindowProc (hwnd, msg, mp1, mp2) ;
}
