#include "store.h"
#include "debug.h"
#include <stdlib.h>

void initMapTable(MAP_ENTRY **table);
void freeTheTable(MAP_ENTRY **table);
void freeMap(MAP_ENTRY *table);
void freeVersions(VERSION *version);
void addMapEntryToIndex(int tableIndex, MAP_ENTRY *newEntry);
void deleteMapping(MAP_ENTRY *mapnode, int tableIndex);
void removeFurtherVersions(VERSION *version);
void collectGarbage(VERSION *version, MAP_ENTRY *mapnode);
int transactionIdCheck(TRANSACTION *tp, VERSION *version);
void placeTransactionLast(TRANSACTION *tp, BLOB *value, VERSION *version);
void replaceTransactionLast(TRANSACTION *tp, BLOB *value, VERSION *version);
void dependOnEarlierVersions(VERSION *newVersion, VERSION *version);
void copyTransactionLast(TRANSACTION *tp, BLOB **value, VERSION *version);
void getValueTransactionLast(TRANSACTION *tp, BLOB **value, VERSION *version);

struct map *the_actual_map;

void store_init(void)
{
    the_actual_map = malloc(sizeof(struct map));
    MAP_ENTRY **table = calloc(8, sizeof(MAP_ENTRY*));
    initMapTable(table);
    pthread_mutex_t mutex;
    pthread_mutex_init(&mutex, NULL);
    the_actual_map->table = table;
    the_actual_map->num_buckets = NUM_BUCKETS;
    the_actual_map->mutex = mutex;
}

void store_fini(void)
{
    freeTheTable(the_actual_map->table);
    pthread_mutex_unlock(&the_actual_map->mutex);
    pthread_mutex_destroy(&the_actual_map->mutex);
}

TRANS_STATUS store_put(TRANSACTION *tp, KEY *key, BLOB *value)
{
    pthread_mutex_lock(&the_actual_map->mutex);
    if(key == NULL)
    {
        //key must not be null
        trans_abort(tp);
        pthread_mutex_unlock(&the_actual_map->mutex);
        return trans_get_status(tp);
    }

    if(value == NULL)
    {
        //delete the mapping of the key

        int hash = (*key).hash;
        int tableIndex = hash % NUM_BUCKETS;

        //check if there is an entry
        MAP_ENTRY *mapnode = the_actual_map->table[tableIndex];
        while(mapnode != NULL)
        {
            if(key_compare(key, (*mapnode).key) == 0)
            {
                //FOUND KEY
                //delete mapping
                deleteMapping(mapnode, tableIndex);
                pthread_mutex_unlock(&the_actual_map->mutex);
                return trans_get_status(tp);
            }
            mapnode = (*mapnode).next;
        }
        //KEY NOT FOUND
        trans_abort(tp);
        pthread_mutex_unlock(&the_actual_map->mutex);
        return trans_get_status(tp);
    }
    else
    {
        //get the entry number by hash
        int hash = (*key).hash;
        int tableIndex = hash % NUM_BUCKETS;

        //check if there is an entry
        MAP_ENTRY *mapnode = the_actual_map->table[tableIndex];
        while(mapnode != NULL)
        {
            if(key_compare(key, (*mapnode).key) == 0)
            {
                //FOUND KEY

                //do garbage collection on the versions
                collectGarbage((*mapnode).versions, mapnode);

                //check if transaction is allowed to succeed
                int idcheck = transactionIdCheck(tp, (*mapnode).versions);
                if(idcheck == 2)
                {
                    //put it after the last version
                    trans_ref(tp, "added reference from store");
                    placeTransactionLast(tp, value, (*mapnode).versions);
                    pthread_mutex_unlock(&the_actual_map->mutex);
                    return trans_get_status(tp);
                }
                else
                {
                    if(idcheck == 1)
                    {
                        //replace the last version
                        replaceTransactionLast(tp, value, (*mapnode).versions);
                        pthread_mutex_unlock(&the_actual_map->mutex);
                        return trans_get_status(tp);
                    }
                    else
                    {
                        //does not succeed
                        trans_abort(tp);
                        pthread_mutex_unlock(&the_actual_map->mutex);
                        return trans_get_status(tp);
                    }
                }
            }
            mapnode = (*mapnode).next;
        }
        //KEY NOT FOUND

        //add a new entry and version
        VERSION *newVersion = version_create(tp, value);
        MAP_ENTRY *newEntry = calloc(1, sizeof(MAP_ENTRY));
        (*newEntry).key = key;
        (*newEntry).versions = newVersion;
        (*newEntry).next = NULL;
        addMapEntryToIndex(tableIndex, newEntry);
        trans_ref(tp, "added reference from store");
        pthread_mutex_unlock(&the_actual_map->mutex);
        return trans_get_status(tp);
    }
}

