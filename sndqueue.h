#ifndef __SND_QUEUE_H__
#define __SND_QUEUE_H__

#include <vorbis/vorbisfile.h>

#include "despotify.h"
#include "util.h"

#if defined(__linux__) 
	#include <endian.h>
#endif
#if defined(__FreeBSD__)
	#include <machine/endian.h>
	#define __BYTE_ORDER _BYTE_ORDER
	#define __LITTLE_ENDIAN _LITTLE_ENDIAN
#endif


#if defined(__BYTE_ORDER)
	#if __BYTE_ORDER == __LITTLE_ENDIAN
		#define SYSTEM_ENDIAN 0
	#else
		#define SYSTEM_ENDIAN 1
	#endif
#else
	#warning "Unknown endian - Assuming little endian"
	#define SYSTEM_ENDIAN 0
#endif


/* Default buffer treshold value is 80 % */
#define BUFFER_THRESHOLD 80;

enum
{
    SND_CMD_START,
    SND_CMD_DATA,
    SND_CMD_END,
    SND_CMD_CHANNEL_END
};

typedef int (*audio_request_callback) (void *);
typedef void (*time_tell_callback) (struct despotify_session *, int cur_time);

void initfp(FILE* _fp);
int snd_get_pcm(struct despotify_session*, struct pcm_data*);


#endif //__SND_QUEUE_H__
