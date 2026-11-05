#include "../inc/messageio.h"
#include "../inc/server.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_LISTEN 1000
#define KA_DELAY 5L
#define KA_DELAY_MAX KA_DELAY * 2

#define SERVER_DEBUG
#define SERVER_DEBUG_VERBOSE
#define LACKSIDASICAL_THREAD_MANAGEMENT

int g_exit = 0;

static void sigint_handler(int signo, siginfo_t *info, void *context)
{
        g_exit = 1;
}

static int set_up_socket(unsigned short port)
{
        int accept_sockfd;
        struct sockaddr_in6 sa = {0};

        accept_sockfd = socket(AF_INET6, SOCK_STREAM, 0);
        if(accept_sockfd < 0) {
                perror("socket");
                return -1;
        }

        sa.sin6_port = htons(port);
        sa.sin6_family = AF_INET6;

        if(bind(accept_sockfd, (struct sockaddr *)&sa, sizeof sa) < 0) {
                perror("bind");
                return -1;
        }

        if(listen(accept_sockfd, MAX_LISTEN) < 0) {
                perror("listen");
                return -1;
        }

        return accept_sockfd;
}

int add_to_client_record(struct client_record **record,
        int client_sockfd, struct sockaddr_in6 client_addr)
{
        struct client_record *new_record;
        struct client_record *ptr;
        #ifdef SERVER_DEBUG
        printf("enter add_to_client_record\n");
        #endif

        new_record = malloc(sizeof *new_record);
        if(!new_record) {
                perror("malloc");
                return -1;
        }
        new_record->sockfd = client_sockfd;
        new_record->addr = malloc(sizeof client_addr);
        if(!new_record->addr) {
                perror("malloc");
                return -1;
        }
        memcpy(new_record->addr, (const void *)&client_addr, 
                sizeof client_addr );
        strncpy(new_record->username, "Anonymous", 
                MAX_USERNAME_LENGTH);
        new_record->time_since_ka_ack = 0;
        new_record->f_connected = 1;

        new_record->prev = NULL;
        new_record->next = NULL;

        if(!*record)
        {
                *record = new_record;
                #ifdef SERVER_DEBUG_VERBOSE
                printf("add_to_client_record: created record\n");
                #endif
                return 0;
        }

        /* get to end of LL */
        ptr = *record;
        while(ptr->next) {
                ptr = ptr->next;
        }
        new_record->prev = ptr;
        ptr->next = new_record;

        #ifdef SERVER_DEBUG
        printf("exit add_to_client_record\n");
        #endif

        return 0;
}


int delete_client_record(struct client_record *record)
{
        struct client_record *ptr;
        struct client_record *prev;

        #ifdef SERVER_DEBUG
        printf("enter delete_client_record\n");
        #endif

        prev = NULL;

        if(!record) {
                #ifdef SERVER_DEBUG
                printf("delete_client_record: empty record\n");
                printf("exit delete_client_record\n");
                #endif
                return 0;
        }

        ptr = record;
        while(ptr) {
                prev = ptr;
                ptr = ptr->next;
        }

        while(prev) {
                struct client_record *d;
                d = prev->prev;
                close(prev->sockfd);
                free(prev->addr);
                free(prev);
                prev = d;
        }

        #ifdef SERVER_DEBUG
        printf("exit delete_client_record\n");
        #endif

        return 0;
}


int remove_from_client_record(struct client_record **record,
        int client_sockfd)
{
        struct client_record *ptr;
        int n_clients;
        #ifdef SERVER_DEBUG
        printf("enter remove_from_client_record\n");
        #endif

        n_clients = 0;
        ptr = *record;
        while(ptr) {
                if(ptr->sockfd == client_sockfd) {
                        struct client_record *prev, *next;
                        prev = ptr->prev;
                        next = ptr->next;
                        if(prev) {
                                prev->next = next;
                        }
                        free(ptr->addr);
                        free(ptr);
                        if(n_clients < 1) {
                                *record = NULL;
                        }
                        #ifdef SERVER_DEBUG
                        printf("Client record size is now %d\n", n_clients);
                        printf("exit remove_from_client_record\n");
                        #endif
                        return 0;
                }
                n_clients++;
                ptr = ptr->next;
        }

        #ifdef SERVER_DEBUG
        printf("Client record size is now %d\n", n_clients);
        printf("exit remove_from_client_record\n");
        #endif
        return -1;
}

/* poll_record.c */
static void *poll_record_thread(void *argument) {
        struct record_data *data = (struct record_data *)argument;
        poll_record(data);
        pthread_exit(NULL);
        return NULL;
}

#ifdef ENABLE_KEEPALIVE
static void *keepalive_thread(void *argument) {
        struct record_data *data = (struct record_data *)argument;
        keepalive(data);
        pthread_exit(NULL);
        return NULL;
}
#endif

