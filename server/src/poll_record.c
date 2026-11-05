#include "../inc/server.h"
#include "../inc/messageio.h"

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define POLL_DELAY_MS 5000

int poll_record(struct record_data *data) {
        while(!*data->f_exit) {
                struct snfds *pollfds;
                int n_fds;
                int fd_k;

                pthread_mutex_lock(&data->record_lock);
                pollfds = pollfds_from_client_record(data->record);
                pthread_mutex_unlock(&data->record_lock);

                n_fds = poll(pollfds->fds, pollfds->nfds, POLL_DELAY_MS);
                if(n_fds < 0) {
                        free(pollfds->fds);
                        free(pollfds);
                        perror("poll");
                        return -1;
                }

                #ifdef SERVER_DEBUG
                printf("from %ld fds poll returned %d\n", pollfds->nfds, 
                        n_fds);
                #endif

                for(fd_k = 0; fd_k < n_fds; fd_k++) {
                        struct pollfd p;

                        p = pollfds->fds[fd_k];

                        #ifdef SERVER_DEBUG
                        printf("event fd: %d\n", p.fd);
                        #endif

                        if(p.revents & POLLIN)
                        {
                                if(handle_client(p.fd, data) < 0) {
                                        fprintf(stderr, "handle_client\n");
                                }
                        }
                        /* never seems to HUP because there's always data to read */
                        else if (p.revents & POLLHUP) {
                                #ifdef SERVER_DEBUG
                                printf("Hard disconnect.");
                                #endif
                        }
                }

                free(pollfds->fds);
                free(pollfds);
        }

        return 0;
}

struct snfds *pollfds_from_client_record(
        const struct client_record *record) {
        struct client_record *cur;
        struct snfds *result;
        struct pollfd *fds;
        nfds_t n_clients;
        size_t fd_idx;

        #ifdef SERVER_DEBUG_VERBOSE
        printf("enter pollfds_from_client_record\n");
        #endif

        result = malloc(sizeof *result);
        if(!result) {
                perror("malloc");
                return NULL;
        }
        result->fds = NULL;
        result->nfds = 0;
        n_clients = 0;

        if(!record) {
                #ifdef SERVER_DEBUG_VERBOSE
                printf("pollfds_from_client_record: empty record\n");
                #endif
                return result;
        }

        cur = (struct client_record *)record;
        while(cur) {
                n_clients += 1;
                cur = cur->next;
        }
        result->nfds = n_clients;

        fds = malloc(sizeof *result->fds * n_clients);
        if(!fds) {
                perror("malloc");
                return NULL;
        }
        memset(fds, '\0', sizeof *result->fds * n_clients);

        fd_idx = 0;
        cur = (struct client_record *)record;
        while(cur) {
                fds[fd_idx].fd = cur->sockfd;
                fds[fd_idx].events = POLLIN;
                cur = cur->next;
        }
        result->fds = fds;
        #ifdef SERVER_DEBUG_VERBOSE
        printf("exit pollfds_from_client_record\n");
        #endif

        return result;
}

int broadcast_to_clients(struct message *msg, struct record_data *data,
        char *username) {

        struct client_record *record = {0};
        char new_payload[MAX_PAYLOAD_LENGTH];
        size_t username_length, colon_length;
        char colon_sep[] = ": ";

        record = data->record;

        username_length = strlen(username);
        colon_length = strlen(colon_sep);

        memset(new_payload, '\0', sizeof new_payload);

        strncpy(new_payload, username, MAX_USERNAME_LENGTH);
        strcpy(new_payload + username_length, colon_sep);
        strncpy(new_payload + username_length + colon_length,
                msg->contents, MAX_PAYLOAD_LENGTH - username_length
                - colon_length); /* prevent run over */

        /* uh oh... Python does not use NULL terminated strings... */
        while(record) {
                if(!record->f_connected) {
                        /* avoid writing to already disconnected client */
                        goto next_record;
                }
                if(send_message(new_payload, msg->type, 
                        record->sockfd, strlen(new_payload))) {
                        fprintf(stderr, "broadcast_to_clients\n");
                }
                next_record:
                record = record->next;
        }
        
        return 0;
}

