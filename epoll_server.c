#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 2048

struct sendQueueNode
{
    char *message;
    struct sendQueueNode* nextMessage;
};

struct fdInfo
{
    int fd;
    int readIndex;
    char readBuffer[BUFFER_SIZE];
    int sendIndex;
    struct sendQueueNode *firstMessage;
    struct sendQueueNode *lastMessage;
} socketsInfo[MAX_CLIENTS];

void set_socket_non_blocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        exit(EXIT_FAILURE);
    }
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL O_NONBLOCK");
        exit(EXIT_FAILURE);
    }
}

void addMessageToQueue(int socketIndex, char* message)
{
    if(socketsInfo[socketIndex].firstMessage == NULL)
    {
        socketsInfo[socketIndex].firstMessage = malloc(sizeof(struct sendQueueNode));
        socketsInfo[socketIndex].lastMessage = socketsInfo[socketIndex].firstMessage;
    }
    else
    {
        socketsInfo[socketIndex].lastMessage->nextMessage = malloc(sizeof(struct sendQueueNode));
        socketsInfo[socketIndex].lastMessage = socketsInfo[socketIndex].lastMessage->nextMessage;

    }

    socketsInfo[socketIndex].lastMessage->message = message;
    socketsInfo[socketIndex].lastMessage->nextMessage = NULL;
}

void deletMessageFromQueue(int socketIndex)
{
    struct sendQueueNode *temp = socketsInfo[socketIndex].firstMessage;
    socketsInfo[socketIndex].firstMessage = socketsInfo[socketIndex].firstMessage->nextMessage;

    free(temp->message);
    free(temp);

    if(socketsInfo[socketIndex].firstMessage == NULL)
    {
        socketsInfo[socketIndex].lastMessage = NULL;
    }
}

void handle_write_event(int socketIndex);

int handle_read_event(int socketIndex)
{
    int socketFD = socketsInfo[socketIndex].fd;
    int readIndex = socketsInfo[socketIndex].readIndex;
    while(1)
    {
        char charRead;
        int readReturn = read(socketFD, &charRead, 1);

        if(readReturn == -1)
        {
            if(errno != EAGAIN)
            {
                printf("Error reading on socket %d", socketFD);
            }
            socketsInfo[socketIndex].readIndex = readIndex;
            return 0;
        }
        if(readReturn == 0)
        {
            printf("Client on socket %d has disconnected", socketFD);
            return 1;
        }

        if(readIndex < BUFFER_SIZE -1)
        {
            socketsInfo[socketIndex].readBuffer[readIndex] = charRead;
        }
        else
        {
            printf("Buffer for socket %d is full", socketFD);
            return -1;
        }
        if(charRead == '\n')
        {
            socketsInfo[socketIndex].readBuffer[readIndex + 1] = '\0';
            for(int i = 0; i < MAX_CLIENTS; i++)
            {
                if(socketsInfo[i].fd != -1 && socketsInfo[i].fd != socketFD)
                {
                    addMessageToQueue(i, strdup(socketsInfo[socketIndex].readBuffer));
                    handle_write_event(i);
                }
            }
            readIndex = 0;
        }
        else
        {
            readIndex += 1;
        }
    }
}

void handle_write_event(int socketIndex)
{
    int socketFD = socketsInfo[socketIndex].fd;
    int sendIndex = socketsInfo[socketIndex].sendIndex;
    while(1)
    {
        int messageSize = strlen(socketsInfo[socketIndex].firstMessage->message) - sendIndex;
        int bytesSent = send(socketFD, socketsInfo[socketIndex].firstMessage->message + sendIndex, messageSize, 0);

        if(bytesSent == messageSize)
        {
            deletMessageFromQueue(socketIndex);
            sendIndex = 0;
            if (socketsInfo[socketIndex].firstMessage == NULL)
            {
                break;
            }
        }
        else
        {
            if(bytesSent == -1)
            {
                if(errno != EAGAIN)
                {
                    printf("Error sending on socket %d", socketFD);
                }
            }
            socketsInfo[socketIndex].sendIndex = sendIndex;
            break;
        }
    }
}

int main (int argc, char* argv[])
{
    int server_socket, new_socket, c;
    struct sockaddr_in server, client;
    char *pvalue = NULL;
    int port;

    printf("PID: %d\n", getpid());

    while ((c = getopt(argc, argv, "p:")) != -1) {
        switch (c) {
            case 'p':
                pvalue = optarg;
                break;
            case '?':
                if (optopt == 'p')
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else
                    fprintf(stderr, "Unknown option character `%c'.\n", optopt);
                return 1;
            default:
                abort();
            }
    }

    if (pvalue == NULL) {
        fprintf(stderr, "Usage: %s -p port\n", argv[0]);
        return 1;
    }

    port = atoi(pvalue);

    // Create socket
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Could not create socket");
        return 1;
    }

    int opt = 1;
    // Set SO_REUSEADDR to allow local address reuse
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(port);

    // Bind
    if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("bind failed. Error");
        return 1;
    }

    // Listen
    listen(server_socket, MAX_CLIENTS);

    // Accept incoming connections
    puts("Waiting for incoming connections...");

    const int MAX_EVENTS = MAX_CLIENTS + 1;
    struct epoll_event event;
    struct epoll_event *events = calloc(MAX_EVENTS, sizeof(struct epoll_event));
    int epollFD = epoll_create1(0);

    event.data.fd = server_socket;
    event.events = EPOLLIN;

    int s = epoll_ctl(epollFD, EPOLL_CTL_ADD, server_socket, &event);
    if(s == -1)
    {
        perror("epoll");
    }

    memset(socketsInfo, 0, sizeof(socketsInfo));
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        socketsInfo[i].fd = -1;
    }

    while(1)
    {
        int num_events = epoll_wait(epollFD, events, MAX_EVENTS, -1);
        for(int i = 0; i < num_events; i++)
        {
            //New client is ready to be accepted
            if(events[i].data.fd == server_socket){
                int client_size = sizeof(struct sockaddr_in);
                new_socket = accept(server_socket, (struct sockaddr*)&client, (socklen_t*)&client_size);
                if(new_socket < 0)
                {
                    perror("accept");
                    continue;
                }
                event.data.fd = new_socket;
                event.events = EPOLLIN | EPOLLOUT | EPOLLET;
                if (epoll_ctl(epollFD, EPOLL_CTL_ADD, new_socket, &event) == -1)
                {
                    perror("epoll_ctl");
                }
                set_socket_non_blocking(new_socket);

                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    if(socketsInfo[j].fd == -1)
                    {
                        socketsInfo[j].fd = new_socket;
                        break;
                    }
                }
            }

            else
            {
                int socketIndex = -1;
                for (int j = 0; j < MAX_CLIENTS; j++)
                {
                    if(events[i].data.fd == socketsInfo[j].fd)
                    {
                        socketIndex = j;
                        break;
                    }
                }
                //Read event
                if(events[i].events & EPOLLIN)
                {
                    handle_read_event(socketIndex);
                }
                //Write event
                if((events[i].events & EPOLLOUT) && socketsInfo[socketIndex].firstMessage != NULL)
                {
                    handle_write_event(socketIndex);
                }
            }

        }
    }


    return 0;
}
