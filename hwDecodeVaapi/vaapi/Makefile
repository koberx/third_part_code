CFLAGS= -DUSE_FFMPEG_VAAPI -DHAVE_SWSCALE -Wno-deprecated-declarations `pkg-config --cflags x11` `pkg-config --cflags xext` -fPIC
#CFLAGS += -DUSE_OUTPUT_IMAGE
LDFLAGS=-shared `pkg-config --libs x11` `pkg-config --libs xext` -lavcodec -lavformat -lswscale -lavutil -lva -lva-x11
PLAYER_LDFLAGS=`pkg-config --libs x11` `pkg-config --libs xext` -lavcodec -lavformat -lswscale -lavutil -lva -lva-x11


SOURCE_heads=ffmpeg.h image.h utils.h debug.h image.h sysdeps.h vaapi.h
SOURCE_srcs= debug.c image.c utils.c ffmpeg.c vaapi.c

OBJS=$(patsubst %.c,%.o,$(SOURCE_srcs))

TARGET_LIB=libvaapi.so
TARGET_EXE=ffmpeg_player

CC=gcc -g
LD=ld

all:$(OBJS) 
	@echo "==== Make $(PLATFORM)bit VAAPI-Lib ===="
	$(LD) $(LDFLAGS) -o $(TARGET_LIB) $(OBJS)

%.o:%.c
	$(CC) -c $(CFLAGS) $< -o $*.o

ffmpeg_player:ffmpeg_player.c
	$(CC) -o $@ $^ $(CFLAGS) $(PLAYER_LDFLAGS) -L ./ -lvaapi


clean:
	rm -rf $(TARGET_LIB) $(TARGET_EXE) *.o
