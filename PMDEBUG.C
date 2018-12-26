/*
 *    dumb TTY in a window
 */
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <time.h>

typedef void far *HANDLE;

#define INCL_DOS
#define INCL_WIN
#define INCL_GPI
#include "os2.h"

static UCHAR debug = 0, sync=0, minimized = 0;

static HDC       hdm ;
static HPS       hpm ;

static SEL sel=0;
static HAB hab;
static HMQ hmq;
static UCHAR fonttype;

static HPS hps=NULL;
static HWND hwndframe=NULL, hwndclient, hwndclienthost;
static UCHAR busy, flush=0;
static char title[60]= "Debug Window";

#define DEBUG_PUTSEVENT     0
#define DEBUG_LOGEVENT      1

static HQUEUE hq;
static QUEUERESULT qresc;
static char queuename[] = "\\QUEUES\\PMDEBUG";

#define FKEY     0x9F
#define CR       13
#define LF       10
#define BS        8
#define ESC      27
#define CURCOLOR -1

FILE *fp=NULL;
char LOG[] = "LO";
char filename[80];

static USHORT ixmax, iymax, xscreen, yscreen, cxchar, cychar, cydesc;
static int arg_c; char **arg_v;

static char *screenbuf=NULL;
static USHORT line=0, ncols, nlines, col=0, nlineswin, ncolswin;

static FONTMETRICS fm[10];
static FATTRS vfat;
static char fname[8];

static int ezfloadsmallfont(HPS hps, int type)
{
   long nfonts=5L;
   POINTL ptl;
   RECTL rcl;
   int i;
   
   ptl.x = 10;
   ptl.y = 10;
   if (!GpiLoadFonts( hab,"seefonts"))
      return 0;
   
   GpiQueryFonts ( hps, (ULONG) QF_PRIVATE,
      NULL, &nfonts, (LONG) sizeof(FONTMETRICS), fm);

   i = type;
   vfat.usRecordLength  =  sizeof(FATTRS) ;
   vfat.lMatch          =  fm[i].lMatch ;
   strcpy(vfat.szFacename, fm[i].szFacename) ;
   vfat.idRegistry      =  fm[i].idRegistry ;
   vfat.usCodePage      =  fm[i].usCodePage ;
   vfat.fsType          =  FATTR_TYPE_FIXED;

   if (GpiCreateLogFont( hps, (PSTR8) fname, (LONG)i+1, &vfat ) != 2)
      return 0;
   GpiSetCharSet( hps,(LONG)i+1);
   return 1;
}

int error_message(USHORT flag, char *name, char *str,...)
{
   int i;
   USHORT flag1=MB_OK;
   char *p;
   char _buffer[288];
   va_list array;

   va_start(array,str);
   vsprintf(_buffer,str,array);

   switch (flag & 7)
   {
      case 1:
      flag1 |= MB_ICONHAND;
      break;

      case 2:
      flag1 |= MB_ICONQUESTION;
      break;

      case 3:
      flag1 |= MB_ICONEXCLAMATION;
      break;
   }

   i = WinMessageBox (HWND_DESKTOP, hwndframe ? hwndframe : HWND_DESKTOP,
      _buffer,name,0,flag1);
   if (i == MBID_YES)
      return 0;
   return i;
}

#define RESTYPE 300
#define MAXICONS 40
#define ncolors 16
#define CX      32
#define CY      32

typedef struct
{
   BITMAPINFOHEADER bh;
   RGB rgb[16];
   char data[CX*CY*4/8];
} COLORICON;
typedef COLORICON far *PCOLORICON;

typedef struct
{
   USHORT id;
   PCOLORICON cic;
} ICONTABLE ;
static ICONTABLE icontable[MAXICONS];
static USHORT nicons=0;

static RECTL r = {0,0,CX,CY };

HANDLE pascal far loadicon(USHORT icon_id)
{
   SEL sel;
   HANDLE p;
   int i;

   if (nicons == MAXICONS)
      return NULL;
   i = DosGetResource(NULL, RESTYPE, icon_id, &sel);
   if (i)
      return NULL;
   icontable[nicons].id = icon_id;
   p = MAKEP(sel,0);
   icontable[nicons].cic = p;
   nicons++;
   return p;
}

