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


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>

#ifdef MP3_SUPPORT
#include <mpg123.h>
#endif

#include "sndqueue.h"

enum {
    DL_FILLING,
    DL_FILLING_BUSY,
    DL_DRAINING,
    DL_END_OF_LIST
};

void shortsleep(void)
{
    /* sleep 100 ms */
    struct timespec delay = {0, 100000000};
    nanosleep(&delay, NULL);
}
void snd_reset_codec(struct despotify_session* ds) {
	DSFYDEBUG("Resetting audio codec\n");
	if ( ds->vf ) {
		ov_clear(ds->vf);
		DSFYfree(ds->vf);
	} else if ( ds->mf ) {
#ifdef MP3_SUPPORT
		mpg123_close(ds->mf);
		mpg123_delete(ds->mf);
#endif
		ds->mf = NULL;
	}
}


/* Reset for new song */
void snd_reset(struct despotify_session* ds)
{
	DSFYDEBUG("Setting state to DL_DRAINING\n");
	ds->fifo->totbytes = 0;
	ds->dlstate = DL_DRAINING;
	snd_reset_codec(ds);
}

/* Initialize sound session, called once */
bool snd_init(struct despotify_session *ds)
{
	DSFYDEBUG ("Initializing sound FIFO etc (happens once)\n");

	DSFYDEBUG("Setting state to DL_FILLING\n");
	ds->dlstate = DL_FILLING;

	/* This is the fifo that will hold fragments of compressed audio */
        ds->fifo = calloc(1, sizeof(struct snd_fifo));
        if (!ds->fifo)
		return false;
        ds->fifo->maxbytes = 1024 * 1024; /* 1 MB default buffer size */
        ds->fifo->watermark = 200 * 1024; /* 200 KB default watermark */

        if (pthread_mutex_init (&ds->fifo->lock, NULL)) {
            DSFYfree (ds->fifo);
            return NULL;
	}

	if (pthread_cond_init (&ds->fifo->cs, NULL)) {
            DSFYfree (ds->fifo);
		pthread_mutex_destroy (&ds->fifo->lock);
		return NULL;
	}

        return true;
}

/* Destroy a sound session */
void snd_destroy (struct despotify_session* ds)
{
	DSFYDEBUG ("Destroying sound FIFO etc\n");

	/* Free the fifo */
	if (ds->fifo) {
		pthread_mutex_lock(&ds->fifo->lock);

		/* free buffers */
		while (ds->fifo->start) {
                        struct snd_buffer* b = ds->fifo->start;
			ds->fifo->start = ds->fifo->start->next;
			free (b->ptr);
			free (b);
		}

                pthread_mutex_unlock(&ds->fifo->lock);
		pthread_cond_destroy(&ds->fifo->cs);
		pthread_mutex_destroy(&ds->fifo->lock);
                
		DSFYfree (ds->fifo);
	}
}

void snd_fill_fifo(struct despotify_session* ds)
{
    if (ds->dlabort) {
        while (ds->dlstate == DL_FILLING_BUSY) {
            DSFYDEBUG("dlstate = %d. waiting...\n", ds->dlstate);
            shortsleep();
        }
        ds->dlstate = DL_DRAINING;
        return;
    }

    switch (ds->dlstate) {
        case DL_DRAINING:
            if (ds->fifo->totbytes < ds->fifo->watermark ) {
                DSFYDEBUG("Low on data (%d / %d), fetching another channel\n",
                          ds->fifo->totbytes, ds->fifo->maxbytes);
                DSFYDEBUG("dlstate = DL_FILLING_BUSY\n");
                ds->dlstate = DL_FILLING_BUSY;
                despotify_snd_read_stream(ds);
            }
            break;

        case DL_FILLING:
            if (ds->fifo->totbytes < (ds->fifo->maxbytes - SUBSTREAM_SIZE)) {
                DSFYDEBUG("dlstate = DL_FILLING_BUSY\n");
                ds->dlstate = DL_FILLING_BUSY;
                despotify_snd_read_stream(ds);
            }
            else {
                DSFYDEBUG("buffer filled. setting dlstate DL_DRAINING\n");
                ds->dlstate = DL_DRAINING;
            }
            break;
    }
}
            
