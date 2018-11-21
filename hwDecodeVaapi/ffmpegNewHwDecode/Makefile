CC = gcc
LIBS =  -Wno-deprecated-declarations -lva-x11 -lva -lpthread -lX11 -lavutil -lswscale -lavcodec -lavformat `pkg-config --cflags x11` `pkg-config --cflags xext`
hw_decode : hw_decode.c x11Display.c
	    $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	    $(RM) hw_decode .*.sw?
