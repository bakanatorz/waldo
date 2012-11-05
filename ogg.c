#include <stdlib.h>
#include <stdio.h>
#include <despotify.h>
#include <unistd.h>
#include <sys/time.h>

#include "sndqueue.h"

typedef struct 
{
    double start;
    size_t curdata;
    bool play;
} callback_data_t;

void print_track_info(const struct track* t)
{
    if(t->has_meta_data)
    {
        printf("Title: %s\nAlbum: %s\nArtist(s): ", t->title, t->album);
        for (struct artist* a = t->artist; a; a = a->next)
        {
            printf("%s%s", a->name, a->next ? ", " : "");
        }
        printf("\nYear: %d\nLength: %02d:%02d\n\n", t->year, t->length / 60000, t->length % 60000 / 1000);
    }
    else
    {
        printf("Track has no metadata\n");
    }
}

void despotify_callback(struct despotify_session* ds, int signal, void* data, void* callback_data)
{
    callback_data_t* cdata = callback_data;
    switch (signal) {
        case DESPOTIFY_NEW_TRACK: {
            struct track* t = data;
            printf("New track: %s / %s (%d:%02d) %d kbit/s\n", t->title, t->artist->name, t->length / 60000, t->length % 60000 / 1000, t->file_bitrate / 1000);
            cdata->curdata = -1;
            break;
        }
    
        case DESPOTIFY_TIME_TELL:
            {
                struct track* t = despotify_get_current_track(ds);
                if (t)
                {
                    size_t prevdata = cdata->curdata;
                    cdata->curdata += *((size_t*)data);
                    size_t totdata = (t->file_bitrate/8)*(t->length/1000.0)/1024;
                    printf("Data (KB): %d/%d (%f%%)", (unsigned int) cdata->curdata/1024, (unsigned int) totdata, ((double)cdata->curdata/1024)/totdata*100);
                    if (prevdata == -1)
                    {
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        cdata->start = tv.tv_sec+((double)tv.tv_usec)*1.0e-6;
                    }
                    else
                    {
                        struct timeval curtv;
                        gettimeofday(&curtv, NULL);
                        double curs = curtv.tv_sec+((double)curtv.tv_usec)*1.0e-6;
                        double diff = curs-cdata->start;
                        double rate = cdata->curdata/1024/diff;
                        double eta = ((int)totdata-cdata->curdata/1024)/rate;
                        int mins = ((int)eta)/60;
                        printf(" Rate: %0.2f KB/s ETA: %d:%05.2f", rate, mins, eta-mins*60.0);
                    }
                    printf("\r");
                    fflush(stdout);
                }
            }
            break;
 
        case DESPOTIFY_END_OF_PLAYLIST:
            printf("\nDownload Complete\n");
            cdata->play = false;
            break;
    }
}

int main(int argc, char** argv)
{
    // Get the auth params from a file
    if(argc < 5)
    {
        printf("Proper usage: test_play auth_filename dest_filename spotify:track:track_uri\n");
        return 1;
    }

    // Set up despotify
    if (!despotify_init())
    {
        printf("despotify_init() failed\n");
        return 1;
    }
    callback_data_t cdata = {0.0, -1, true};
    struct despotify_session* ds = despotify_init_client(despotify_callback, &cdata, true, true);
    if (!ds)
    {
        printf("despotify_init_client() failed\n");
        return 1;
    }

    if (!despotify_authenticate(ds, argv[1], argv[2]))
    {
        printf("Authentication failed\n");
        despotify_exit(ds);
        return 1;
    }

    printf("Authentication successful!\n");

    FILE* fp = fopen(argv[3],"wb");
    char* uri = argv[4]+14;
    char id[33];
    despotify_uri2id(uri,id);
    struct track* t = despotify_get_track(ds, id);
    printf("Playing track %s\n",uri);
    if (!t)
    {
        printf("Invalid track uri:%s id:%s\n", uri, id);
        return 1;
    }

    printf("\nTrack Info:\n");
    print_track_info(t);

    despotify_play(ds, t, false);
    cdata.play = true;
    char buf[4096];
    while (cdata.play)
    {
        snd_fill_fifo(ds);
        size_t outsize = snd_consume_data(ds,sizeof(buf),buf,vorbis_consume);
        if (outsize) {
            ds->client_callback(ds, DESPOTIFY_TIME_TELL, &outsize, ds->client_callback_data);
            fwrite(buf, outsize, 1, fp);
        }
        else {
            break;
        }
    }
    fclose(fp);
    despotify_exit(ds);
    if (!despotify_cleanup())
    {
        printf("despotify_cleanup() failed\n");
        return 1;
    }
    return 0;
}
