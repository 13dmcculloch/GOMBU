#include "../inc/messageio.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#define UNIT_SEPARATOR (char)31

static ssize_t tcp_read(void *dest, int sockfd, size_t n)
{
        ssize_t n_read = 0;

        while(n_read < n) {
                ssize_t rc;

                rc = read(sockfd, (char *)dest + n_read, n - n_read);
                if(rc <= 0) {
                        perror("read");
                        return -1;
                }

                n_read += rc;
        }

        return n_read;
}

static ssize_t tcp_write(void *data, int sockfd, size_t n)
{
        ssize_t n_sent = 0;

        while(n_sent < n) {
                ssize_t rc;

                rc = write(sockfd, (char *)data + n_sent, n - n_sent);
                if(rc < 0) {
                        perror("write");
                        return -1;
                }

                n_sent += rc;
        }

        return n_sent;
}

struct message *receive_message(int sockfd)
{
        struct message *ret;
        unsigned short length;

        ret = malloc(sizeof *ret);
        if(!ret) {
                perror("malloc");
                return NULL;
        }
        memset(ret, '\0', sizeof *ret);

        if(tcp_read(&length, sockfd, sizeof length) <= 0) {
                fprintf(stderr, "tcp_read\n");
                free(ret);
                return NULL;
        }

        ret->length = ntohs(length);

        if(tcp_read((char *)ret + sizeof ret->length,
                sockfd, ret->length) <= 0) {
                fprintf(stderr, "tcp_read\n");
                free(ret);
                return NULL;
        }

        return ret;
}

int send_message(void *payload, unsigned char type, int sockfd, 
        size_t payload_length)
{
        unsigned short length;

        if(payload_length > MAX_PAYLOAD_LENGTH) {
                fprintf(stderr, "send_message: too long\n");
                return -1;
        }

        length = htons(payload_length + sizeof(type));

        if(tcp_write(&length, sockfd, sizeof(length)) < 0) {
                fprintf(stderr, "tcp_write\n");
                return -1;
        }

        if(tcp_write(&type, sockfd, sizeof type) < 0) {
                fprintf(stderr, "tcp_write\n");
                return -1;
        }

        if(tcp_write(payload, sockfd, payload_length) < 0) {
                fprintf(stderr, "tcp_write\n");
                return -1;
        }

        return 0;
}

int store_message_to_log(const struct message *msg, const char *username, 
        FILE *logf)
{
        size_t contents_length, buf_length;
        char time_str[64];
        char *buf, *buf_s;
        char us = UNIT_SEPARATOR;
        time_t t;
        struct tm *tp;

        contents_length = msg->length - sizeof msg->type;

        t = time(NULL);
        tp = gmtime(&t);
        if(strftime(time_str, sizeof time_str, "%x %X: ", 
                tp) < 1) {
                fprintf(stderr, "strftime\n");
        }

        buf_length = (contents_length + sizeof time_str 
                + MAX_USERNAME_LENGTH + 1);
        buf = malloc(buf_length * sizeof *buf);
        if(!buf) {
                perror("malloc");
                return -1;
        }
        memset(buf, '\0', buf_length * sizeof *buf);
        buf_s = buf;

        memcpy(buf, &us, sizeof(us));
        buf += sizeof(us);
        strcpy(buf, time_str);
        buf += strlen(time_str);
        strncpy(buf, username, MAX_USERNAME_LENGTH);
        buf += strlen(username);
        memcpy(buf, ": ", 2);
        buf += 2;
        memcpy(buf, msg->contents, contents_length);
        buf += contents_length;
        strcpy(buf, "\n");

        fwrite(buf_s, sizeof *buf_s, strlen(buf_s), logf);
        fflush(logf);

        free(buf_s);
        
        return 0;
}

