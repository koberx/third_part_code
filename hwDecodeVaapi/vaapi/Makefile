CFLAGS= -DUSE_FFMPEG_VAAPI -Wno-deprecated-declarations  
CFLAGS += -DUSE_OUTPUT_IMAGE
CC = gcc -g
LIBS =  -lva-x11 -lva -lpthread -lX11 -lavutil -lswscale -lavcodec -lavformat `pkg-config --cflags x11` `pkg-config --cflags xext`
ffmpeg_player : debug.c  ffmpeg.c  ffmpeg_player.c  image.c  utils.c  vaapi.c 
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)
clean:
	$(RM) ffmpeg_player .*.sw?
