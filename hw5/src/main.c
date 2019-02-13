#include "debug.h"
#include "client_registry.h"
#include "server.h"
#include "transaction.h"
#include "store.h"
#include "csapp.h"
#include <unistd.h>
#include <signal.h>

static void terminate(int status);
void *thread(void *vargp);
void sighup_handler(int sig);

CLIENT_REGISTRY *client_registry;

int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    extern char *optarg;
    extern int optind;
    int c = 0;
    int errflag = 0;
    int hflag = 0;
    int pflag = 0;
    int qflag = 0;
    char *pflagCommand;
    char* hflagCommand;

    while ((c = getopt(argc, argv, "p:qh:")) != -1)
    {
        switch (c)
        {
            case 'p':
            if(pflag == 1)
            {
                //PRINT ERROR CANNOT HAVE MULTIPLE p-flags
            }
            pflag = 1;
            pflagCommand = optarg;
            break;

            case 'q':
            qflag = 1;
            break;

            case 'h':
            hflag = 1;
            hflagCommand = optarg;
            break;

            case '?':
            errflag = 1;
            break;
        }
    }

    //ERROR CHECKING COMMANDS PASSED
    if(pflag != 1)
    {
        //PRINT ERROR
        exit(1);
    }
    else
    {
        if(errflag == 1)
        {
            //PRINT ERROR
            exit(1);
        }
    }

    if(hflag == 1)
    {
        printf("%s \n", hflagCommand);
    }

    if(qflag == 1)
    {

    }




    // Perform required initializations of the client_registry,
    // transaction manager, and object store.
    client_registry = creg_init();
    trans_init();
    store_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function xacto_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.

    //CREATE THE SIGHUP HANDLER
    sigset_t block_mask;
    sigemptyset(&block_mask);
    struct sigaction sigact;
    sigact.sa_handler = sighup_handler;
    sigact.sa_mask = block_mask;
    sigact.sa_flags = 0;
    if (sigaction(SIGHUP, &sigact, NULL) == -1)
    {
        terminate(EXIT_FAILURE);
    }

    int listenfd;
    int *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    listenfd = open_listenfd(pflagCommand);

    while(1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen);//connfdp = socket of the client
        Pthread_create(&tid, NULL, xacto_client_service, connfdp);
    }

    //terminate(EXIT_SUCCESS);

    //fprintf(stderr, "You have to finish implementing main() "
	//    "before the Xacto server will function.\n");

    //terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    // Shutdown all client connections.
    // This will trigger the eventual termination of service threads.
    creg_shutdown_all(client_registry);

    debug("Waiting for service threads to terminate...");
    creg_wait_for_empty(client_registry);
    debug("All service threads terminated.");

    // Finalize modules.
    creg_fini(client_registry);
    trans_fini();
    store_fini();

    debug("Xacto server terminating");
    exit(status);
}

/*void *thread(void *vargp)
{
    int connfd = *((int*) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    xacto_client_service(connfd);
    Close(connfd);
    return NULL;
}*/

void sighup_handler(int sig)
{
    terminate(EXIT_SUCCESS);
}