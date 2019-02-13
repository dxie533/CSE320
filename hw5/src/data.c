#include "data.h"
#include "debug.h"
#include "csapp.h"
#include <pthread.h>

BLOB *blob_create(char *content, size_t size)
{
    /*BLOB *theBlob = calloc(1, sizeof(BLOB));
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    char *prefixCopy = calloc(1, sizeof(content)*(strlen(content) + 1));
    strcpy(prefixCopy, content);
    char *contentCopy = calloc(1, size);
    memcpy(contentCopy, content, size);
    (*theBlob).mutex = mutex;
    (*theBlob).refcnt = 1;
    (*theBlob).size = size;
    (*theBlob).content = contentCopy;
    (*theBlob).prefix = prefixCopy;
    return theBlob;*/
    BLOB *theBlob = calloc(1, sizeof(BLOB));
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    if(content == NULL)
    {
        (*theBlob).prefix = NULL;
        (*theBlob).content = NULL;
        (*theBlob).size = 0;
    }
    else
    {
        char *prefixCopy = calloc(1, size + 1);
        memcpy(prefixCopy, content, size);
        (*theBlob).prefix = prefixCopy;
        char *contentCopy = calloc(1, size);
        memcpy(contentCopy, content, size);
        (*theBlob).content = contentCopy;
        (*theBlob).size = size;
    }
    (*theBlob).mutex = mutex;
    (*theBlob).refcnt = 1;
    return theBlob;
}

BLOB *blob_ref(BLOB *bp, char *why)
{
    pthread_mutex_t *mutex = &(*bp).mutex;
    pthread_mutex_lock(mutex);
    (*bp).refcnt = (*bp).refcnt + 1;
    debug("reaason why : %s \n.", why);
    pthread_mutex_unlock(mutex);
    return bp;
}

void blob_unref(BLOB *bp, char *why)
{
    /*pthread_mutex_t *mutex = &(*bp).mutex;
    pthread_mutex_lock(mutex);
    (*bp).refcnt = (*bp).refcnt - 1;
    debug("reaason why : %s \n.", why);
    if((*bp).refcnt == 0)
    {
        free((*bp).prefix);
        free((*bp).content);
        pthread_mutex_unlock(mutex);
        pthread_mutex_destroy(mutex);
        free(bp);
    }
    else
    {
        pthread_mutex_unlock(mutex);
    }*/
    pthread_mutex_t *mutex = &(*bp).mutex;
    pthread_mutex_lock(mutex);
    (*bp).refcnt = (*bp).refcnt - 1;
    debug("reaason why : %s \n.", why);
    if((*bp).refcnt == 0)
    {
        if((*bp).content == NULL)
        {
            pthread_mutex_unlock(mutex);
            pthread_mutex_destroy(mutex);
            free(bp);
        }
        else
        {
            free((*bp).prefix);
            free((*bp).content);
            pthread_mutex_unlock(mutex);
            pthread_mutex_destroy(mutex);
            free(bp);
        }
    }
    else
    {
        pthread_mutex_unlock(mutex);
    }
}

int blob_compare(BLOB *bp1, BLOB *bp2)
{
    int hash1 = blob_hash(bp1);
    int hash2 = blob_hash(bp2);
    if(hash1 == hash2)
    {
        if(memcmp((*bp1).content, (*bp2).content, (*bp2).size) == 0)
        {
            return 0;
        }
    }
    return -1;
}

int blob_hash(BLOB *bp)
{
    unsigned int hash = 0;
    unsigned int position = 0;
    int temp;
    char *string = (*bp).content;

    while (*string != '\0')
    {
        temp = *string;
        hash = hash + (position * temp);
        string++;
        position++;
    }
    return hash;
}

KEY *key_create(BLOB *bp)
{
    pthread_mutex_t *mutex = &(*bp).mutex;
    pthread_mutex_lock(mutex);
    int hashed = blob_hash(bp);
    KEY *theKey = calloc(1, sizeof(KEY));
    (*theKey).hash = hashed;
    (*theKey).blob = bp;
    pthread_mutex_unlock(mutex);
    return theKey;
}

void key_dispose(KEY *kp)
{
    blob_unref((*kp).blob, "dispose key");
    (*kp).blob = NULL;
    free(kp);
}

int key_compare(KEY *kp1, KEY *kp2)
{
    if(blob_compare((*kp1).blob, (*kp2).blob) == 0)
    {
        if((*kp1).hash == (*kp2).hash)
        {
            return 0;
        }
    }
    return -1;
}

VERSION *version_create(TRANSACTION *tp, BLOB *bp)
{
    VERSION *theVersion = calloc(1, sizeof(VERSION));
    (*theVersion).creator = tp;
    (*theVersion).blob = bp;
    (*theVersion).next = NULL;
    (*theVersion).prev = NULL;
    trans_ref(tp, "version create called");
    return theVersion;
}

void version_dispose(VERSION *vp)
{
    trans_unref((*vp).creator, "version dispose called");
    blob_unref((*vp).blob, "version dispose called");
    (*vp).creator = NULL;
    (*vp).blob = NULL;
    (*vp).next = NULL;
    (*vp).prev = NULL;
    free(vp);
}