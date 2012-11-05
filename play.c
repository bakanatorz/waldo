#include <stdlib.h>
#include <stdio.h>
#include <despotify.h>
#include <unistd.h>

#include "audio.h"

static bool play = false;
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

void callback(struct despotify_session* ds, int signal, void* data, void* callback_data)
{
    static int seconds = -1;
    (void)ds; (void)callback_data; /* don't warn about unused parameters */
    
    switch (signal) {
        case DESPOTIFY_NEW_TRACK: {
            struct track* t = data;
            printf("New track: %s / %s (%d:%02d) %d kbit/s\n", t->title, t->artist->name, t->length / 60000, t->length % 60000 / 1000, t->file_bitrate / 1000);
            break;
        }
    
        case DESPOTIFY_TIME_TELL:
            if ((int)(*((double*)data)) != seconds) {
                seconds = *((double*)data);
                printf("Time: %d:%02d\r", seconds / 60, seconds % 60);
                fflush(stdout);
            }
            break;
 
        case DESPOTIFY_END_OF_PLAYLIST:
            printf("Track over\n");
            play = false;
            break;
    }
}

int main(int argc, char** argv)
{
    // Get the auth params from a file
    if(argc < 4)
    {
        printf("Proper usage: test_play username password spotify:track:track_uri\n");
        return 1;
    }

    // Set up despotify
    if (!despotify_init())
    {
        printf("despotify_init() failed\n");
        return 1;
    }

    struct despotify_session* ds = despotify_init_client(callback, NULL, true, true);
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

    char* uri = argv[3]+14;
    char id[33];
    despotify_uri2id(uri,id);
    void* audio_device = audio_init();
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
    struct pcm_data pcm;
    play = true;
    while (play)
    {
        int rc = despotify_get_pcm(ds, &pcm);
        if (rc == 0)
        {
            audio_play_pcm(audio_device, &pcm);
        }
        else
        {
            printf("despotify_get_pcm() failed\n");
            return 1;
        }
    }


    audio_exit(audio_device);
    despotify_exit(ds);
    if (!despotify_cleanup())
    {
        printf("despotify_cleanup() failed\n");
        return 1;
    }
    return 0;
}