int pascal far color_icon(HANDLE hwndf, HANDLE hwnd, int id, int flag)
{
   BITMAPINFO *b;
   HPS hps;
   HBITMAP h;
   POINTL ptl;
   static int xold,yold;
   static PCOLORICON cic;
   int i;
   SWP swp;

   WinQueryWindowPos(hwndf,&swp);     /* not minimized */
   if (!(swp.fs & SWP_MINIMIZE))
      return 0;

   for (i=0; i<nicons; i++)          /* search id in table */
   {
      if (icontable[i].id == id)
         break;
   }
   if (i == nicons) return FALSE;    /* not found */

   cic = icontable[i].cic;
   ptl.x = 0;
   ptl.y = 0;
   hps = WinBeginPaint (hwnd,NULL, NULL);
   h = GpiCreateBitmap(hps, &cic->bh, CBM_INIT, 
      cic->data, (PBITMAPINFO)&cic->bh);
   if (h)
   {
      WinDrawBitmap(hps,h,&r,&ptl,0L,0L,DBM_IMAGEATTRS);
      GpiDeleteBitmap(h);
   }
   WinEndPaint(hps);
   if (!h)
      return FALSE;
   return TRUE;
}

static int menuchange(HANDLE hwnd, USHORT menuitem, USHORT flag, char *ptr,...)
{
   HWND h;
   va_list array;
   char buf[200],*p;
   USHORT mode,flag1;

   if (!hwnd) hwnd = hwndframe;
   flag1 = flag & 0xFF;
   h = WinWindowFromID(hwnd, FID_MENU);
   if (h)
   {
      p = ptr;
      if (ptr && strchr(ptr,'%'))
      {
         va_start(array,ptr);
         vsprintf(buf,ptr,array);
         p = buf;
      }
      switch (flag1)
      {
         case 0:
         case 1:
            switch (flag >> 8)
            {
               case 0:
                  mode = MIA_DISABLED;
                  break;
               case 1:
                  mode = MIA_CHECKED;
                  flag ^= 1;        /* menu checked flag is reversed in OS/2 */
                  break;
            }
            WinSendMsg(h, MM_SETITEMATTR, MPFROM2SHORT(menuitem, TRUE),
                                    MPFROM2SHORT(mode, (flag & 1) ? 0 : mode));
            break;
         case 2:
            WinSendMsg(h, MM_SETITEMTEXT, MPFROMSHORT(menuitem), MPFROMP(p));
            break;
      }
   }
}
                             /* should be called from WM_CHAR message */
static int charfrommsg(MPARAM mp1, MPARAM mp2)
{
   int i,ctrl=0,vk; USHORT ch;
   int state = 0;

   ch = SHORT1FROMMP(mp2);       /* char code */
   vk = SHORT2FROMMP(mp2);       /* virtual key code */

   i = SHORT1FROMMP(mp1);
   if (i & KC_KEYUP)
      return 0 ;

   if (i & KC_INVALIDCHAR)
      return 0 ;

   if (i & KC_SHIFT)
      state |= 0x300;
   if (i & KC_CTRL)
      state |= 0x400;
   if (i & KC_ALT)
      state |= 0x800;
   if (!vk)
   {
      ctrl = (i & KC_CTRL);
   }
   else
   {
      switch (vk)
      {
         case VK_SHIFT:
         case VK_CTRL:
         case VK_ALT:
            return 0;
         case VK_BREAK:           /* abortion key Ctrl-Break */
            return 0xFF;
         case VK_BACKSPACE:
            return 8 | state;
         case VK_ENTER:
         case VK_NEWLINE:
            return 13 | state;
         case VK_ESC:
            return 27 | state;
         case VK_SPACE:
            return ' ' | state;
         case VK_TAB:
            return 9   | state;
         default:
            return vk | 0x80 | state;
      }
   }

   i = CHAR1FROMMP(mp2);         /* ascii code */
   if (i && ctrl)
   {
      return (i & 0x1F) | state;           /* control key */
   }

   return i; /* | state; */
}

