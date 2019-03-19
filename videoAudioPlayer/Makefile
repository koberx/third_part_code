CC = gcc
CFLAGS = -I/usr/local/include
#CFLAGS += -DUSE_OUTPUT_IMAGE
LIBS =  -Wno-deprecated-declarations -lpthread -lX11 -L/usr/local/lib -lavutil -lswscale -lavcodec -lavformat -lswresample -lasound `pkg-config --cflags x11` `pkg-config --cflags xext`
softDecode : media.c  player.c threadPool.c
	    $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	    $(RM) softDecode .*.sw?