static int shutdown_server(struct record_data *data) {
        /* *data->f_exit = 1; */
        pthread_mutex_lock(&data->record_lock);
        delete_client_record(data->record);
        pthread_mutex_unlock(&data->record_lock);

        fwrite("Server shutdown.\n", 1, 16, data->logf);
        fclose(data->logf);
        return 0;
}

int server(unsigned short port)
{
        int accept_sockfd, rc;
        struct record_data data;
        struct sigaction act, act_sigpipe;
        #ifdef ENABLE_KEEPAILVE
        pthread_t ka_thr;
        #endif
        pthread_t poll_thr;

        memset(&act, 0, sizeof act);
        memset(&act_sigpipe, 0, sizeof act_sigpipe);

        act.sa_sigaction = &sigint_handler;
        if(sigaction(SIGINT, &act, NULL) < 0) {
                perror("sigaction");
                return -1;
        }

        act_sigpipe.sa_handler = SIG_IGN;
        if(sigaction(SIGPIPE, &act_sigpipe, NULL) < 0) {
                perror("sigaction");
                return -1;
        }

        data.f_exit = &g_exit;
        data.n_clients = 0;
        data.record = NULL;
        (void)pthread_mutex_init(&data.record_lock, NULL);

        accept_sockfd = set_up_socket(port);
        if(accept_sockfd < 0) {
                fprintf(stderr, "set_up_socket\n");
                return -1;
        }

        /* open chat log fd */
        #ifdef SERVER_DEBUG
        data.logf = fopen(CHAT_LOG_FILENAME, "w");
        #else
        data.logf = fopen(CHAT_LOG_FILENAME, "a");
        #endif
        fwrite("Server started.\n", 1, 17, data.logf);

        #ifdef ENABLE_KEEPAILVE
        /* start keepalive thread */
        rc = pthread_create(&ka_thr, NULL, 
                &keepalive_thread,
                (void *)&data);
        if(rc) {
                perror("pthread_create");
                return -1;
        }
        #endif

        /* start thread for poll_record */
        rc = pthread_create(&poll_thr, NULL, 
                &poll_record_thread,
                (void *)&data);
        if(rc) {
                perror("pthread_create");
                return -1;
        }

        /* start stats display thread */

        /* parent accept loop */
        while(!*data.f_exit) {
                int client_sockfd;
                struct sockaddr_in6 client_addr;
                socklen_t client_len;

                client_len = sizeof client_addr;
                client_sockfd = accept(accept_sockfd, 
                        (struct sockaddr *)&client_addr, 
                        &client_len);
                if(client_sockfd < 0) {
                        perror("accept");
                        break;
                }
                /*fcntl(client_sockfd, F_SETFL, O_NONBLOCK);*/

                pthread_mutex_lock(&data.record_lock);
                if(add_to_client_record(&data.record, 
                        client_sockfd, client_addr) < 0) {
                        fprintf(stderr, "add_to_client_record");
                }
                pthread_mutex_unlock(&data.record_lock);
                #ifdef SERVER_DEBUG
                printf("Client connected.\n");
                #endif
        }

        shutdown_server(&data);
        #ifdef LACKSIDASICAL_THREAD_MANAGEMENT
        pthread_join(poll_thr, NULL);
        #ifdef ENABLE_KEEPALIVE
        pthread_join(ka_thr, NULL);
        #endif
        #else
        pthread_kill(poll_thr, SIGINT);
        #ifdef ENABLE_KEEPALIVE
        pthread_kill(ka_thr, NULL);
        #endif
        #endif

        return 0;
}

/* smaller threads */
int keepalive(struct record_data *data) {
        struct client_record *record;
        char type = KA_ENQ;

        record = data->record;

        while(!*data->f_exit) {
                sleep((unsigned int)KA_DELAY);
                #ifdef SERVER_DEBUG
                puts("Broadcast loop...");
                #endif
                record = data->record;
                /* broadcast loop */
                pthread_mutex_lock(&data->record_lock);
                while(record) {
                        /* state not connected if delay too long */
                        if(record->time_since_ka_ack >= KA_DELAY_MAX) {
                                record->f_connected = 0;
                        }
                        /* increment time of each client */
                        record->time_since_ka_ack += KA_DELAY;
                        if(!record->f_connected) {
                                goto next_record;
                        }
                        /* send keepalive enquiry to each client */
                        if(send_message(&type, type, record->sockfd, 
                                sizeof type)) {
                                fprintf(stderr, "send_message\n");
                                /* SIGPIPE */
                                record->f_connected = 0;
                        }
                        next_record:
                        record = record->next;
                }
                pthread_mutex_unlock(&data->record_lock);
        }
        
        return 0;
}