/* NULL-terminated username! */
int set_username(int fd, struct client_record *record, char *new_username) {
        if(strlen(new_username) > sizeof record->username) {
                return -1; /* too long */
        }
        while(record) {
                if(fd == record->sockfd) {
                        strncpy(record->username, new_username,
                                MAX_USERNAME_LENGTH);
                        break;
                }
                record = record->next;
        }
        
        return 0;
}

char *get_username(int fd, struct client_record *record) {
        while(record) {
                if(fd == record->sockfd) {
                        return record->username;
                }
                record = record->next;
        }
        return NULL;
}

struct client_record *get_client(int fd, struct client_record *record) {
        while(record) {
                if(fd == record->sockfd) {
                        return record;
                }
                record = record->next;
        }
        return NULL;
}

int handle_client(int fd, struct record_data *data) {
        /* receive message and act based on its type */
        struct message *msg;
        struct client_record *client;
        char *client_username;
        #ifdef SERVER_DEBUG
        puts("enter handle_client");
        #endif

        pthread_mutex_lock(&data->record_lock);
        #ifdef SERVER_DEBUG_VERBOSE
        puts("got lock");
        #endif
        client = get_client(fd, data->record);
        if(!client) {
                fprintf(stderr, "get_client (impossible)\n");
        }

        /* clear up disconnected client from keepalive */
        if(!client->f_connected) {
                #ifdef SERVER_DEBUG_VERBOSE
                puts("No connection.");
                #endif
                close(client->sockfd);
                if(remove_from_client_record(&data->record,
                        client->sockfd) < 0) {
                        fprintf(stderr, 
                        "remove_from_client_record\n");
                }
                pthread_mutex_unlock(&data->record_lock);
                #ifdef SERVER_DEBUG
                puts("Removed unconnected client.");
                puts("Exit handle_client.");
                #endif
                return 0;
        }

        msg = receive_message(fd);
        #ifdef SERVER_DEBUG_VERBOSE
        puts("Got message");
        #endif
        /* empty message may mean connection remotely closed */
        if(!msg) {
                #ifdef SERVER_DEBUG_VERBOSE
                puts("No message.");
                #endif
                client->f_connected = 0;
                pthread_mutex_unlock(&data->record_lock);
                #ifdef SERVER_DEBUG
                puts("Exit handle_client.");
                #endif
                return 0;
        }
        #ifdef SERVER_DEBUG
        printf("type: %hu\n", msg->type);
        #ifdef SERVER_DEBUG_VERBOSE
        puts(msg->contents);
        #endif
        #endif

        client_username = client->username;

        switch(msg->type) {
                case TEXT:
                        store_message_to_log(msg, client_username,
                                data->logf);
                        broadcast_to_clients(msg, data, client_username);
                        break;
                case DISCONNECT:
                        store_message_to_log(msg, client_username,
                                data->logf);
                        #ifdef SERVER_DEBUG
                        puts("Disconnect");
                        #endif
                        remove_from_client_record(&data->record,
                                fd);
                        break;
                case VERIFY:
                case USERNAME:
                        #ifdef SERVER_DEBUG
                        puts("Set username");
                        #endif
                        store_message_to_log(msg, client_username,
                                data->logf);
                        client_username = msg->contents;
                        set_username(fd, data->record, client_username);
                        break; 
                case REQUEST_LOGS:
                case KA_ENQ:
                case KA_ACK:
                        #ifdef SERVER_DEBUG
                        puts("Enter handle_ka_ack");
                        #endif
                        (void)handle_ka_ack(fd, data);
                        break;
                default:
                        break;
        }

        pthread_mutex_unlock(&data->record_lock);

        free(msg);

        #ifdef SERVER_DEBUG
        puts("exit handle_client");
        #endif
        

        return 0;
}

int handle_ka_ack(int client_fd, struct record_data *data) {
        struct client_record *record;

        record = data->record;

        while(record) {
                if(client_fd == record->sockfd) {
                        record->time_since_ka_ack = 0L;
                        return 0;
                }
                record = record->next;
        }

        return -1;
}
                        
