#ifndef __AUTH_H__
#define __AUTH_H__
typedef struct
{
    char* username;
    char* password;
} auth_t;

auth_t* auth_file(const char* filename);

void free_auth_t(auth_t* auth);

#endif //__AUTH_H__
