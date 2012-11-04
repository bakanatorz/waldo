/*
 *$Id: LICENSE 3 2009-02-24 21:58:43Z despotify $

 *Copyright (c) 2008-2009, #HACK.SE
 *All rights reserved.
 *
 *Redistribution and use in source and binary forms, with or without
 *modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *THIS SOFTWARE IS PROVIDED BY THE AUTHORS ''AS IS'' AND ANY
 *EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 *WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *DISCLAIMED. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
 *DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef DESPOTIFY_SNDQUEUE_H
#define DESPOTIFY_SNDQUEUE_H

#include <pthread.h>
#include <vorbis/vorbisfile.h>

#include "despotify.h"

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

#define DSFYfree(p) do { free(p); (p) = NULL; } while (0)
#ifdef DEBUG
#define DSFYDEBUG(...) { FILE *fd = fopen("/tmp/despotify.log","at"); fprintf(fd, "%s:%d %s() ", __FILE__, __LINE__, __func__); fprintf(fd, __VA_ARGS__); fclose(fd); }
#else
#define DSFYDEBUG(...)
#endif

#define _DSFYDEBUG(...)

#ifdef DEBUG_SNDQUEUE
#define DSFYDEBUG_SNDQUEUE(...) DSFYDEBUG(__VA_ARGS__)
#else
#define DSFYDEBUG_SNDQUEUE(...)
#endif

enum
{
    SND_CMD_START,
    SND_CMD_DATA,
    SND_CMD_END,
    SND_CMD_CHANNEL_END
};

typedef int (*audio_request_callback) (void *);
typedef void (*time_tell_callback) (struct despotify_session *, int cur_time);

void snd_reset (struct despotify_session* ds);
bool snd_init (struct despotify_session* ds);
void snd_destroy (struct despotify_session *);
void snd_set_data_callback (struct despotify_session *, audio_request_callback, void *);
void snd_set_end_callback (struct despotify_session* ds,
			   audio_request_callback callback, void *arg);
void snd_set_timetell_callback (struct despotify_session* ds,
                                time_tell_callback callback);

int snd_stop (struct despotify_session* ds);
int snd_next (struct despotify_session *ds);
void snd_start (struct despotify_session* ds);
void snd_ioctl (struct despotify_session *session, int cmd, void *data, int length);
long pcm_read (struct despotify_session* ds, char *buffer, int length,
               int bigendianp, int word, int sgned, int *bitstream);

void snd_mark_dlding (struct despotify_session* ds);
void snd_mark_idle (struct despotify_session* ds);
void snd_mark_end (struct despotify_session* ds);

size_t snd_ov_read_callback(void *ptr, size_t size, size_t nmemb, void* ds);
long snd_pcm_read(struct despotify_session* ds,
                  char *buffer, int length, int bigendianp,
                  int word, int sgned, int *bitstream);
int snd_get_pcm(struct despotify_session*, struct pcm_data*);
#endif
