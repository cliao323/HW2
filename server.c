#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS	10

static unsigned int cli_count = 0;
static int uid = 1;

/* Client structure */
typedef struct
{
    struct sockaddr_in addr;	/* Client remote address */
    int connfd;			/* Connection file descriptor */
    int uid;			/* Client unique identifier */
    char name[64];			/* Client name */
} client_t;

client_t *clients[MAX_CLIENTS];

/* Add client to list */
void list_add(client_t *cl);

/* Delete list from list */
void list_delete(int uid);

/* Send message to all clients but the sender */
void send_message(char *s, int uid);

/* Send message to all clients */
void send_message_all(char *s);

/* Send message to sender */
void send_message_self(const char *s, int connfd);

/* Send message to client */
void send_message_client(char *s, int uid);

/* Send list of online clients */
void send_online_clients(int connfd);

/* Strip CRLF */
void strip_newline(char *s);


/* Handle all communication with the client */
void *handle_client(void *arg)
{
    char buff_out[2048];
    char buff_in[1024];
    int rlen;
    char *name;

    cli_count++;
    client_t *cli = (client_t *)arg;

    printf("ACCEPTED\n");
    printf("REFERENCED BY %d\n", cli->uid);

    /* Let the User set name */
    send_message_self("[Server] NAME\r\n", cli->connfd);
    if((rlen = read(cli->connfd, buff_in, sizeof(buff_in)-1)) > 0)
    {
        buff_in[rlen] = '\0';
        buff_out[0] = '\0';
        strip_newline(buff_in);
        name = strtok(buff_in, " ");
                if(name && strcmp(name, "Server"))
                {
                    strcpy(cli->name, name);
                }
                else
                {
                    send_message_self("[Server] NAME CANNOT BE NULL or Server   USE /N TO CHANGE NAME\r\n", cli->connfd);
                }
    }

    sprintf(buff_out, "[Server]  %s JOINED\r\n", cli->name);
    send_message_all(buff_out);

    /* Receive input from client */
    while((rlen = read(cli->connfd, buff_in, sizeof(buff_in)-1)) > 0)
    {
        buff_in[rlen] = '\0';
        buff_out[0] = '\0';
        strip_newline(buff_in);

        /* Ignore empty buffer */
        if(!strlen(buff_in))
        {
            continue;
        }

        /* Special options */
        if(buff_in[0] == '/')
        {
            char *command, *param;
            command = strtok(buff_in," ");
            if(!strcmp(command, "/Q"))
            {
                break;
            }
            else if(!strcmp(command, "/T"))
            {
                send_message_self("[Server] TEST\r\n", cli->connfd);
            }
            else if(!strcmp(command, "/N"))
            {
                param = strtok(NULL, " ");
                if(param && strcmp(param, "Server"))
                {
                    char *old_name = strdup(cli->name);
                    strcpy(cli->name, param);
                    sprintf(buff_out, "[Server] RENAME, %s TO %s\r\n", old_name, cli->name);
                    free(old_name);
                    send_message_all(buff_out);
                }
                else
                {
                    send_message_self("[Server] NAME CANNOT BE NULL OR Server\r\n", cli->connfd);
                }
            }
            else if(!strcmp(command, "/PM"))
            {
                param = strtok(NULL, " ");
                if(param)
                {
                    int uid = atoi(param);
                    param = strtok(NULL, " ");
                    if(param)
                    {
                        sprintf(buff_out, "[PM][%s]", cli->name);
                        while(param != NULL)
                        {
                            strcat(buff_out, " ");
                            strcat(buff_out, param);
                            param = strtok(NULL, " ");
                        }
                        strcat(buff_out, "\r\n");
                        send_message_client(buff_out, uid);
                    }
                    else
                    {
                        send_message_self("[Server] MESSAGE CANNOT BE NULL\r\n", cli->connfd);
                    }
                }
                else
                {
                    send_message_self("[Server] ID CANNOT BE NULL\r\n", cli->connfd);
                }
            }
            else if(!strcmp(command, "/AN"))
            {
                    param = strtok(NULL, " ");
                    if(param)
                    {
                        sprintf(buff_out, "[AN][%s]", cli->name);
                        while(param != NULL)
                        {
                            strcat(buff_out, " ");
                            strcat(buff_out, param);
                            param = strtok(NULL, " ");
                        }
                        strcat(buff_out, "\r\n");
                        send_message_all(buff_out);
                    }
                    else
                    {
                        send_message_self("[Server] ANNOUNCEMENT CANNOT BE NULL\r\n", cli->connfd);
                    }
            }
            else if(!strcmp(command, "/O"))
            {
                sprintf(buff_out, "CLIENTS %d\r\n", cli_count);
                send_message_self(buff_out, cli->connfd);
                send_online_clients(cli->connfd);
            }
            else
            {
                send_message_self("[Server] UNKOWN COMMAND\r\n", cli->connfd);
            }
        }
        else
        {
            /* Send message test*/
            //snprintf(buff_out, sizeof(buff_out), "[%s] %s\r\n", cli->name, buff_in);
            //send_message(buff_out, cli->uid);
        }
    }

    /* Close connection */
    //close(cli->connfd);
    sprintf(buff_out, "[Server] %s DISCONNECTED\r\n", cli->name);
    send_message_all(buff_out);

    /* Delete client from list and yeild thread */
    list_delete(cli->uid);
    printf("LEAVED ");
    printf("REFERENCED BY %d\n", cli->uid);
    free(cli);
    cli_count--;
    pthread_detach(pthread_self());

    return NULL;
}