/* This function stops the player thread */
int snd_stop (struct despotify_session *ds)
{
	int ret = 0;

	DSFYDEBUG ("Entering with arg %p. dl state is %d\n", ds, ds->dlstate);

        if (ds->dlstate < DL_DRAINING)
            ds->dlabort = true;
            
        while (ds->dlstate == DL_FILLING_BUSY) {
            DSFYDEBUG("dlstate = %d. waiting...\n", ds->dlstate);
            shortsleep();
        }
        
        /* Don't request more data in pcm_read(),
           even if the buffer gets low */
        DSFYDEBUG("dlstate = DL_DRAINING\n");
        ds->dlstate = DL_DRAINING;

        pthread_mutex_lock (&ds->fifo->lock);

	/* free the ogg fifo */
	while (ds->fifo->start) {
		struct snd_buffer* b = ds->fifo->start;
		ds->fifo->start = ds->fifo->start->next;
		free(b->ptr);
		free(b);
	}

	ds->fifo->start = NULL;
	ds->fifo->end = NULL;

	/* Reset the session */
	snd_reset(ds);

        ds->dlabort = false;
        pthread_mutex_unlock (&ds->fifo->lock);

	return ret;
}

int snd_next(struct despotify_session *ds)
{
    pthread_mutex_lock(&ds->fifo->lock);

    /* go through fifo and look for next track */
    struct snd_buffer* b;
    struct snd_buffer* next;
    for (b = ds->fifo->start; b; b = next) {
        if (b->cmd == SND_CMD_START)
            break;

        if (b->ptr)
            free(b->ptr);
        ds->fifo->totbytes -= b->length;
        next = b->next;
        free(b);
    }

    ds->fifo->start = b;

    if (!b) {
        /* we didn't have next track in memory */
        ds->fifo->end = NULL;
        pthread_mutex_unlock(&ds->fifo->lock);
        return 0;
    }
    
    pthread_mutex_unlock(&ds->fifo->lock);

    /* notify client */
    if (ds->client_callback)
        ds->client_callback(ds, DESPOTIFY_NEW_TRACK,
                            b->ptr,
                            ds->client_callback_data);

    /* tell decoder to start over */
    snd_reset_codec(ds);

    return 1;
}

void snd_ioctl (struct despotify_session* ds, int cmd, void *data, int length)
{
        switch (cmd) {
            case SND_CMD_CHANNEL_END:
                /* end of substream */
                if (ds->dlabort) {
                    DSFYDEBUG("ds->dlstate = DL_DRAINING\n");
                    ds->dlstate = DL_DRAINING;
                }
                else
                    if (ds->dlstate != DL_END_OF_LIST) {
                        DSFYDEBUG("ds->dlstate = DL_FILLING\n");
                        ds->dlstate = DL_FILLING; /* step down from DL_FILLING_BUSY */
                    }
                return;

            case SND_CMD_END:
                /* end of track. end of playlist? */
                if (!ds->track) {
                    DSFYDEBUG("ds->dlstate = DL_END_OF_LIST\n");
                    ds->dlstate = DL_END_OF_LIST;
                }
                break;
        }

        if (ds->dlabort) {
            if (data)
                free(data);
            return;
        }

        struct snd_buffer* buff = malloc(sizeof(struct snd_buffer));
	if (!buff) {
		perror ("malloc failed");
		exit (-1);
	}

        buff->length = length;
	buff->cmd = cmd;
        buff->consumed = 0;
        buff->ptr = data;
	buff->next = NULL;

        pthread_mutex_lock (&ds->fifo->lock);

	DSFYDEBUG_SNDQUEUE("Current FIFO totbytes=%d, pushed data length is %d\n", ds->fifo->totbytes, length);

	/* Drop the first ogg page if it is Spotify's
           spec-violating single-page same-serial-number stream */
	if (ds->fifo->lastcmd == SND_CMD_START && (buff->ptr[5] == 6)) {
            int offset = 28; /* size of ogg header */

            /* calculate size of page */
            for (int i=0; i < buff->ptr[26]; i++)
                offset += buff->ptr[i+27];

            if (offset < buff->length) {
                memmove(buff->ptr, buff->ptr + offset, length - offset);
                buff->length -= offset;
                DSFYDEBUG("Dropping the first %d bytes of data in this stream, new length is %d\n", offset, buff->length);
            }
            else {
                DSFYDEBUG("Corrupt first OGG packet gave offset %d. Skipping.", offset);
            }
        }

	/* Hook in entry in linked list */
	if (ds->fifo->end)
            ds->fifo->end->next = buff;

	ds->fifo->end = buff;

	/* If this is the first entry */
	if (ds->fifo->start == NULL)
		ds->fifo->start = buff;

	ds->fifo->totbytes += buff->length;

	/* Signal receiver */
	pthread_cond_signal (&ds->fifo->cs);
	pthread_mutex_unlock (&ds->fifo->lock);

        ds->fifo->lastcmd = cmd;
}

