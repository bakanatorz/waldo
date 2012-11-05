#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <despotify.h>

#include "mongoose.h"

void despotify_callback(struct despotify_session* ds, int signal, void* data, void* callback_data)
{
}

static void *mongoose_callback(enum mg_event event, struct mg_connection* connection)
{
    const struct mg_request_info* request_info = mg_get_request_info(connection);
    if (event == MG_NEW_REQUEST)
    {
        struct despotify_session* ds = despotify_init_client(despotify_callback, NULL, true, true);
        if (!ds)
        {
            printf("despotify_init_client() failed\n");
            return 1;
        }

        char post_data[1024], username[sizeof(post_data)], password[sizeof(post_data)], uri[sizeof(post_data)];
        int post_data_len;

        // Read POST data
        post_data_len = mg_read(connection, post_data, sizeof(post_data));

        // Parse form data. input1 and input2 are guaranteed to be NUL-terminated
        mg_get_var(post_data, post_data_len, "username", username, sizeof(username));
        mg_get_var(post_data, post_data_len, "password", password, sizeof(password));
        mg_get_var(post_data, post_data_len, "uri", uri, sizeof(uri));

        const char* yay = "Authorization successful!";
        const char* nay = "Authorization failed :(";
        const char* msg;

        if (despotify_authenticate(ds, username, password))
        {
            msg = yay;
        }
        else
        {
            msg = nay;
        }

        mg_printf(connection, "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain\r\n\r\n"
        "%s\r\n",
        msg);
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
