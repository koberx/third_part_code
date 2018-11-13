CFLAGS= -DUSE_FFMPEG_VAAPI -DHAVE_SWSCALE `pkg-config --cflags x11` `pkg-config --cflags xext`
LDFLAGS=-shared `pkg-config --libs x11` `pkg-config --libs xext` -lavcodec -lavformat -lswscale -lavutil -lva -lva-x11
PLAYER_LDFLAGS=`pkg-config --libs x11` `pkg-config --libs xext` -lavcodec -lavformat -lswscale -lavutil -lva -lva-x11

ifeq ($(shell getconf LONG_BIT), 32)
PLATFORM = 32
else
PLATFORM = 64
endif

ifeq ($(PLATFORM), 32)
CFLAGS += -D __CM_BOX_PLATFORM_32
OUTPUT_DIR = ../lib32
else
PLATFORM = 64
CFLAGS += -fPIC -D __CM_BOX_PLATFORM_64
OUTPUT_DIR = ../lib64
endif

SOURCE_heads=ffmpeg.h image.h utils.h debug.h image.h sysdeps.h vaapi.h
SOURCE_srcs= debug.c image.c utils.c ffmpeg.c vaapi.c

OBJS=$(patsubst %.c,%.o,$(SOURCE_srcs))

TARGET_LIB=libvaapi.so
TARGET_EXE=ffmpeg_player

CC=gcc -g
LD=ld

all:$(OBJS) ffmpeg_player
	@echo "==== Make $(PLATFORM)bit VAAPI-Lib ===="
	$(LD) $(LDFLAGS) -o $(TARGET_LIB) $(OBJS)

%.o:%.c
	$(CC) -c $(CFLAGS) $< -o $*.o

ffmpeg_player:ffmpeg_player.c
	$(CC) -o $@ $^ $(CFLAGS) $(PLAYER_LDFLAGS) -L ./ -lvaapi


clean:
	rm -rf $(TARGET_LIB) $(TARGET_EXE) *.o
