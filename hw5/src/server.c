#include "server.h"
#include "debug.h"
#include "client_registry.h"
#include "protocol.h"
#include "data.h"
#include "store.h"
#include <transaction.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

CLIENT_REGISTRY *client_registry;

typedef struct client_registry {
    int fd;

    struct {
        struct client_registry *next;
        struct client_registry *prev;
    } links;
}CLIENT_REGISTRY;


void freeStructures(int fdClient, TRANSACTION *thisTransaction, XACTO_PACKET *pkt, void **datap);
void resetdataptr(void **datap);
void initdatap(void **datap);

void *xacto_client_service(void *arg)
{
    //retrieve the int
    int fdClient = *(int*)arg;

    //free the storage
    free(arg);

    //detach the thread
    if(pthread_detach(pthread_self()) != 0)
    {
        debug("error in de_taching thread");
    }

    //register file descriptor with client registry
    creg_register(client_registry, fdClient);

    //create a transaction
    TRANSACTION *thisTransaction = trans_create();

    int done = 0;
    int abort = 0;
    void **datap = calloc(1, sizeof(void*)*3);
    initdatap(datap);
    void *datapInitial = datap;
    XACTO_PACKET *pkt = calloc(1, sizeof(XACTO_PACKET));
    while(done == 0)
    {
        //make sure the datapointer is always at the beginning
        datap = datapInitial;
        //free datap every loop

        //get the 3 request packets
        //get the first reply packet
        if(proto_recv_packet(fdClient, pkt, datap) != 0)
        {
            //send the required reply and abort
            trans_abort(thisTransaction);
            abort = 1;
            done = 1;
        }
        else
        {
            datap++;
            if((*pkt).type == XACTO_GET_PKT)
            {
                //GET THE OTHER 2 PACKETS
                if(proto_recv_packet(fdClient, pkt, datap) != 0)
                {
                    //send the required reply and abort
                    trans_abort(thisTransaction);
                    abort = 1;
                    done = 1;
                }

                //create key
                BLOB *keyBlob = calloc(1, sizeof(BLOB));
                keyBlob = blob_create((char*)(*datap), (*pkt).size);
                KEY *theKey = calloc(1, sizeof(KEY));
                theKey = key_create(keyBlob);

                //set the datapointer to the beginning
                datap = datapInitial;

                //DO THE STORE CRAP
                BLOB **blobptr = calloc(1, sizeof(BLOB*));
                store_get(thisTransaction, theKey, blobptr);

                //check for an abort
                if(trans_get_status(thisTransaction) == TRANS_ABORTED)
                {
                    trans_abort(thisTransaction);
                    abort = 1;
                    done = 1;
                }
                //send a reply packet
                struct timespec ts;
                clock_gettime(CLOCK_MONOTONIC, &ts);
                (*pkt).type = XACTO_REPLY_PKT;
                (*pkt).status = trans_get_status(thisTransaction);
                (*pkt).null = 1;
                (*pkt).size = 0;
                (*pkt).timestamp_sec = ts.tv_sec;
                (*pkt).timestamp_nsec = ts.tv_nsec;
                free(*datap);
                *datap = NULL;
                if(proto_send_packet(fdClient, pkt, *datap) != 0)
                {
                    trans_abort(thisTransaction);
                    abort = 1;
                    done = 1;
                }

                //send a data packet with payload
                BLOB *theBlob = *blobptr;
                if(*blobptr == NULL)
                {
                    //SEND A NULL DATA PACKET
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    (*pkt).type = XACTO_DATA_PKT;
                    (*pkt).status = trans_get_status(thisTransaction);
                    (*pkt).null = 1;
                    (*pkt).size = 0;
                    (*pkt).timestamp_sec = ts.tv_sec;
                    (*pkt).timestamp_nsec = ts.tv_nsec;

                    if(proto_send_packet(fdClient, pkt, *blobptr) != 0)
                    {
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }
                }
                else
                {
                    //SEND THE CONTENT
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    (*pkt).type = XACTO_DATA_PKT;
                    (*pkt).status = trans_get_status(thisTransaction);
                    (*pkt).null = 0;
                    (*pkt).size = (*theBlob).size;
                    (*pkt).timestamp_sec = ts.tv_sec;
                    (*pkt).timestamp_nsec = ts.tv_nsec;

                    if(proto_send_packet(fdClient, pkt, (*theBlob).content) != 0)
                    {
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }
                }

                //FREE THE STRUCTURES SPECIFIC TO THIS
                *blobptr = NULL;
                free(blobptr);
            }
            else
            {
                if((*pkt).type == XACTO_PUT_PKT)
                {
                    //get the values
                    if(proto_recv_packet(fdClient, pkt, datap) != 0)
                    {
                        //send the required reply and abort
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }
                    debug("put key %s \n.",(char*)*datap);
                    //create key
                    BLOB *keyBlob = calloc(1, sizeof(BLOB));
                    keyBlob = blob_create((char*)(*datap), (*pkt).size);
                    KEY *theKey = calloc(1, sizeof(KEY));
                    theKey = key_create(keyBlob);

                    datap++;
                    //get next value
                    if(proto_recv_packet(fdClient, pkt, datap) != 0)
                    {
                        //send the required reply and abort
                        free(keyBlob);
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }

                    debug("put data %s \n.",(char*)*datap);

                    //create data blob
                    BLOB *dataBlob = calloc(1, sizeof(BLOB));
                    dataBlob = blob_create((char*)(*datap), (*pkt).size);

                    //put it in the store
                    store_put(thisTransaction, theKey, dataBlob);

                    //check for an abort
                    if(trans_get_status(thisTransaction) == TRANS_ABORTED)
                    {
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }

                    //show store
                    store_show();

                    //send a reply packet
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    (*pkt).type = XACTO_REPLY_PKT;
                    (*pkt).status = trans_get_status(thisTransaction);
                    (*pkt).null = 1;
                    (*pkt).size = 0;
                    (*pkt).timestamp_sec = ts.tv_sec;
                    (*pkt).timestamp_nsec = ts.tv_nsec;
                    free(*datap);
                    *datap = NULL;
                    if(proto_send_packet(fdClient, pkt, *datap) != 0)
                    {
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }
                }
                else
                {
                    if((*pkt).type == XACTO_COMMIT_PKT)
                    {
                        if(trans_get_status(thisTransaction) == TRANS_ABORTED)
                        {
                            trans_abort(thisTransaction);
                            abort = 1;
                            done = 1;
                        }
                        else
                        {
                            if(trans_commit(thisTransaction) == TRANS_COMMITTED)
                            {
                                //send a reply packet
                                struct timespec ts;
                                clock_gettime(CLOCK_MONOTONIC, &ts);
                                (*pkt).type = XACTO_REPLY_PKT;
                                (*pkt).status = trans_get_status(thisTransaction);
                                (*pkt).null = 1;
                                (*pkt).size = 0;
                                (*pkt).timestamp_sec = ts.tv_sec;
                                (*pkt).timestamp_nsec = ts.tv_nsec;
                                datap = datapInitial;
                                debug("commit  %s \n.",(char*)*datap);
                                free(*datap);
                                *datap = NULL;

                                //show store
                                store_show();

                                if(proto_send_packet(fdClient, pkt, *datap) != 0)
                                {
                                    trans_abort(thisTransaction);
                                    abort = 1;
                                    done = 1;
                                }

                                //commit
                                done = 1;
                            }
                            else
                            {
                                //abort happened
                                trans_abort(thisTransaction);
                                abort = 1;
                                done = 1;
                            }
                        }
                    }
                    else
                    {
                        //if packet is none of those then abort
                        trans_abort(thisTransaction);
                        abort = 1;
                        done = 1;
                    }
                }
            }
            //free the structures
            datap = datapInitial;
            resetdataptr(datap);
        }
    }

    if(abort == 1)
    {
        //abort happened
        datap = datapInitial;
        free(*datap);
        *datap = NULL;
        //send a abort packet
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        (*pkt).type = XACTO_REPLY_PKT;
        (*pkt).status = trans_get_status(thisTransaction);
        (*pkt).null = 1;
        (*pkt).size = 0;
        (*pkt).timestamp_sec = ts.tv_sec;
        (*pkt).timestamp_nsec = ts.tv_nsec;
        if(proto_send_packet(fdClient, pkt, *datap) != 0)
        {
            done = 1;
        }
        //free the stuff
        datap = datapInitial;
        freeStructures(fdClient, thisTransaction, pkt, datap);
    }
    else
    {
        //commit happened already
        datap = datapInitial;
        freeStructures(fdClient, thisTransaction, pkt, datap);
    }
    close(fdClient);
    return NULL;
}

void freeStructures(int fdClient, TRANSACTION *thisTransaction, XACTO_PACKET *pkt, void **datap)
{
    free(pkt);
    free(datap[2]);
    free(datap[1]);
    free(datap[0]);
    free(datap);
    creg_unregister(client_registry, fdClient);
}

void resetdataptr(void **datap)
{
    free(datap[2]);
    free(datap[1]);
    free(datap[0]);
    datap[2] = NULL;
    datap[1] = NULL;
    datap[0] = NULL;
}

void initdatap(void **datap)
{
    *datap = NULL;
    datap++;
    *datap = NULL;
    datap++;
    *datap = NULL;
}