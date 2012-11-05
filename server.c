#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include <despotify.h>

#include "mongoose.h"
#include "sndqueue.h"

bool play;

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
        printf("fid: %s tid: %s\n", t->file_id, t->track_id);
    }
    else
    {
        printf("Track has no metadata\n");
    }
}

void despotify_callback(struct despotify_session* ds, int signal, void* data, void* callback_data)
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

static void *mongoose_callback(enum mg_event event, struct mg_connection* connection)
{
    const struct mg_request_info* request_info = mg_get_request_info(connection);
    if (event == MG_NEW_REQUEST)
    {
        printf("New Request: %s\n", request_info->uri);
        struct despotify_session* ds = despotify_init_client(despotify_callback, NULL, true, true);
        if (!ds)
        {
            printf("despotify_init_client() failed\n");
            return "";
        }

        char username[1024], password[1024], uri[1024];

        const char* c = request_info->uri+1;
        char* d = username;
        int var = 0;
        while(*c)
        {
            if (*c == '/')
            {
                *d = '\0';
                if (var == 0)
                {
                    d = password;
                    var++;
                }
                else if (var == 1)
                {
                    d = uri;
                    var++;
                }
                else
                {
                    break;
                }
            }
            else
            {
                *(d++) = *c;
            }
            c++;
        }
        uri[22] = '\0';
        printf("username: %s\npassword: %s\nuri: %s\n",username, password, uri);

        if (despotify_authenticate(ds, username, password))
        {
            printf("auth'd\n");
            char id[33];
            despotify_uri2id(uri,id);
            printf("%d\n",strlen(id));
            struct track* t = despotify_get_track(ds, id);
            printf("Serving track %s\n",id);
            if (!t)
            {
                printf("Invalid track uri:%s id:%s\n", uri, id);
                mg_printf(connection, "HTTP/1.0 200 OK\r\n"
                "Content-Type: text/plain\r\n\r\n"
                "Invalid track uri:%s id:%s", uri, id);
            }
            else
            {

                printf("\nTrack Info:\n");
                print_track_info(t);
                char filename[27];
                strcpy(filename, uri);
                strcpy(filename+22, ".ogg");
                FILE* file;
                printf("starting up\n");
                print_track_info(t);
                if (file = fopen(filename, "rb"))
                {
                    fclose(file);
                    printf("found cache\n");
                }
                else
                {
                    despotify_play(ds, t, false);
                    file = fopen(filename, "wb");
                    play = true;
                    char buf[4096];
                    while (play)
                    {
                        snd_fill_fifo(ds);
                        size_t outsize = snd_consume_data(ds,sizeof(buf),buf,vorbis_consume);
                        if (outsize) {
                            fwrite(buf, outsize, 1, file);
                        }
                        else {
                            break;
                        }
                    }
                    fclose(file);
                    despotify_stop(ds);
                }
                printf("Sending data\n");
                mg_send_file(connection, filename);
                printf("Data sent\n");
            }
        }
        else
        {
            printf("Authorization Failed :(\n\n");
            mg_printf(connection, "HTTP/1.0 200 OK\r\n"
            "Content-Type: text/plain\r\n\r\n"
            "%s\r\n%s\r\nAuthorization Failed :(", username, password);
        }

        despotify_exit(ds);
        return "";
    }
    else
    {
        return NULL;
    }
}

int main(int argc, char** argv)
{
    // Set up Mongoose options
    struct mg_context* context;
    const char* options[] = {"listening_ports", "8080", NULL};
    char** c = options;
    printf("Options:\n");
    while (*c)
    {
        printf("%s %s\n",*c,*(c+1));
        c+=2;
    }

    // Init spotify
    if (!despotify_init())
    {
        printf("despotify_init() failed\n");
        return 1;
    }

    // Run Mongoose
    context = mg_start(&mongoose_callback, NULL, options);
    //getchar();
    for(;;)
        sleep(1);

    mg_stop(context);
    if (!despotify_cleanup())
    {
        printf("despotify_cleanup() failed\n");
        return 1;
    }
    return 0;
}
