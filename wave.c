#include <stdlib.h>
#include <stdio.h>
#include <despotify.h>
#include <unistd.h>
#include <sys/time.h>

static bool play = false;

typedef struct chunk_t
{
    struct pcm_data pcm;
    struct chunk_t* next;
} chunk_t;

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
    static double starts = 0;
    (void)callback_data; /* don't warn about unused parameters */
    
    switch (signal) {
        case DESPOTIFY_NEW_TRACK: {
            struct track* t = data;
            printf("New track: %s / %s (%d:%02d) %d kbit/s\n", t->title, t->artist->name, t->length / 60000, t->length % 60000 / 1000, t->file_bitrate / 1000);
            seconds = -1;
            break;
        }
    
        case DESPOTIFY_TIME_TELL:
            if ((int)(*((double*)data)) != seconds) {
                struct track* t = despotify_get_current_track(ds);
                if (t)
                {
                    int prevseconds = seconds;
                    seconds = *((double*)data);
                    int trackseconds = t->length/1000;
                    printf("Time: %d:%02d/%d:%02d (%f%%)", seconds / 60, seconds % 60, trackseconds / 60, trackseconds % 60, ((double)seconds)/trackseconds*100);
                    if (prevseconds == -1)
                    {
                        struct timeval tv;
                        gettimeofday(&tv, NULL);
                        starts = tv.tv_sec+((double)tv.tv_usec)*1.0e-6;
                    }
                    else
                    {
                        struct timeval curtv;
                        gettimeofday(&curtv, NULL);
                        double curs = curtv.tv_sec+((double)curtv.tv_usec)*1.0e-6;
                        double diff = curs-starts;
                        double rate = seconds/diff;
                        double eta = (trackseconds-seconds)/rate;
                        int mins = ((int)eta)/60;
                        printf(" ETA: %d:%05.2f", mins, eta-mins*60);
                    }
                    printf("\r");
                    fflush(stdout);
                }
            }
            break;
 
        case DESPOTIFY_END_OF_PLAYLIST:
            printf("\nDownload Complete\n");
            play = false;
            break;
}
}

int main(int argc, char** argv)
{
    // Get the auth params from a file
    if(argc < 5)
    {
        printf("Proper usage: test_play username password dest_filename spotify:track:track_uri\n");
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
    chunk_t* chunk = calloc(1, sizeof(chunk_t));
    chunk_t* headchunk = chunk;
    play = true;
    while (play)
    {
        int rc = despotify_get_pcm(ds, &chunk->pcm);
        if (rc == 0)
        {
            chunk->next = calloc(1, sizeof(chunk_t));
            chunk = chunk->next;
        }
        else
        {
            printf("despotify_get_pcm() failed\n");
            return 1;
        }
    }

    int len = 0;
    chunk = headchunk;
    while(chunk)
    {
        len += chunk->pcm.len;
        chunk = chunk->next;
    }

    FILE* fp = fopen(argv[3],"wb");
    const int16_t NumChannels = 2;
    const int16_t BitsPerSample = 16;
    const int32_t Subchunk2Size = len * NumChannels * BitsPerSample/8;
    const int32_t SampleRate = 44100;

    const int32_t ChunkSize = 36 + Subchunk2Size;
    const int32_t SubChunk1Size = 16;
    const int16_t AudioFormat = 1;
    const int32_t ByteRate = SampleRate * NumChannels * BitsPerSample/8;
    const int16_t BlockAlign = NumChannels * BitsPerSample/8;


    fwrite("RIFF", 1, 4, fp);
    fwrite(&ChunkSize, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    fwrite(&SubChunk1Size, 1, 4, fp);
    fwrite(&AudioFormat, 2, 1, fp);
    fwrite(&NumChannels, 2, 1, fp);
    fwrite(&SampleRate, 4, 1, fp);
    fwrite(&ByteRate, 4, 1, fp);
    fwrite(&BlockAlign, 2, 1, fp);
    fwrite(&BitsPerSample, 2, 1, fp);
    fwrite("data", 1, 4, fp);
    fwrite(&Subchunk2Size, 4, 1, fp);
    chunk = headchunk;
    while(chunk)
    {
        fwrite(chunk->pcm.buf, 1, chunk->pcm.len, fp);
        chunk = chunk->next;
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
