#include "x11Display.h"

Window create_simple_window(Display* display, int width, int height, int x, int y)
{
 
  int screen_num = DefaultScreen(display);
  int win_border_width = 2;
  Window win;
 
  win = XCreateSimpleWindow(display, RootWindow(display, screen_num),
                            x, y, width, height, win_border_width,
                            BlackPixel(display, screen_num),
                            WhitePixel(display, screen_num));
 
  /* make the window actually appear on the screen. */
  XMapWindow(display, win);
  /* flush all pending requests to the X server. */
 
  XFlush(display);
 
  return win;
 
}

GC create_gc(Display* display, Window win, int reverse_video)
{
    GC gc;                /* handle of newly created GC.  */
    unsigned long valuemask = 0;      /* which values in 'values' to  */
                      /* check when creating the GC.  */
    XGCValues values;         /* initial values for the GC.   */
    unsigned int line_width = 2;      /* line width for the GC.       */
    int line_style = LineSolid;       /* style for lines drawing and  */
    int cap_style = CapButt;      /* style of the line's edje and */
    int join_style = JoinBevel;       /*  joined lines.       */
    int screen_num = DefaultScreen(display);
    gc = XCreateGC(display, win, valuemask, &values);
    if (gc < 0) {
        fprintf(stderr, "XCreateGC: \n");
    }
    /* allocate foreground and background colors for this GC. */
    if (reverse_video) {
        XSetForeground(display, gc, WhitePixel(display, screen_num));
        XSetBackground(display, gc, BlackPixel(display, screen_num));
    }
    else {
        XSetForeground(display, gc, BlackPixel(display, screen_num));
        XSetBackground(display, gc, WhitePixel(display, screen_num));
    }
    XSetLineAttributes(display, gc, line_width, line_style, cap_style, join_style);
    XSetFillStyle(display, gc, FillSolid);
    return gc;
}

int initX11(display_t *show)
{

    show->display_name = getenv("DISPLAY");
    show->display = XOpenDisplay(show->display_name);
    if (show->display == NULL) {
        fprintf(stderr, "cannot connect to X server '%s'\n", show->display_name);
        return -1;
    }

    show->screen_num = DefaultScreen(show->display);
    show->display_width = DisplayWidth(show->display, show->screen_num);
    show->display_height = DisplayHeight(show->display, show->screen_num);

    show->win = create_simple_window(show->display, show->display_width, show->display_height, 0, 0); 
    show->gc = create_gc(show->display, show->win, 0);
    XSync(show->display, False);

    return 0;
    
}

void deinitX11(display_t *show)
{
    XFlush(show->display);
    XSync(show->display, False);
    XCloseDisplay(show->display);
}
