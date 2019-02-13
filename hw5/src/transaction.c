#include "transaction.h"
#include "debug.h"
#include <stdlib.h>

void freeDependencies(DEPENDENCY *depends);
void unrefDependencies(TRANSACTION *tp);

int idCounter;

void trans_init(void)
{
    debug("initialized transactions");
    trans_list.id = -1;
    trans_list.depends = NULL;
    trans_list.next = &trans_list;
    trans_list.prev = &trans_list;
    idCounter = 0;
}

void trans_fini(void)
{
    int done = 0;
    while(done != 1)
    {
        TRANSACTION *transptr = &trans_list;
        while((*transptr).next != &trans_list)
        {
            transptr = (*transptr).next;
        }
        if(transptr == &trans_list)
        {
            debug("trans_fini freed initial node");
            (*transptr).next = NULL;
            (*transptr).prev = NULL;
            free(transptr);
            done = 1;
        }
        else
        {
            //set the prev transaction's next node to sentinel
            debug("trans_fini free node");
            TRANSACTION *prevTrans = (*transptr).prev;
            (*prevTrans).next = &trans_list;

            //free everything
            sem_t transSemaphore = (*transptr).sem;
            pthread_mutex_t *transMutex = &(*transptr).mutex;
            freeDependencies((*transptr).depends);
            (*transptr).depends = NULL;
            (*transptr).next = NULL;
            (*transptr).prev = NULL;
            if(sem_destroy(&transSemaphore) == -1)
            {
                debug("transaction semaphore destroy failure");
            }
            pthread_mutex_unlock(transMutex);
            pthread_mutex_destroy(transMutex);
            free(transptr);
        }
    }
}

TRANSACTION *trans_create(void)
{
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    sem_t semaphore;
    sem_init(&semaphore, 0, 0);
    TRANSACTION *theTransaction = calloc(1, sizeof(TRANSACTION));
    (*theTransaction).id = idCounter;
    idCounter++;
    (*theTransaction).refcnt = 1;
    (*theTransaction).status = TRANS_PENDING;
    (*theTransaction).depends = NULL;
    (*theTransaction).waitcnt = 0;
    (*theTransaction).sem = semaphore;
    (*theTransaction).mutex = mutex;
    (*theTransaction).next = &trans_list;
    TRANSACTION *prevTrans = trans_list.prev;
    (*prevTrans).next = theTransaction;
    (*theTransaction).prev = prevTrans;
    trans_list.prev = theTransaction;
    return theTransaction;
}

TRANSACTION *trans_ref(TRANSACTION *tp, char *why)
{
    pthread_mutex_t *mutex = &(*tp).mutex;
    pthread_mutex_lock(mutex);
    (*tp).refcnt = (*tp).refcnt + 1;
    debug("new increased reference count: %d \n.",(*tp).refcnt);
    debug("reaason why : %s \n.", why);
    pthread_mutex_unlock(mutex);
    return tp;
}

void trans_unref(TRANSACTION *tp, char *why)
{
    pthread_mutex_t *mutex = &(*tp).mutex;
    pthread_mutex_lock(mutex);
    (*tp).refcnt = (*tp).refcnt - 1;
    debug("new decreased reference count: %d \n.",(*tp).refcnt);
    if((*tp).refcnt == 0)
    {
        sem_t transSemaphore = (*tp).sem;
        freeDependencies((*tp).depends);
        TRANSACTION *prevTrans = (*tp).prev;
        TRANSACTION *nextTrans = (*tp).next;
        (*prevTrans).next = nextTrans;
        (*nextTrans).prev = prevTrans;
        (*tp).depends = NULL;
        (*tp).next = NULL;
        (*tp).prev = NULL;
        if(sem_destroy(&transSemaphore) == -1)
        {
            debug("transaction semaphore destroy failure");
        }
        pthread_mutex_unlock(mutex);
        pthread_mutex_destroy(mutex);
        free(tp);
    }
    else
    {
        pthread_mutex_unlock(mutex);
    }
}