int tasklist(char *str,...)
{
   char buffer[288];
   va_list array;
   static SWCNTRL swctl = { 0 , 0 , 0 , 0 , 0 , SWL_VISIBLE ,
                             SWL_JUMPABLE , NULL , 0 } ;
   PID pid;
   TID tid;
   static HSWITCH hsw = NULL;

   if (!hwndframe) return;
   va_start(array,str);
   vsprintf(buffer,str,array);
   buffer[MAXNAMEL] = 0;      /* truncate to max length */
   strcpy(swctl.szSwtitle,buffer);

    /* add ourselves to the switch list */
   WinQueryWindowProcess ( hwndframe , & pid , & tid ) ;
   swctl.hwnd = hwndframe ;
   swctl.idProcess = pid ;
   if (!hsw)
      hsw = WinAddSwitchEntry (&swctl) ;
   else
      WinChangeSwitchEntry(hsw, &swctl);
   WinSetWindowText(hwndframe, buffer);
   return strlen(buffer);
}

wrtext(int l, int c, char *txt, USHORT color, USHORT nbytes)
{
   POINTL ptl;
   RECTL r;

   if (!hps || minimized)
      return ;
   if (!nbytes)
      nbytes = strlen(txt);
   l = nlines-1-l;
   ptl.x = c*cxchar;
   ptl.y = cydesc+l*cychar;
   r.xLeft = ptl.x;
   r.yTop  = ptl.y + cychar -1 - cydesc;
   r.xRight = ptl.x + nbytes*cxchar -1;
   r.yBottom =   ptl.y - cydesc;
   if (color != -1)
      GpiSetColor(hps, (ULONG)color);
   GpiCharStringPosAt (hps, &ptl, &r, CHS_OPAQUE, (LONG)nbytes ,txt, NULL);
}
MRESULT EXPENTRY wndproc (HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2);

setwindow()
{
   ULONG ll = SWP_SHOW;

   if (argoption('i'))
      ll |= SWP_MINIMIZE;

   WinSetWindowPos(hwndframe, NULL, 0, 0, 0, 0, ll);
}

winitall()
{
   static ULONG fl = FCF_TITLEBAR      | FCF_SYSMENU | FCF_ACCELTABLE |
                     FCF_SIZEBORDER    | FCF_MINMAX  | 
                     FCF_SHELLPOSITION | FCF_MENU;
   ULONG ll;
   QMSG         q ;
   int restore();
   
   hab = WinInitialize(0);
   hmq = WinCreateMsgQueue(hab,0);
   ll = CS_SIZEREDRAW;
   if (sync = argoption('s'))
      ll |= CS_SYNCPAINT;
   WinRegisterClass (
                  hab,                /* Anchor block handle */
                  title,              /* Name of class being registered */
                  wndproc,            /* Window procedure for class */
                  ll,                 /* Class style */
                  0) ;                /* Extra bytes to reserve */

   hwndframe = WinCreateStdWindow (
                  HWND_DESKTOP,       /* Parent window handle */
                  0L,
                                      /* Style of frame window */
                  &fl,                /* Pointer to control data */
                  title,              /* Client window class name */
                  title,              /* Title bar text */
                  0L,                 /* Style of client window */
                  NULL,               /* Module handle for resources */
                  0x234,              /* ID of resources */
                  &hwndclient) ;      /* Pointer to client window handle */


   if (hwndframe)
   {
      onexit(restore);
      while (WinGetMsg(hab, &q, NULL, 0,0))
         WinDispatchMsg(hab, &q);

      WinDestroyWindow (hwndframe) ;
   }
   WinDestroyMsgQueue(hmq);
   WinTerminate(hab);
}


static int scrollup()
{
   RECTL r;
   r.xLeft = 0;
   r.xRight = ixmax;
   r.yBottom = 0;
   r.yTop = cychar;
   if (!minimized)
      WinScrollWindow(hwndclient, 0, cychar, NULL, NULL, NULL, NULL,
         SW_INVALIDATERGN);
}

void far getinput()
{
   int i;
   UCHAR ch;
   while (1)
   {
      if ((i = fgetc(stdin)) != EOF)
      {
         ch = i;
         _debug_putchr(ch);
      }
   }
}

