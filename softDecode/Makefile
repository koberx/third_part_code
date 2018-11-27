CC = gcc
CFLAGS = -I/usr/local/include
#CFLAGS += -DUSE_OUTPUT_IMAGE
LIBS =  -Wno-deprecated-declarations -lva-x11 -lva -lpthread -lX11 -lavutil -lswscale -lavcodec -lavformat `pkg-config --cflags x11` `pkg-config --cflags xext`
softDecode : softDecode.c
	    $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	    $(RM) softDecode .*.sw?
