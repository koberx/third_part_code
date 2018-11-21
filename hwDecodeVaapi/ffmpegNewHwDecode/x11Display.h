
#ifndef _X11DISPLAY_H_
#define _X11DISPLAY_H_

#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>    
#include <unistd.h>    
#include <X11/Xutil.h>
#include <X11/Xatom.h>

typedef struct {
    Display* display; 
    int screen_num;
    Window win;
    unsigned int display_width;
    unsigned int display_height;    
    char *display_name;
    GC gc;
}display_t;

extern int initX11(display_t *show);
void deinitX11(display_t *show);

#endif
