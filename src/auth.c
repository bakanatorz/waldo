#include "auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFLEN 60

auth_t* auth_file(const char* filename)
{
    FILE* fp;
    if ((fp = fopen(filename, "r")) == NULL)
    {
        fprintf(stderr, "Cannot open file %s\n", filename);
        return NULL;
    }

    char *username=NULL, *password = NULL;
    char buf[BUFLEN];

    if (fgets(buf, sizeof(buf), fp) != NULL)
    {
        buf[strlen(buf)-1] = '\0';
        if (strlen(buf) == 0)
        {
            return NULL;
        }
        username = strdup(buf);
    }
    else
    {
        return NULL;
    }

    if (fgets(buf, sizeof(buf), fp) != NULL)
    {
        buf[strlen(buf)-1] = '\0';
        if (strlen(buf) == 0)
        {
            return NULL;
        }
        password = strdup(buf);
    }
    else
    {
        return NULL;
    }

    auth_t* ret = malloc(sizeof(auth_t));
    ret->username = username;
    ret->password = password;
    return ret;

}

void free_auth(auth_t* auth)
{
    if (auth)
    {
        free(auth->username);
        free(auth->password);
        free(auth);
    }
}