TRANS_STATUS store_get(TRANSACTION *tp, KEY *key, BLOB **valuep)
{
    pthread_mutex_lock(&the_actual_map->mutex);
    //get the entry number by hash
    int hash = (*key).hash;
    int tableIndex = hash % NUM_BUCKETS;

    //check if there is an entry
    MAP_ENTRY *mapnode = the_actual_map->table[tableIndex];
    while(mapnode != NULL)
    {
        if(key_compare(key, (*mapnode).key) == 0)
        {
            //FOUND KEY

            //do garbage collection on the versions
            collectGarbage((*mapnode).versions, mapnode);

            //check if transaction is allowed to succeed
            int idcheck = transactionIdCheck(tp, (*mapnode).versions);
            if(idcheck == 2)
            {
                //put it after the last version with the same value
                trans_ref(tp, "added reference from store");
                copyTransactionLast(tp, valuep, (*mapnode).versions);
                pthread_mutex_unlock(&the_actual_map->mutex);
                return trans_get_status(tp);
            }
            else
            {
                if(idcheck == 1)
                {
                    //get value of last version
                    getValueTransactionLast(tp, valuep, (*mapnode).versions);
                    pthread_mutex_unlock(&the_actual_map->mutex);
                    return trans_get_status(tp);
                }
                else
                {
                    //does not succeed
                    trans_abort(tp);
                    pthread_mutex_unlock(&the_actual_map->mutex);
                    return trans_get_status(tp);
                }
            }

        }
        mapnode = (*mapnode).next;
    }
    //KEY NOT FOUND

    //add a new entry and version with a null value
    /*char *nullContent = "null";
    BLOB *nullblob = blob_create(nullContent, sizeof(char*));
    VERSION *newVersion = version_create(tp, nullblob);
    MAP_ENTRY *newEntry = calloc(1, sizeof(MAP_ENTRY));
    (*newEntry).key = key;
    (*newEntry).versions = newVersion;
    (*newEntry).next = NULL;
    addMapEntryToIndex(tableIndex, newEntry);
    trans_ref(tp, "added reference from store");
    *valuep = NULL;
    pthread_mutex_unlock(&the_actual_map->mutex);
    return trans_get_status(tp);*/
    char *nullContent = NULL;
    BLOB *nullblob = blob_create(nullContent, 0);
    VERSION *newVersion = version_create(tp, nullblob);
    MAP_ENTRY *newEntry = calloc(1, sizeof(MAP_ENTRY));
    (*newEntry).key = key;
    (*newEntry).versions = newVersion;
    (*newEntry).next = NULL;
    addMapEntryToIndex(tableIndex, newEntry);
    trans_ref(tp, "added reference from store");
    *valuep = NULL;
    pthread_mutex_unlock(&the_actual_map->mutex);
    return trans_get_status(tp);

}

void initMapTable(MAP_ENTRY **table)
{
    int i = 0;
    while(i < NUM_BUCKETS)
    {
        *table = NULL;
        table++;
        i++;
    }
}

