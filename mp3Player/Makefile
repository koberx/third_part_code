CC = gcc
#CFLAGS = -Wall -g -O0 -I ./usr/include/  -lpthread  -L ./usr/lib/ -lasound -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale -lm -lz  -lSDL -lpthread
CFLAGS =  -I ./usr/include/  -lpthread
CFLAGS += -Wno-deprecated-declarations
LIBS   = -lpthread  -L ./usr/lib/ -lasound -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lswresample -lswscale -lm -lz

player : audioPlayer.c
	    $(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	    $(RM) player .*.sw?
