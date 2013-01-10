#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/time.h>

#include <despotify.h>

#include "mongoose/mongoose.h"
#include "sndqueue.h"

typedef struct track_status_t
{
    double completion;
    struct track* t;
    pthread_mutex_t lock;
    struct track_status_t* next;
} track_status_t;

typedef struct
{
    const char* filename;
    struct despotify_session* ds;
    struct track* t;
} downloader_data_t;

typedef enum {
    INIT,
    CHECK,
    GET,
    NUMREQTYPES
} reqtype;

static const char *username, *password;
static track_status_t* global_tracks = NULL;
pthread_mutex_t global_lock;

void despotify_callback(struct despotify_session* ds, int signal, void* data, void* callback_data)
{
    if (signal == DESPOTIFY_END_OF_PLAYLIST && callback_data)
    {
        *((bool*)callback_data) = 0;
    }
}

size_t fileSize(const char* filename)
{
    FILE* fp = fopen(filename, "rb");
    fseek(fp, 0L, SEEK_END);
    size_t size = ftell(fp);
    fclose(fp);
    return size;
}

size_t expectedSize(const struct track* t)
{
    return (t->file_bitrate/8)*(t->length/1000.0)/1024;
}

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

void json_track_info(struct mg_connection* connection, const struct track* t)
{
    if(t && t->has_meta_data)
    {
        mg_printf(connection, "{\"title\" : \"%s\", \"album\" : \"%s\", \"year\": %d, \"length\": %d, \"artists\": \"",
                t->title, t->album, t->year, t->length);
        for (struct artist* a = t->artist; a; a = a->next)
        {
            mg_printf(connection, "%s%s", a->name, a->next ? ", " : "");
        }
        mg_printf(connection, "\"}");
    }
    else
    {
        printf("Track has no metadata\n");
    }
}

bool fileExists(const char* filename)
{
    FILE* fp = fopen(filename,"r");
    bool val = fp;
    fclose(fp);
    return val;
}

void respond(struct despotify_session* ds, struct mg_connection* connection, const char* value, const struct track* t)
{
    mg_printf(connection, "HTTP/1.0 200 OK\r\n"
    "Content-Type: application/json\r\n"
    "Access-Control-Allow-Origin: *\r\n\r\n"
    "{\"response\":\"%s\"", value);
    if (t)
    {
        mg_printf(connection, ", \"info\" : ");
        json_track_info(connection, t);
    }
    mg_printf(connection, "}");
    if (ds)
    {
        despotify_exit(ds);
    }
}

void* download_thread(void* param)
{
    downloader_data_t* data = param;
    printf("Download thread starting for %s\n", data->t->track_id);

    // Create track status
    track_status_t* track = malloc(sizeof(track_status_t));
    track->completion = 0.0;
    track->t = data->t;
    track->next = NULL;
    pthread_mutex_init(&track->lock, NULL);

    // Push track status to ll
    pthread_mutex_lock(&global_lock);
    if (!global_tracks)
    {
        global_tracks = track;
    }
    else
    {
        track_status_t* curtrack = global_tracks;
        while (curtrack->next)
        {
            curtrack = curtrack->next;
        }
        curtrack->next = track;
    }
    pthread_mutex_unlock(&global_lock);
    printf("Added %s to global queue\n", track->t->track_id);

    // Check file size
    despotify_play(data->ds, (struct track*)data->t, false);
    printf("Loaded despotify for %s\n", track->t->track_id);
    FILE* file = fopen(data->filename, "wb");
    char buf[4096];
    size_t totdata = expectedSize(data->t);
    size_t cumdata = 0;
    bool* play = malloc(sizeof(bool));
    *play = true;
    data->ds->client_callback_data = play;
    while (track->t && play)
    {
        snd_fill_fifo(data->ds);
        size_t outsize = snd_consume_data(data->ds,sizeof(buf),buf,vorbis_consume);
        if (outsize) {
            fwrite(buf, outsize, 1, file);
            cumdata += outsize;
            double val = ((double)cumdata)/1024/totdata;
            pthread_mutex_lock(&track->lock);
            track->completion = val;
            pthread_mutex_unlock(&track->lock);
        }
        else
        {
            break;
        }
    }
    fclose(file);
    if (!track->t)
    {
        remove(data->filename);
    }

    // Cleanup
    printf("cleaning up\n");
    pthread_mutex_lock(&global_lock);
    if (global_tracks == track)
    {
        global_tracks = track->next;
    }
    else
    {
        track_status_t* curtrack = global_tracks;
        while (curtrack->next != track)
        {
            curtrack = curtrack->next;
        }
        curtrack->next = track->next;
    }
    pthread_mutex_unlock(&global_lock);

    printf("Tarck %s complete\n", track->t->track_id);
    despotify_exit(data->ds);
    free((char*)data->filename);
    
    free(track);
    free(data);
    free(play);
    return 0;
}