static int redraw()
{
   int i,k=nlines-nlineswin;

   busy=1;
   i = (line+1+k)%nlines;

   while (k < nlines)
   {
      if (i == nlines)
         i = 0;
      wrtext(k,0,&screenbuf[i*(ncols+1)], CURCOLOR, ncolswin);
      i++;
      k++;
   }
   busy=0;
   return 0 ;
}
#define const

static int _debug_puts(char far *txt)
{
   int i,n; char far *q;

   if (!hwndframe) return ;
   q = txt;
   while (*q)
   {
      _debug_putchr(*q);
      q++;
   }
   if (fp && flush)
      fflush(fp);
}

static int _debug_putchr(char ch)
{
   int i,j;

   if (!hwndframe || !hps) return;
   if (!screenbuf) return ;
/*    while (busy && sync) ; */
   if (fp) fputc(ch, fp);

   switch (ch)
   {
      case CR:
         col = 0;
         break;
      case LF:
         line++;
         if (line == nlines)
            line = 0;
         col=0;
         memset(&screenbuf[line*(ncols+1)],' ', ncols);
         screenbuf[line*(ncols+1)+ncols] = 0;

         scrollup();
         break;
      case BS:
         if (col)
         {
            col--;
            i = ' ';
            wrtext(nlines-1,col,(char*)&i,CURCOLOR,1);
            screenbuf[line*(ncols+1)+col] = ' ';
         }
         break;
      default:
         if (col < ncols)
         {
            wrtext(nlines-1,col,&ch,CURCOLOR,1);
            screenbuf[line*(ncols+1)+col] = ch;
            col++;
         }
         break;
   }
}         

void setup_screenbuf()
{
   int i;
   FONTMETRICS fm ;

   if (screenbuf) free(screenbuf);
   ezfloadsmallfont(hps, fonttype);
   GpiQueryFontMetrics (hps, (LONG) sizeof fm, &fm) ;
   cxchar = (int) fm.lAveCharWidth ;
   cychar = (int) fm.lMaxBaselineExt ;
   cydesc = (int) fm.lMaxDescender ;

   ncols = xscreen/cxchar;
   nlines = yscreen/cychar;
   screenbuf = malloc((ncols+1)*nlines);

   for (i=0; i<nlines; i++)            /* init with blanks */
   {
      memset(&screenbuf[i*(ncols+1)],' ', ncols);
      screenbuf[i*(ncols+1)+ncols] = 0;
   }
   col = 0;
   line = 0;
}

void far readqueue()
{
   USHORT i,len,pri;
   char far *p;

   HAB hab = WinInitialize(0);

   while (hq)
   {
      int counter;

      DosReadQueue(hq, &qresc, &len, &p, 0, DCWW_WAIT, (UCHAR*)&pri, NULL);

      switch (qresc.usEventCode)
      {
         case DEBUG_PUTSEVENT:

         for (i=0; i<len; i++)
         {
            _debug_putchr(p[i]);
         }
         break;
  
         case DEBUG_LOGEVENT:
         _debug_logmessage((PQMSG)p);
         break;
      }
      DosFreeSeg(SELECTOROF(p));
   }
}

static int plinit()
{
   HDC hdc;
   ULONG x,y; int i;
   SIZEL hello;
   char *stk;

   hdc = WinOpenWindowDC(hwndclient);

   DevQueryCaps(hdc,CAPS_WIDTH,  1L, &x);
   DevQueryCaps(hdc,CAPS_HEIGHT, 1L, &y);
   xscreen = x; 
   yscreen = y;
   hello.cx = 0;
   hello.cy = 0;

   hps = GpiCreatePS(hab,hdc,&hello,PU_PELS | GPIF_DEFAULT | GPIT_NORMAL |
         GPIA_ASSOC);

   setup_screenbuf();
#define STKSIZE 4096

   stk = malloc(STKSIZE);

   if (argoption('z'))       /* enable input from stdin */
   {
      if (stk)
     /*     _beginthread(getinput, stk, STKSIZE, NULL); */
         DosCreateThread(getinput, (PTID)&i, stk+STKSIZE);
   }
   else
   {

      if (!DosOpenQueue((USHORT*)&i, &hq, queuename))
      {
         DosCloseQueue(hq);
         error_message(3, "Duplicate", "Debug window already present!");
         exit(1);
      }
      if (DosCreateQueue(&hq, QUE_FIFO, queuename))
         return 1;
      DosCreateThread(readqueue, (PTID)&i, stk+STKSIZE);
   }
   return 0;
}

