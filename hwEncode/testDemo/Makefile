CC = gcc
CFLAGS = -I /root/usr/install/include
LIBS =  -lva-x11 -lva -lpthread -lX11 -lavutil -lswscale
encodeDemo : encodeTest.c va_display.c va_display_x11.c
	    $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	    $(RM) encodeDemo .*.sw?
