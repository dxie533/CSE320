#include "protocol.h"
#include "csapp.h"
#include <unistd.h>
#include <stdlib.h>
#include "debug.h"

int proto_send_packet(int fd, XACTO_PACKET *pkt, void *data)
{
    //convert the fields larger than 1 byte to network byte order
    uint8_t isNull = (*pkt).null;
    uint32_t datasize;
    if(!isNull)
    {
        datasize = (*pkt).size;
    }
    else
    {
        datasize = 0;
    }

    (*pkt).size = htonl((*pkt).size);
    (*pkt).timestamp_sec = htonl((*pkt).timestamp_sec);
    (*pkt).timestamp_nsec = htonl((*pkt).timestamp_nsec);
    ssize_t byteWritten;

    int counter1 = 0;
    size_t bytesleft1 = sizeof(XACTO_PACKET);
    XACTO_PACKET *pktbuf = pkt;
    while(counter1 < sizeof(XACTO_PACKET))
    {
        byteWritten = write(fd, pktbuf, bytesleft1);
        debug("Bytes written %d \n.",(int)byteWritten);
        if(byteWritten == -1)
        {
            return -1;
        }
        pktbuf = pktbuf + byteWritten;
        counter1 = counter1 + byteWritten;
        bytesleft1 = bytesleft1 - byteWritten;
    }

    if(!isNull)
    {
        if(datasize != 0)
        {
            int counter2 = 0;
            size_t bytesleft2 = datasize;
            void *payloadPtr = data;
            while(counter2 < datasize)
            {
                byteWritten = write(fd, payloadPtr, datasize);
                debug("Bytes2 written %d \n.",(int)byteWritten);
                if(byteWritten == -1)
                {
                    return -1;
                }
                payloadPtr = payloadPtr + byteWritten;
                counter2 = counter2 + byteWritten;
                bytesleft2 = bytesleft2 - byteWritten;
            }
        }
    }
    return 0;
}

int proto_recv_packet(int fd, XACTO_PACKET *pkt, void **datap)
{
    //READ PACKET
    ssize_t byteWritten;
    int counter1 = 0;
    size_t bytesleft1 = sizeof(XACTO_PACKET);
    XACTO_PACKET *pktbuf = pkt;
    while(counter1 < sizeof(XACTO_PACKET))
    {
        byteWritten = read(fd, pktbuf, bytesleft1);
        debug("Bytes read %d \n.",(int)byteWritten);
        if(byteWritten == -1)
        {
            return -1;
        }
        if(byteWritten == 0)
        {
            return -1;
            //break;
        }
        pktbuf = pktbuf + byteWritten;
        counter1 = counter1 + byteWritten;
        bytesleft1 = bytesleft1 - byteWritten;
    }
    //CONVERT FIELDS
    uint8_t isNull = (*pkt).null;
    uint32_t datasize;

    (*pkt).size = ntohl((*pkt).size);
    (*pkt).timestamp_sec = ntohl((*pkt).timestamp_sec);
    (*pkt).timestamp_nsec = ntohl((*pkt).timestamp_nsec);

    if(!isNull)
    {
        datasize = (*pkt).size;
    }
    else
    {
        datasize = 0;
    }

    if(!isNull)
    {
        if(datasize != 0)
        {
            void *payloadPtr = calloc(1,datasize);
            void *startofPayload = payloadPtr;
            if(payloadPtr == NULL)
            {
                return -1;
            }

            int counter2 = 0;
            size_t bytesleft2 = datasize;
            while(counter2 < datasize)
            {
                byteWritten = read(fd, payloadPtr, bytesleft2);
                debug("Bytes2 read %d \n.",(int)byteWritten);
                if(byteWritten == -1)
                {
                    return -1;
                }
                if(byteWritten == 0)
                {
                    return -1;
                    //break;
                }
                payloadPtr = payloadPtr + byteWritten;
                counter2 = counter2 + byteWritten;
                bytesleft2 = bytesleft2 - byteWritten;
            }
            *datap = startofPayload;
        }
        else
        {
            datap = NULL;
        }
    }
    return 0;
}