int snd_consume_data(struct despotify_session* ds, int req_bytes, void* private, int (*consumer)(void* source, int bytes, void* private, int offset))
{
    int totlength = 0;
    bool loop = true;

    pthread_mutex_lock(&ds->fifo->lock);

    snd_fill_fifo(ds);

    /* process data */
    while (loop) {
        //printf("looping\n");
        /* Check queue status */
        if (ds->fifo->start == NULL) {
            _DSFYDEBUG ("Waiting for data (%d bytes)\n", ds->fifo->totbytes);
            pthread_cond_wait (&ds->fifo->cs, &ds->fifo->lock);
            _DSFYDEBUG ("Got data\n");
        }

        DSFYDEBUG_SNDQUEUE("Processing one buffer at ds->fifo->start."
                           " %zd bytes requested. Totbytes: %d\n",
                           req_bytes, ds->fifo->totbytes );

        struct snd_buffer* b = ds->fifo->start;
        if (!b)
            break;

        _DSFYDEBUG("loop cmd:%d bytes:%d\n", b->cmd, ds->fifo->totbytes);

        switch (b->cmd)
        {
            case SND_CMD_START:
                /* first packet of a track */
		DSFYDEBUG ("Got SND_CMD_START\n");

		/* Increment by one */
                ds->fifo->start = ds->fifo->start->next;

                /* notify client */
                if (ds->client_callback)
                    ds->client_callback(ds, DESPOTIFY_NEW_TRACK,
                                        b->ptr,
                                        ds->client_callback_data);
                /* If this was the last entry */
                if (b == ds->fifo->end)
                    ds->fifo->end = NULL;
                if (b->ptr)
                    DSFYfree (b->ptr);
                DSFYfree (b);
                break;
                
            case SND_CMD_DATA:
            {
                /* data packet */
                int remaining = b->length - b->consumed;
                //int ptrsize = size * nmemb;
                int length;
                
                if (totlength + remaining < req_bytes)
                    length = remaining;	/* The entire buffer will fit */
                else {
                    length = req_bytes - totlength; /* Don't overrun ptrsize */
                }

                consumer(b->ptr+b->consumed, length, private, totlength);

                b->consumed += length;
                totlength += length;
        
                /* If we have used the entire buffer, free it */
                if (b->consumed == b->length) {
                    ds->fifo->start = ds->fifo->start->next;
                    ds->fifo->totbytes -= b->length;

                    /* If this was the last entry */
                    if (b == ds->fifo->end)
			ds->fifo->end = NULL;
                    
                    DSFYfree (b->ptr);
                    DSFYfree (b);
                }

                /* exit if input is empty or output is full */
                if (!ds->fifo->start || totlength == (int)(req_bytes))
                    loop = false;
                break;
            }

            case SND_CMD_END:
                /* last packet of a track, return 0 bytes to signal EOF */
		DSFYDEBUG ("Got SND_CMD_END\n");

                /* if there already are bytes to return,
                   send them first and then come back here empty */
                if (totlength) {
                    loop = false;
                    break;
                }

		/* Increment by one */
		ds->fifo->start = ds->fifo->start->next;

		/* If this was the last entry */
		if (b == ds->fifo->end)
                    ds->fifo->end = NULL;
                
                /* If this was the last entry */
                if (b == ds->fifo->end)
                    ds->fifo->end = NULL;
                if (b->ptr)
                    DSFYfree (b->ptr);
		DSFYfree (b);

		_DSFYDEBUG("Calling despotify_end_of_track\n");

                if (!ds->fifo->start) {
                    /* (snd_stop locks the mutex internally) */
                    pthread_mutex_unlock(&ds->fifo->lock);
                    snd_stop(ds);
                    pthread_mutex_lock(&ds->fifo->lock);

                    if (ds->client_callback)
                        ds->client_callback(ds, DESPOTIFY_END_OF_PLAYLIST,
                                            NULL, ds->client_callback_data);
                }
                /* return 0 bytes as EOF marker to decoder */
                loop = false;
                break;
        }
    }

    pthread_mutex_unlock(&ds->fifo->lock);

    /* Return number of bytes read to ogg-layer */
    DSFYDEBUG("Returning %d bytes. %d left.\n",
               totlength, ds->fifo->totbytes);
    return totlength;
}