resize()
{
   nlineswin = (iymax+cychar-1)/cychar;
   ncolswin  = (ixmax+cxchar-1)/cxchar;
}

help()
{
   error_message(2,"HELP",
   "PMDEBUG ver. 1\n"
   "(C) 1990 C Freak Software Inc.\n\n"
   "Usage: PMDEBUG [-s] [-1] [-fname] [-i]\n"
   "-s: synchronous redraw\n"
   "-1: small 6x8 font\n"
   "-f<filename> named log file\n"
   "   -f as last parameter: no log\n"
   "-i: start under icon\n"
/*    "-z: input rather than debug window\n" */
/*    "    e.g. PMDEBUG -i < file\n" */
/*    "name: title bar\n" */
   );
}

MRESULT EXPENTRY wndproc (HWND hwnd, USHORT msg, MPARAM mp1, MPARAM mp2)
{
   int i;
   char _buf[400];

   switch (msg)
   {
      case WM_CREATE:
         hwndclient = hwnd;
         hwndframe = WinQueryWindow(hwnd,QW_PARENT,0);
         initall();
         if (plinit()) return 1L;
         setwindow();
         loadicon(0x234);
         break;

      case WM_SETFOCUS:
         return 0L;

      case WM_SIZE:
         ixmax = SHORT1FROMMP(mp2);
         iymax = SHORT2FROMMP(mp2);
         resize();      
         break;

      case WM_PAINT:
         if (color_icon(hwndframe, hwndclient, 0x234, 0))
         {
            minimized = 1;
            return 1L;
         }
         minimized = 0;
         WinBeginPaint(hwnd, hps, NULL);
         redraw();
         WinEndPaint(hps);
         break;

      case WM_COMMAND:
         i = SHORT1FROMMP(mp1);
         goto chr;

      case WM_CHAR:
         i = charfrommsg(mp1,mp2);
         chr:
         keyevent(i);
         return 0;

      case WM_DESTROY:
         hwndframe = NULL;
         GpiAssociate(hps,NULL);
         GpiDestroyPS(hps);
         if (sel)
            DosFreeSeg(sel);
         break;

   }
   return WinDefWindowProc (hwnd, msg, mp1, mp2) ;
}

static char buf[80];
typedef struct {
   char *name;
   USHORT msg;
} MSGS;