int main(int argc, char *argv[])
{
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    /* Socket settings */
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(atoi(argv[1]));

    /* Ignore pipe signals */
    signal(SIGPIPE, SIG_IGN);

    /* Bind */
    if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Socket binding failed");
        return 1;
    }

    /* Listen */
    if(listen(listenfd, 10) < 0)
    {
        perror("Socket listening failed");
        return 1;
    }

    printf("[SERVER STARTED PORT: %d MAX CLIENTS: %d]\n",atoi(argv[1]),MAX_CLIENTS);
    /* Accept clients */

    while(1)
    {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

        /* Check if max clients is reached */
        if((cli_count+1) == MAX_CLIENTS)
        {
            printf("MAX CLIENTS REACHED\n");
            printf("REJECT");
            printf("\n");
            close(connfd);
            continue;
        }

        /* Client settings */
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->addr = cli_addr;
        cli->connfd = connfd;
        cli->uid = uid++;
        sprintf(cli->name, "%d", cli->uid);

        /* Add client to the list and fork thread */
        list_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

        sleep(1);
    }
}

/* Add client to list */
void list_add(client_t *cl)
{
    int i;
    for(i=0; i<MAX_CLIENTS; i++)
    {
        if(!clients[i])
        {
            clients[i] = cl;
            return;
        }
    }
}

/* Delete client from list */
void list_delete(int uid)
{
    int i;
    for(i=0; i<MAX_CLIENTS; i++)
    {
        if(clients[i])
        {
            if(clients[i]->uid == uid)
            {
                clients[i] = NULL;
                return;
            }
        }
    }
}

/* Send message to all clients but the sender */
void send_message(char *s, int uid)
{
    int i;
    for(i=0; i<MAX_CLIENTS; i++)
    {
        if(clients[i])
        {
            if(clients[i]->uid != uid)
            {
                if(write(clients[i]->connfd, s, strlen(s))<0)
                {
                    perror("write");
                    exit(-1);
                }
            }
        }
    }
}

/* Send message to all clients */
void send_message_all(char *s)
{
    int i;
    for(i=0; i<MAX_CLIENTS; i++)
    {
        if(clients[i])
        {
            if(write(clients[i]->connfd, s, strlen(s))<0)
            {
                perror("write");
                exit(-1);
            }
        }
    }
}

/* Send message to sender */
void send_message_self(const char *s, int connfd)
{
    if(write(connfd, s, strlen(s))<0)
    {
        perror("write");
        exit(-1);
    }
}

/* Send message to client */
void send_message_client(char *s, int uid)
{
    int i;
    for(i=0; i<MAX_CLIENTS; i++)
    {
        if(clients[i])
        {
            if(clients[i]->uid == uid)
            {
                if(write(clients[i]->connfd, s, strlen(s))<0)
                {
                    perror("write");
                    exit(-1);
                }
            }
        }
    }
}

/* Send list of online clients */
void send_online_clients(int connfd)
{
    int i;
    char s[64];
    for(i=0; i<MAX_CLIENTS; i++)
    {
        if(clients[i])
        {
            sprintf(s, "CLIENT %d | %s\r\n", clients[i]->uid, clients[i]->name);
            send_message_self(s, connfd);
        }
    }
}

/* Strip CRLF */
void strip_newline(char *s)
{
    while(*s != '\0')
    {
        if(*s == '\r' || *s == '\n')
        {
            *s = '\0';
        }
        s++;
    }
}
