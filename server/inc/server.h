#ifndef __SERVER_H__
#define __SERVER_H__

#include "standards.h"

#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>

#define SERVER_DEBUG

#define MAX_USERNAME_LENGTH 64
#define CHAT_LOG_FILENAME "/tmp/gombu_log"

struct client_record
{
        struct sockaddr_in6 *addr;
        struct client_record *prev;
        struct client_record *next;
        char username[MAX_USERNAME_LENGTH];
        long time_since_ka_ack;
        int sockfd;
        int f_connected;
};

/* stores everything about the server, could may as well be global */
struct record_data
{
        int *f_exit;
        int n_clients;
        struct client_record *record;
        FILE *logf;
        pthread_mutex_t record_lock;
};

struct snfds {
        struct pollfd *fds;
        nfds_t nfds;
};

/*
 * server.c
 */
int server(unsigned short port);

/* client record handling */
int add_to_client_record(struct client_record **record,
        int client_sockfd, struct sockaddr_in6 client_addr);
int remove_from_client_record(struct client_record **record,
        int client_sockfd);


/* keepalive thread if necessary */
int keepalive(struct record_data *);

/*
 * poll_record.c
 */
/* in poll thread */
int poll_record(struct record_data *record);
struct snfds *pollfds_from_client_record(
        const struct client_record *record);
int handle_client(int fd, struct record_data *record);

/* server actions */
int handle_ka_ack(int client_fd, struct record_data *data);

/*
 * stats.c
 */
#endif