void trans_add_dependency(TRANSACTION *tp, TRANSACTION *dtp)
{
    DEPENDENCY *dependency = calloc(1, sizeof(DEPENDENCY));
    (*dependency).trans = dtp;
    (*dependency).next = NULL;
    if((*tp).depends == NULL)
    {
        (*tp).depends = dependency;
        trans_ref(dtp,  "added dependecy");
    }
    else
    {
        DEPENDENCY *dependencyptr = (*tp).depends;
        while((*dependencyptr).next != NULL)
        {
            if((*dependencyptr).trans == dtp)
            {
                //already contained the dependee
                return;
            }
            dependencyptr = (*dependencyptr).next;
        }
        (*dependencyptr).next = dependency;
        trans_ref(dtp,  "added dependecy");
    }
    //increment waitcnt of the dependency being added
    pthread_mutex_t *dtpMutex = &(*dtp).mutex;
    pthread_mutex_lock(dtpMutex);
    (*dtp).waitcnt = (*dtp).waitcnt + 1;
    pthread_mutex_unlock(dtpMutex);
}

TRANS_STATUS trans_commit(TRANSACTION *tp)
{
    DEPENDENCY *dependencyptr = (*tp).depends;
    while(dependencyptr != NULL)
    {
        TRANSACTION *transactionptr = (*dependencyptr).trans;
        sem_t* semaphore = &(*transactionptr).sem;
        int semint;
        sem_getvalue(semaphore, &semint);
        sem_wait(semaphore);
        //check if transaction has aborted
        if(trans_get_status(transactionptr) == TRANS_ABORTED)
        {
            //set itself to abort
            trans_ref(tp, "one of the dependencies aborted");
            trans_abort(tp);
            return trans_get_status(tp);
        }
        dependencyptr = (*dependencyptr).next;
    }
    //all transactions committed
    //unref the dependencies
    unrefDependencies(tp);
    (*tp).status = TRANS_COMMITTED;
    sem_t* thisSemaphore = &(*tp).sem;
    int refCounter = 0;
    while(refCounter < (*tp).waitcnt)
    {
        sem_post(thisSemaphore);
        refCounter++;
    }
    int semint;
    sem_getvalue(thisSemaphore, &semint);
    trans_unref(tp, "trans committed");
    return trans_get_status(tp);
}

TRANS_STATUS trans_abort(TRANSACTION *tp)
{
    if(trans_get_status(tp) == TRANS_COMMITTED)
    {
        //fatal error
        abort();
    }
    if(trans_get_status(tp) == TRANS_ABORTED)
    {
        //do nothing
        return TRANS_ABORTED;
    }
    (*tp).status = TRANS_ABORTED;
    sem_t thisSemaphore = (*tp).sem;
    int refCounter = 0;
    while(refCounter < (*tp).waitcnt)
    {
        sem_post(&thisSemaphore);
        refCounter++;
    }
    unrefDependencies(tp);
    trans_unref(tp, "transaction aborted");
    return TRANS_ABORTED;
}

void trans_show(TRANSACTION *tp)
{
    int semvalue;
    sem_t semaphore = (*tp).sem;
    sem_getvalue(&semaphore, &semvalue);
    debug("id: %u \n.", (*tp).id);
    debug("refcnt: %u \n.", (*tp).refcnt);
    debug("status: %d \n.", (*tp).status);
    debug("depends: %p \n.", (*tp).depends);
    debug("waitcnt: %d \n.", (*tp).waitcnt);
    debug("sem: %d \n.", semvalue);
    //debug("mutex: %p \n.", (*tp).mutex);
    debug("next: %p \n.", (*tp).next);
    debug("prev: %p \n.", (*tp).prev);
}

void trans_show_all(void)
{
    TRANSACTION *transptr = trans_list.next;
    while(transptr != &trans_list)
    {
        trans_show(transptr);
    }
}


TRANS_STATUS trans_get_status(TRANSACTION *tp)
{
    pthread_mutex_t* mutex = &(*tp).mutex;
    pthread_mutex_lock(mutex);
    TRANS_STATUS status = (*tp).status;
    pthread_mutex_unlock(mutex);
    return status;
}


void freeDependencies(DEPENDENCY *depends)
{
    DEPENDENCY *dependencyptr;
    while(depends != NULL)
    {
        dependencyptr = depends;
        depends = (*depends).next;
        (*dependencyptr).trans = NULL;
        (*dependencyptr).next = NULL;
        free(dependencyptr);
    }
}

void unrefDependencies(TRANSACTION *tp)
{
    DEPENDENCY *dependencyptr = (*tp).depends;
    while(dependencyptr != NULL)
    {
        TRANSACTION *transactionptr = (*dependencyptr).trans;
        trans_unref(transactionptr, "committed/aborted dependency");
        dependencyptr = (*dependencyptr).next;
    }
}