static MSGS msgs[] = 
{
 "WM_NULL                ", 0x0000,
 "WM_CREATE              ", 0x0001,
 "WM_DESTROY             ", 0x0002,
 "WM_OTHERWINDOWDESTROYED", 0x0003,
 "WM_ENABLE              ", 0x0004,
 "WM_SHOW                ", 0x0005,
 "WM_MOVE                ", 0x0006,
 "WM_SIZE                ", 0x0007,
 "WM_ADJUSTWINDOWPOS     ", 0x0008,
 "WM_CALCVALIDRECTS      ", 0x0009,
 "WM_SETWINDOWPARAMS     ", 0x000a,
 "WM_QUERYWINDOWPARAMS   ", 0x000b,
 "WM_HITTEST             ", 0x000c,
 "WM_ACTIVATE            ", 0x000d,
 "WM_SETFOCUS            ", 0x000f,
 "WM_SETSELECTION        ", 0x0010,
 "WM_COMMAND             ", 0x0020,
 "WM_SYSCOMMAND          ", 0x0021,
 "WM_HELP                ", 0x0022,
 "WM_PAINT               ", 0x0023,
 "WM_TIMER               ", 0x0024,
 "WM_SEM1                ", 0x0025,
 "WM_SEM2                ", 0x0026,
 "WM_SEM3                ", 0x0027,
 "WM_SEM4                ", 0x0028,
 "WM_CLOSE               ", 0x0029,
 "WM_QUIT                ", 0x002a,
 "WM_SYSCOLORCHANGE      ", 0x002b,
 "WM_SYSVALUECHANGED     ", 0x002d,
 "WM_CONTROL             ", 0x0030,
 "WM_VSCROLL             ", 0x0031,
 "WM_HSCROLL             ", 0x0032,
 "WM_INITMENU            ", 0x0033,
 "WM_MENUSELECT          ", 0x0034,
 "WM_MENUEND             ", 0x0035,
 "WM_DRAWITEM            ", 0x0036,
 "WM_MEASUREITEM         ", 0x0037,
 "WM_CONTROLPOINTER      ", 0x0038,
 "WM_CONTROLHEAP         ", 0x0039,
 "WM_QUERYDLGCODE        ", 0x003a,
 "WM_INITDLG             ", 0x003b,
 "WM_SUBSTITUTESTRING    ", 0x003c,
 "WM_MATCHMNEMONIC       ", 0x003d,
 "WM_USER                ", 0x1000,
 "WM_MOUSEFIRST          ", 0x0070,
 "WM_MOUSELAST           ", 0x0079,
 "WM_BUTTONCLICKFIRST    ", 0x0071,
 "WM_BUTTONCLICKLAST     ", 0x0079,
 "WM_MOUSEMOVE           ", 0x0070,
 "WM_BUTTON1DOWN         ", 0x0071,
 "WM_BUTTON1UP           ", 0x0072,
 "WM_BUTTON1DBLCLK       ", 0x0073,
 "WM_BUTTON2DOWN         ", 0x0074,
 "WM_BUTTON2UP           ", 0x0075,
 "WM_BUTTON2DBLCLK       ", 0x0076,
 "WM_BUTTON3DOWN         ", 0x0077,
 "WM_BUTTON3UP           ", 0x0078,
 "WM_BUTTON3DBLCLK       ", 0x0079,
 "WM_CHAR                ", 0x007a,
 "WM_VIOCHAR             ", 0x007b,
 "WM_FLASHWINDOW         ", 0x0040,
 "WM_FORMATFRAME         ", 0x0041,
 "WM_UPDATEFRAME         ", 0x0042,
 "WM_FOCUSCHANGE         ", 0x0043,
 "WM_SETBORDERSIZE       ", 0x0044,
 "WM_TRACKFRAME          ", 0x0045,
 "WM_MINMAXFRAME         ", 0x0046,
 "WM_SETICON             ", 0x0047,
 "WM_QUERYICON           ", 0x0048,
 "WM_SETACCELTABLE       ", 0x0049,
 "WM_QUERYACCELTABLE     ", 0x004a,
 "WM_TRANSLATEACCEL      ", 0x004b,
 "WM_QUERYTRACKINFO      ", 0x004c,
 "WM_QUERYBORDERSIZE     ", 0x004d,
 "WM_NEXTMENU            ", 0x004e,
 "WM_ERASEBACKGROUND     ", 0x004f,
 "WM_QUERYFRAMEINFO      ", 0x0050,
 "WM_QUERYFOCUSCHAIN     ", 0x0051,
 "WM_QUERYFRAMECTLCOUNT  ", 0x0059,
 "WM_RENDERFMT           ", 0x0060,
 "WM_RENDERALLFMTS       ", 0x0061,
 "WM_DESTROYCLIPBOARD    ", 0x0062,
 "WM_PAINTCLIPBOARD      ", 0x0063,
 "WM_SIZECLIPBOARD       ", 0x0064,
 "WM_HSCROLLCLIPBOARD    ", 0x0065,
 "WM_VSCROLLCLIPBOARD    ", 0x0066,
 "WM_DRAWCLIPBOARD       ", 0x0067,
 "WM_DDE_FIRST           ", 0x00A0,
 "WM_DDE_INITIATE        ", 0x00A0,
 "WM_DDE_REQUEST         ", 0x00A1,
 "WM_DDE_ACK             ", 0x00A2,
 "WM_DDE_DATA            ", 0x00A3,
 "WM_DDE_ADVISE          ", 0x00A4,
 "WM_DDE_UNADVISE        ", 0x00A5,
 "WM_DDE_POKE            ", 0x00A6,
 "WM_DDE_EXECUTE         ", 0x00A7,
 "WM_DDE_TERMINATE       ", 0x00A8,
 "WM_DDE_INITIATEACK     ", 0x00A9,
 "WM_DDE_LAST            ", 0x00AF,
 "WM_QUERYCONVERTPOS     ", 0x00b0,
} ;

