#ifndef __MESSAGE_IO__
#define __MESSAGE_IO__

#include <stddef.h>
#include <stdio.h>

#define MAX_PAYLOAD_LENGTH 65000
#define MAX_USERNAME_LENGTH 64

enum message_type
{
        NOWT,  /* util.py: MessageType.TEXT = 1 ... */
        TEXT,
        DISCONNECT,
        VERIFY,
        USERNAME,
        REQUEST_LOGS,
        KA_ENQ,
        KA_ACK
};


struct message
{
        unsigned short length;  /* exclusive */
        unsigned char type;
        char contents[MAX_PAYLOAD_LENGTH];  /* wasteful? */
};

struct message *receive_message(int sockfd);
int send_message(void *payload, unsigned char type, int sockfd, 
        size_t payload_length);
int pass_on_message(const struct message *msg, int sockfd);

int store_message_to_log(const struct message *msg, 
        const char *username, FILE *logf);

#endif