int vorbis_consume(void* source, int bytes, void* private, int offset)
{
    memcpy(private+offset,source,bytes);
    return bytes;
}

/*
 * Ogg-Vorbis read() callback
 * Called by both ov_info() and ov_read()
 * 
 * This functions dequeues buffers from the fifo
 *
 */
size_t snd_ov_read_callback(void *ptr, size_t size, size_t nmemb, void* session)
{
    struct despotify_session* ds = session;

    /* TODO: Add function ptr */
    size_t outsize = snd_consume_data(ds,size*nmemb,ptr,vorbis_consume);
    return outsize;
}

int snd_do_vorbis(struct despotify_session* ds, struct pcm_data* pcm ) {
    if (!ds->vf) {
        DSFYDEBUG ("Initializing vorbisfile struct\n");

        /* Allocate Vorbis struct */
        ds->vf = calloc(1, sizeof (OggVorbis_File));
        if (!ds->vf)
            return -1;

        /* Initialize Vorbis struct with the appropriate callbacks */
        ov_callbacks callbacks;
        callbacks.read_func = snd_ov_read_callback;
        callbacks.seek_func = NULL;
        callbacks.close_func = NULL;
        callbacks.tell_func = NULL;
        
        /* Now call ov_open_callbacks(). This will trigger the read_func */
        _DSFYDEBUG("Calling ov_open_callbacks()\n");

        int ret = ov_open_callbacks(ds, ds->vf, NULL, 0, callbacks);
        if (ret) {
            DSFYfree(ds->vf);
            DSFYDEBUG("ov_open_callbacks(): error %d (%s)\n",
                      ret,
                      ret == OV_ENOTVORBIS? "not Vorbis":
                      ret == OV_EBADHEADER? "bad header":
                      "unknown, check <vorbis/codec.h>")
                return ret * 10;
        }
    }
    
    vorbis_info* vi = ov_info(ds->vf, -1);

    pcm->samplerate = vi->rate;
    pcm->channels = vi->channels;

    while (1) {
        /* decode to pcm */
        ssize_t r = ov_read(ds->vf, pcm->buf, sizeof(pcm->buf),
                            SYSTEM_ENDIAN, 2, 1, NULL);

        /* assume no valid data read. */
        pcm->len = 0;

        if (r == OV_HOLE) {
            /* vorbis got garbage */
            DSFYDEBUG ("pcm_read() == OV_HOLE\n");
            continue;
        }
        
        if (r < 0) {
            DSFYDEBUG ("pcm_read() == %zd\n", r);
            return r;
        }

        if (r == 0) {
            /* end of file */
            ov_clear(ds->vf);
            DSFYfree(ds->vf);
            return 0;
        }

        /* valid data *was* read, update length. */
        pcm->len = r;

        if (ds->client_callback) {
            double point = ov_time_tell(ds->vf);
            ds->client_callback(ds, DESPOTIFY_TIME_TELL, &point,
                                ds->client_callback_data);
        }

        /* top up fifo */
        snd_fill_fifo(ds);

        break;
    }
    return 0;
}

