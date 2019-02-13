#include "client_registry.h"
#include "debug.h"
#include "csapp.h"
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

typedef struct client_registry {
    int fd;

    struct {
        struct client_registry *next;
        struct client_registry *prev;
    } links;
}CLIENT_REGISTRY;

pthread_mutex_t mutex;
int clientCount;
sem_t noClientsMutex;

CLIENT_REGISTRY *creg_init()
{
    debug("creg_init");
    pthread_mutex_init(&mutex, NULL);
    Sem_init(&noClientsMutex, 0, 1);
    clientCount = 0;
    CLIENT_REGISTRY *clientptr = malloc(sizeof(CLIENT_REGISTRY));
    (*clientptr).fd = -1;
    (*clientptr).links.next = NULL;
    (*clientptr).links.prev = NULL;
    return clientptr;
}

void creg_fini(CLIENT_REGISTRY *cr)
{
    debug("creg_fini");
    pthread_mutex_lock(&mutex);
    int done = 0;
    while(done != 1)
    {
        CLIENT_REGISTRY *crPtr = cr;
        while((*crPtr).links.next != NULL)
        {
            crPtr = (*crPtr).links.next;
        }
        if(crPtr == cr)
        {
            debug("creg_fini freed initial node");
            (*crPtr).links.next = NULL;
            (*crPtr).links.prev = NULL;
            free(crPtr);
            done = 1;
        }
        else
        {
            debug("creg_fini_freed node");
            CLIENT_REGISTRY *prevClient = (*crPtr).links.prev;
            (*prevClient).links.next = NULL;
            (*crPtr).links.next = NULL;
            (*crPtr).links.prev = NULL;
            free(crPtr);
        }
    }
    pthread_mutex_unlock(&mutex);
    pthread_mutex_destroy(&mutex);
}

void creg_register(CLIENT_REGISTRY *cr, int fd)
{
    debug("creg_register");
    pthread_mutex_lock(&mutex);
    clientCount = clientCount + 1;
    if(clientCount == 1)
    {
        P(&noClientsMutex);
    }
    while((*cr).links.next != NULL)
    {
        cr = (*cr).links.next;
    }
    CLIENT_REGISTRY *clientptr = malloc(sizeof(CLIENT_REGISTRY));
    (*clientptr).fd = fd;
    (*clientptr).links.next = NULL;
    (*clientptr).links.prev = cr;
    (*cr).links.next = clientptr;
    pthread_mutex_unlock(&mutex);
}

void creg_unregister(CLIENT_REGISTRY *cr, int fd)
{
    debug("creg_unregister");
    pthread_mutex_lock(&mutex);
    int done = 0;
    while(cr != NULL && done == 0)
    {
        if(fd == (*cr).fd)
        {
            close(fd);
            CLIENT_REGISTRY *prev = (*cr).links.prev;
            CLIENT_REGISTRY *next = (*cr).links.next;
            if(prev != NULL)
            {
                (*prev).links.next = next;
            }
            if(next != NULL)
            {
                (*next).links.prev = prev;
            }
            (*cr).links.prev = NULL;
            (*cr).links.next = NULL;
            free(cr);
            done = 1;
        }
        else
        {
            cr = (*cr).links.next;
        }
    }
    clientCount = clientCount - 1;
    if(clientCount == 0)
    {
        V(&noClientsMutex);
    }
    pthread_mutex_unlock(&mutex);
}

void creg_wait_for_empty(CLIENT_REGISTRY *cr)
{
    debug("creg_wait");
    P(&noClientsMutex);
    debug("creg_wait done");
    return;
}

void creg_shutdown_all(CLIENT_REGISTRY *cr)
{
    debug("creg_shutdown");
    pthread_mutex_lock(&mutex);
    while((*cr).links.next != NULL)
    {
        if((*cr).fd != -1)
        {
            if(shutdown((*cr).fd, SHUT_RDWR) == -1)
            {
                debug("shutdown failed");
            }
        }
        cr = (*cr).links.next;
    }
    pthread_mutex_unlock(&mutex);
}