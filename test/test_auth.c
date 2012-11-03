#include <stdlib.h>
#include <stdio.h>
#include <despotify.h>

#include "auth.h"

int main(int argc, char** argv)
{
    // Get the auth params from a file
    if(argc < 2)
    {
        printf("No filename given\n");
        return 1;
    }

    auth_t* auth = auth_file(argv[1]);

    if (!auth)
    {
        printf("Couldn't find username and/or password\n");
        return 1;
    }
    
    //printf("username: %s\npassword: %s\n", auth->username, auth->password);

    // Set up despotify
    if (!despotify_init())
    {
        printf("despotify_init() failed\n");
        return 1;
    }
    struct despotify_session* ds = despotify_init_client(NULL, NULL, true, true);
    if (!ds)
    {
        printf("despotify_init_client() failed\n");
        return 1;
    }

    if (!despotify_authenticate(ds, auth->username, auth->password))
    {
        printf("Authentication failed\n");
        despotify_exit(ds);
        return 1;
    }

    printf("Authentication successful!\n");
    
    free_auth(auth);
    return 0;
}
