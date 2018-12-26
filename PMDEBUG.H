#ifndef PASFAR
   #define PASFAR pascal far
#endif
#ifdef INCL_WIN

#if INTERNAL
   HWND PASFAR debug_open(HAB host_hab,HMQ host_hmq,HWND host_hwndclient,char far *name,
       USHORT flag,ULONG resv);
#endif
   int PASFAR debug_logmessage(HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2, char far *str);
#endif

#if INTERNAL
   int PASFAR debug_charinput(int key);
   int PASFAR debug_close(void );
   int PASFAR debug_putchr(char ch);
#endif
int PASFAR debug_puts(char far *txt);

#define WM_DEBUGOPEN (WM_USER+0x1000)
#define WM_DEBUG_PUTS (WM_USER+0x1000)
#define WM_DEBUG_LOGMESSAGE (WM_USER+0x1001)

/*
 * return values:
 *  -1 : shared data not available
 *  0  : OK
 *  1  : another thread or process is processing shared data
 */

typedef struct _DEBUG_DATA
{
   HWND hwndframe;
   HWND hwndclient;      /* client window handle of debug window */
   HPS  hps;
   char buffer[300];
   QMSG qmsg;
} DEBUG_DATA;