static int _debug_logmessage(QMSG far *q)
{
   int i;
   char *p = buf;
   char tmpbuf[200];

   sprintf(buf, "%04Xh", q->msg);
   for (i = 0; i<sizeof(msgs)/sizeof(*msgs); i++)
   {
      if (q->msg == msgs[i].msg)
      {
         p = msgs[i].name;
         break;
      }
   }
   sprintf(tmpbuf,"hwnd=%08lXh,msg=%-23s,mp1=%08lXh,mp2=%08lXh\n",
      q->hwnd,
      p,
      q->mp1,
      q->mp2);
   _debug_puts(tmpbuf);
/*    fputs(tmpbuf,stdout); */
}

int argoption(char letter)
{
   char c,d;
   int j;
   for (j=1; j<arg_c; j++)
   {
      c = arg_v[j][0]; d = toupper(arg_v[j][1]);
      if (c == '/' || c == '-')
      {
         if (d == toupper(letter))
            return(j);
      }
   }
   return(0);
}

restore()
{
   if (fp) 
   {
      fclose(fp); fp = NULL;
   }
   if (hq)
   {
      DosCloseQueue(hq); hq = NULL;
   }
   if (hwndframe)
      WinDestroyWindow(hwndframe);
}

open_log(char *filename)
{
   int l,i;
   char *p;


   i = 0;
   while (1)
   {
      fp = fopen(filename,"a");
      if (!fp)
      {
         if (i == 0)
            error_message(1,"ERROR", "File %s cannot be opened\n"
                  "Default (%s) is chosen.", filename,LOG);
         else
         {
            error_message(1,"ERROR", "File %s cannot be opened\n"
                  "Disk full or other error.", filename);
            exit(1);
         }
      }
      else
         break;
      strcpy(filename,LOG);
      i++;
   }
}

close_log()
{
   fclose(fp);
   fp = NULL;
}

logmenuonoff()
{
   menuchange(NULL, 'L', fp    ? 0x101 : 0x100, NULL);
   menuchange(NULL, 'F', flush ? 0x101 : 0x100, NULL);
   if (fp)
      tasklist("%s : %s",title,filename);
   else
      tasklist(title);
}

initall()
{
   if (*filename)
      open_log(filename);
   logmenuonoff();
}

keyevent(int i)
{
   switch (tolower(i&0xFF))
   {
      case '?':
      case FKEY+1:
         help();
         break;

      case 0xFF:
      case 3:
      case FKEY+3:
         exit(0);
   /*    case 'l': */
   /*       fonttype = 1; */
   /*       goto small; */
   /*    case 's': */
   /*       fonttype = 0; */
   /*    small: */
      case 'c':
         setup_screenbuf();
         resize();
         WinInvalidateRect(hwndclient, NULL, 0);
         break;

      case 'd':
         DosEnterCritSec();
         if (fp)
            close_log();
         remove(filename);
         DosExitCritSec();
         logmenuonoff();
         break;

      case 'l':
         DosEnterCritSec();
         if (fp)
            close_log();
         else
            open_log(filename);
         DosExitCritSec();
         logmenuonoff();
         break;

      case 'f':
         DosEnterCritSec();
         flush ^= 1;
         if (flush && fp)
            fflush(fp);
         DosExitCritSec();
         logmenuonoff();
         break;
 
#if 0
      case FKEY+2:
      if (argoption('d'))
         debug_logmessage( hwnd, msg, mp1, mp2, "main loop:");
#endif
      break;
   }
   return 0;
}

main(int argc, char **argv)
{
   int i;

   arg_c = argc;
   arg_v = argv;
   if (debug = argoption('d'))
      ;

   if (i = argoption('f'))
   {
      strcpy(filename, arg_v[i]+2);       /* -ffilename  */
                                          /*    or       */
      if (!*filename && arg_c > i+1 && !strchr("/-",*arg_v[i+1]))
         strcpy(filename, arg_v[i+1]);    /* -f filename */
   }
   else
      strcpy(filename,LOG);
   fonttype = argoption('1') ? 0 : 1;
   winitall();
}
