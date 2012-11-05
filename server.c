#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include <despotify.h>

#include "mongoose.h"
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

static void *mongoose_callback(enum mg_event event, struct mg_connection* connection)
{
    const struct mg_request_info* request_info = mg_get_request_info(connection);
    if (event == MG_NEW_REQUEST)
    {
        printf("New Request: %s\n", request_info->uri);
        callback_data_t cdata = {0.0, -1, true};
        struct despotify_session* ds = despotify_init_client(despotify_callback, &cdata, true, true);
        if (!ds)
        {
            printf("despotify_init_client() failed\n");
            return "";
        }

        char post_data[1024], username[1024], password[1024], uri[1024];
        int post_data_len;
        post_data_len = mg_read(connection, post_data, sizeof(post_data));

        mg_get_var(post_data, post_data_len, "username", username, sizeof(username));
        mg_get_var(post_data, post_data_len, "password", password, sizeof(password));
        mg_get_var(post_data, post_data_len, "uri", uri, sizeof(uri));

        printf("username: %s\npassword: %s\nuri: %s\n",username, password, uri);

        if (strlen(username) && strlen(password) && strlen(uri) && despotify_authenticate(ds, username, password))
        {
            printf("auth'd\n");
            char id[33];
            despotify_uri2id(uri,id);
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
                FILE* file = fopen(filename, "rb");
                size_t filesize = 0;
                // This is not exact, but we should expect to have at least this
                // much audio data, not counting the header blocks. Just a sanity check
                // to make sure the download wasn't stopped early
                size_t expectedsize = (t->file_bitrate/8)*(t->length/1000.0);
                if (file)
                {
                    fseek(file, 0L, SEEK_END);
                    filesize = ftell(file);
                }

                printf("starting up\n");
                if (file && filesize > expectedsize)
                {
                    fclose(file);
                    printf("found cache\n");
                }
                else
                {
                    despotify_play(ds, t, false);
                    file = fopen(filename, "wb");
                    cdata.play = true;
                    char buf[4096];
                    while (cdata.play)
                    {
                        snd_fill_fifo(ds);
                        size_t outsize = snd_consume_data(ds,sizeof(buf),buf,vorbis_consume);
                        if (outsize) {
                            ds->client_callback(ds, DESPOTIFY_TIME_TELL, &outsize, ds->client_callback_data);
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
            "%s\r\n%s\r%s\r\n\nAuthorization Failed :(", username, password, uri);
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