void freeTheTable(MAP_ENTRY **table)
{
    int i = 0;
    while(i < NUM_BUCKETS)
    {
        freeMap(*table);
        table++;
        i++;
    }
}

void freeMap(MAP_ENTRY *table)
{
    MAP_ENTRY *mapptr;
    while(table != NULL)
    {
        mapptr = table;
        table = (*table).next;
        (*mapptr).key = NULL;
        freeVersions((*mapptr).versions);
        (*mapptr).versions = NULL;
        (*mapptr).next = NULL;
        free(mapptr);
    }
}

void freeVersions(VERSION *version)
{
    VERSION *versionptr;
    while(version != NULL)
    {
        versionptr = version;
        version = (*version).next;
        version_dispose(versionptr);
    }
}

//adds entry to end of the linked list
void addMapEntryToIndex(int tableIndex, MAP_ENTRY *newEntry)
{
    MAP_ENTRY **map = the_actual_map->table;
    map = map + tableIndex;
    //MAP_ENTRY *entrynode = the_actual_map->table[tableIndex];
    //MAP_ENTRY *entrynode = *map;
    if(*map == NULL)
    {
        //entrynode = newEntry;
        *map = newEntry;
    }
    else
    {
        MAP_ENTRY *mapptr = *map;
        /*while((*entrynode).next != NULL)
        {
            entrynode = (*entrynode).next;
        }
        (*entrynode).next = newEntry;*/
        while((*mapptr).next != NULL)
        {
            mapptr = (*mapptr).next;
        }
        (*mapptr).next = newEntry;
    }
}

void deleteMapping(MAP_ENTRY *mapnode, int tableIndex)
{
    MAP_ENTRY *prevnode = NULL;
    MAP_ENTRY *node = the_actual_map->table[tableIndex];
    while(node != mapnode)
    {
        prevnode = node;
        node = (*node).next;
    }

    if(prevnode == NULL)
    {
        freeMap(mapnode);
        the_actual_map->table[tableIndex] = NULL;
    }
    else
    {
        (*prevnode).next = (*mapnode).next;
        freeMap(mapnode);
    }
}

void collectGarbage(VERSION *version, MAP_ENTRY *mapnode)
{
    //get the earliest abort and remove all later versions
    if(version == NULL)
    {
        return;
    }
    VERSION *versionptr = version;
    int done = 0;
    while(versionptr != NULL && done == 0)
    {
        TRANSACTION *thisTransaction = (*versionptr).creator;
        if(trans_get_status(thisTransaction) == TRANS_ABORTED)
        {
            removeFurtherVersions(versionptr);
            (*versionptr).next = NULL;
            done = 1;
        }
        else
        {
            versionptr = (*versionptr).next;
        }
    }
    //all aborted transactions are gone by now
    //clear all committed versions except the newest one
    versionptr = version;
    int highestCommittedId = -1;
    TRANSACTION *highestCommittedTransaction = NULL;
    while(versionptr != NULL)
    {
        TRANSACTION *thisTransaction = (*versionptr).creator;
        int transid = (*thisTransaction).id;
        if(transid > highestCommittedId && trans_get_status(thisTransaction) == TRANS_COMMITTED)
        {
            highestCommittedId = transid;
            highestCommittedTransaction = thisTransaction;
        }
        versionptr = (*versionptr).next;
    }
    if(highestCommittedTransaction != NULL)
    {
        versionptr = version;
        VERSION *temp;
        while((*versionptr).creator != highestCommittedTransaction)
        {
            temp = versionptr;
            versionptr = (*versionptr).next;
            version_dispose(temp);
            trans_unref((*versionptr).creator, "grabage collection");
        }
        (*mapnode).versions = versionptr;
    }
    return;
}

void removeFurtherVersions(VERSION *version)
{
    VERSION *versionptr;
    while(version != NULL)
    {
        versionptr = version;
        version = (*version).next;
        version_dispose(versionptr);
    }
}