// Callback when the server gets a new request
static void *mongoose_callback(enum mg_event event, struct mg_connection* connection)
{
    const struct mg_request_info* request_info = mg_get_request_info(connection);
    if (event == MG_NEW_REQUEST)
    {
        printf("New Request: %s\n", request_info->uri);

        // Parse the uri and get the request type and track ID
        if (!(request_info->uri+1))
        {
            return NULL;
        }
        char* req = strdup(request_info->uri+1);
        printf("req: %s\n", req);
        if (!strcmp(req, "monitor"))
        {
            printf("monitor request\n");
            mg_printf(connection, "HTTP/1.0 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n"
            "{");
            bool first=true;
            pthread_mutex_lock(&global_lock);
            track_status_t* track = global_tracks;
            while (track)
            {
                const unsigned char* id;
                double completion;
                pthread_mutex_lock(&track->lock);
                id = track->t->track_id;
                completion = track->completion;
                pthread_mutex_unlock(&track->lock);
                if (!first)
                {
                    mg_printf(connection, ", ");
                }
                mg_printf(connection, "\"%s\" : %.2f", id, completion);
                first = false;
                track = track->next;
            }
            pthread_mutex_unlock(&global_lock);
            mg_printf(connection, "}");
            printf("Printed Monitor\n");
            return "";
        }

        char* uri = strchr(req, '/');
        if (uri)
        {
            *uri = '\0';
            uri+=1;
        }
        char* end = strchr(uri, '/');
        if (end)
        {
            *end = '\0';
        }
        if (!req || !uri || strlen(uri) != 22)
        {
            printf("No valid request found\n");
            return NULL;
        }

        // Check if the ID is valid
        // If we're handling a GET req, return 404
        // else give a JSON response of "invalid"
        // if valid track not found
        char id[33];
        despotify_uri2id(uri,id);

        // Get the filename
        char filename[37];
        strcpy(filename, id);
        strcpy(filename+32, ".ogg");
        printf("id: %s\n",id);

        // Save the request type as an enum
        bool forceinit = false;
        reqtype request;
        if (!strcmp(req, "forceinit"))
        {
            printf("force init request\n");
            forceinit = true;
            request = INIT;
        }
        else if (!strcmp(req, "init"))
        {
            printf("init request\n");
            request = INIT;
        }
        else if (!strcmp(req, "check"))
        {
            printf("check request\n");
            request = CHECK;
        }
        else if (!strcmp(req, "get"))
        {
            printf("get request\n");
            request = GET;
        }
        else
        {
            printf("Invalid request verb found\n");
            return NULL;
        }

        // If it's a check or init, see if the track is in the queue or
        // the file exists before authenticating
        if (request == INIT || request == CHECK)
        {
            // Check if the track is in the list
            pthread_mutex_lock(&global_lock);
            track_status_t* track = global_tracks;
            track_status_t* prevtrack = NULL;
            while(track)
            {
                if (!strcmp((char*)track->t->track_id, id))
                {
                    if (request == CHECK)
                    {
                        // It's safe to respond() without
                        // holding the mutex because it's const
                        pthread_mutex_unlock(&global_lock);
                        double val;
                        pthread_mutex_lock(&track->lock);
                        val = track->completion;
                        pthread_mutex_unlock(&track->lock);
                        char vals[5];
                        sprintf(vals,"%.2f", val);
                        respond(NULL, connection, vals, track->t);
                        printf("Track %s Completion: %.2f%%\n", id, val*100);
                        return "";
                    }
                    else if (forceinit)
                    {
                        prevtrack->next = track->next;
                        track->t = NULL;
                        pthread_mutex_unlock(&global_lock);
                        printf("Removed current download\n");
                        break;
                    }
                    else // request == INIT
                    {
                        respond(NULL, connection, "progressing", track->t);
                        return "";
                    }
                }
                prevtrack = track;
                track = track->next;
            }
            pthread_mutex_unlock(&global_lock);

        }

        // Create the spotify session
        struct despotify_session* ds = despotify_init_client(despotify_callback, NULL, true, true);
        if (!ds)
        {
            printf("despotify_init_client() failed\n");
            return NULL;
        }
        if (!despotify_authenticate(ds, username, password))
        {
            printf("Failed authentication\n");
            despotify_exit(ds);
            return NULL;
        }

        struct track* t = despotify_get_track(ds, id);
        if (!t)
        {
            if (request == GET)
            {
                despotify_exit(ds);
                printf("Invalid Track %s\n", id);
                return NULL;
            }
            respond(ds, connection, "invalid", NULL);
            printf("Invalid Track %s\n", id);
            return "";
        }
        
        // Check if the file exists
        if (((request == INIT && !forceinit) || request == CHECK) && fileExists(filename))
        {
            respond(NULL, connection, "complete", t);
            printf("Track %s complete\n", id);
            return "";
        }

        // Know we have a valid track id now
        printf("Serving track %s\n",id);
        printf("Track Info:\n");
        print_track_info(t);
        printf("\n");

        switch (request)
        {
            // GET - return the file
            case GET:
                mg_printf(connection, "Access-Control-Allow-Origin: *\r\n");
                mg_send_file(connection, filename);
                printf("Sent %s\n", filename);
                return "";
            // CHECK - track must be unstarted
            case CHECK:
                respond(ds, connection, "unstarted", t);
                printf("Track %s unstarted\n", id);
                return "";
            // INIT - track should start
            case INIT:
                if (!forceinit && (fileExists(filename) && fileSize(filename) > expectedSize(t)))
                {
                    respond(ds, connection, "complete", t);
                    printf("Track %s complete\n", id);
                    return "";
                }
                // TODO (ebakan): launch download thread
                pthread_t thread;
                downloader_data_t* data = malloc(sizeof(downloader_data_t));
                data->filename = strdup(filename);
                data->ds = ds;
                data->t = t;
                pthread_create(&thread, NULL, &download_thread, data);
                pthread_detach(thread);
                respond(NULL, connection, "starting", t);
                printf("Track %s download starting\n", id);
                return "";
            default:
                despotify_exit(ds);
                return NULL;
        }
    }
    return NULL;
}

int main(int argc, char** argv)
{
    // Set up Mongoose options
    struct mg_context* context;
    const char* port = "8080";
    if (argc > 3)
    {
        port = argv[1];
        username = argv[2];
        password = argv[3];
    }
    else if (argc > 2)
    {
        username = argv[1];
        password = argv[2];
    }
    else
    {
        printf("Invalid parameters. Proper usage is waldo_server [port] username password\n");
    }
    const char* options[] = {"listening_ports", port, NULL};
    char** c = options;
    printf("Options:\n");
    while (*c)
    {
        printf("%s %s\n",*c,*(c+1));
        c+=2;
    }

    // Init spotify
    printf("Initting despotify\n");
    if (!despotify_init())
    {
        printf("despotify_init() failed\n");
        return 1;
    }

    pthread_mutex_init(&global_lock, NULL);

    // Run Mongoose
    printf("starting mongoose\n");
    context = mg_start(&mongoose_callback, NULL, options);
    printf("mongoose up and running\n");
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
