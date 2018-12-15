#include "media.h"
int main(int argc,char *argv[])
{
	int ret;
    avcodec_register_all();
    av_register_all();
	
	player_t    * mediaContext = (player_t *)malloc(sizeof(player_t));
    if (mediaContext == NULL) {
        printf("[rxhu] player_t malloc failed!\n");
        return -1;
    }

	//init the list
    INIT_LIST_HEAD(&mediaContext->videoStream.videoPacketQueue);
    INIT_LIST_HEAD(&mediaContext->videoStream.videoFrameQueue);
    mediaContext->videoStream.packetSize = 0;
    mediaContext->videoStream.frameSize = 0;
    mediaContext->decThreadExit = false;
    mediaContext->readThreadExit = false;

	ret = initDisplay(mediaContext);  
	if (ret < 0) {
		printf("[rxhu] initDisplay failed \n");
		return -1;
	}
	
   	ret = openInput(mediaContext, argv[1]);
	if (ret < 0) {
		printf("[rxhu] open the media file failed!\n");
		return -1;
	}
	displayResize(mediaContext);
	mediaContext->thpool = thpool_init(4);
	thpool_add_work(mediaContext->thpool, localPacketReadThread, mediaContext);
	thpool_add_work(mediaContext->thpool, localVideoPacketDecodeThread, mediaContext);
	thpool_add_work(mediaContext->thpool, localVideoFrameShowThread, mediaContext);	
	thpool_wait(mediaContext->thpool);
    thpool_destroy(mediaContext->thpool);
	mediaDeinit(mediaContext);
	free(mediaContext);

    return 0;
}