int transactionIdCheck(TRANSACTION *tp, VERSION *version)
{
    if(version == NULL)
    {
        return 2;
    }
    int thisid = (*tp).id;
    while((*version).next != NULL)
    {
        version = (*version).next;
    }
    TRANSACTION *thisTransaction = (*version).creator;
    if(thisid > (*thisTransaction).id)
    {
        return 2;
    }
    if(thisid == (*thisTransaction).id)
    {
        return 1;
    }
    return 0;
}

void placeTransactionLast(TRANSACTION *tp, BLOB *value, VERSION *version)
{
    VERSION *newVersion = version_create(tp, value);
    VERSION *versionptr = version;
    (*newVersion).next = NULL;
    if(versionptr == NULL)
    {
        versionptr = newVersion;
    }
    else
    {
        while((*versionptr).next != NULL)
        {
            versionptr = (*versionptr).next;
        }
        (*versionptr).next = newVersion;
    }
    dependOnEarlierVersions(newVersion, version);
}

void replaceTransactionLast(TRANSACTION *tp, BLOB *value, VERSION *version)
{
    VERSION *versionptr = version;
    while((*versionptr).next != NULL)
    {
        versionptr = (*versionptr).next;
    }
    blob_unref((*versionptr).blob, "replaced the version");
    (*versionptr).blob = value;
    dependOnEarlierVersions(versionptr, version);
}

void dependOnEarlierVersions(VERSION *newVersion, VERSION *version)
{
    if(version == NULL)
    {
        return;
    }
    TRANSACTION *newVersionTransaction = (*newVersion).creator;
    while(version != newVersion)
    {
        TRANSACTION *thisTransaction = (*version).creator;
        if(trans_get_status(thisTransaction) == TRANS_PENDING)
        {
            trans_add_dependency(newVersionTransaction, thisTransaction);
        }
        version = (*version).next;
    }
}

void copyTransactionLast(TRANSACTION *tp, BLOB **value, VERSION *version)
{
    VERSION *versionptr = version;
    while((*versionptr).next != NULL)
    {
        versionptr = (*versionptr).next;
    }
    VERSION *newVersion = version_create(tp, (*versionptr).blob);
    (*newVersion).next = NULL;
    (*versionptr).next = newVersion;
    *value = (*versionptr).blob;
    blob_ref((*versionptr).blob, "get transaction increase blob");
    dependOnEarlierVersions(newVersion, version);
}

void getValueTransactionLast(TRANSACTION *tp, BLOB **value, VERSION *version)
{
    VERSION *versionptr = version;
    while((*versionptr).next != NULL)
    {
        versionptr = (*versionptr).next;
    }
    *value = (*versionptr).blob;
    blob_ref((*versionptr).blob, "get transaction increase blob");
    dependOnEarlierVersions(versionptr, version);
}

void store_show(void)
{
    int i = 0;
    while(i < NUM_BUCKETS)
    {
        MAP_ENTRY **map = the_actual_map->table;
        map = map + i;
        MAP_ENTRY *mapptr = *map;
        debug("MAP ENTRY: %d \n.",i);
        while(mapptr != NULL)
        {
            KEY *theKey = (*mapptr).key;
            BLOB *theBlob = (*theKey).blob;
            debug("KEY: %s \n.",(*theBlob).prefix);
            VERSION *versions = (*mapptr).versions;
            while(versions != NULL)
            {
                BLOB *versionBlob = (*versions).blob;
                TRANSACTION *trans = (*versions).creator;
                int id = (*trans).id;
                debug("ID: %d \n.",id);
                debug("PAYLOAD: %s \n.",(*versionBlob).prefix);
                id = id;
                theBlob = theBlob;
                versionBlob = versionBlob;
                versions = (*versions).next;
            }
            mapptr = (*mapptr).next;
            debug("---------------------------------");
        }
        i++;
    }
}