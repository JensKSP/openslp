
/***************************************************************************/
/*                                                                         */
/* Project:     OpenSLP - OpenSource implementation of Service Location    */
/*              Protocol                                                   */
/*                                                                         */
/* File:        slplib_knownda.c                                           */
/*                                                                         */
/* Abstract:    Internal implementation for generating unique XIDs.        */
/*              Provides functions that are supposed to generate 16-bit    */
/*              values that won't be generated for a long time in this     */
/*              process and hopefully won't be generated by other process  */ 
/*              for a long time.                                           */
/*                                                                         */
/*-------------------------------------------------------------------------*/
/*                                                                         */
/* Copyright (c) 1995, 1999  Caldera Systems, Inc.                         */
/*                                                                         */
/* This program is free software; you can redistribute it and/or modify it */
/* under the terms of the GNU Lesser General Public License as published   */
/* by the Free Software Foundation; either version 2.1 of the License, or  */
/* (at your option) any later version.                                     */
/*                                                                         */
/*     This program is distributed in the hope that it will be useful,     */
/*     but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/*     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the       */
/*     GNU Lesser General Public License for more details.                 */
/*                                                                         */
/*     You should have received a copy of the GNU Lesser General Public    */
/*     License along with this program; see the file COPYING.  If not,     */
/*     please obtain a copy from http://www.gnu.org/copyleft/lesser.html   */
/*                                                                         */
/*-------------------------------------------------------------------------*/
/*                                                                         */
/*     Please submit patches to http://www.openslp.org                     */
/*                                                                         */
/***************************************************************************/


#include "slp.h"
#include "libslp.h"


/*=========================================================================*/
SLPList G_KnownDAList = {0,0,0};
/* The list of known DAs. All calls in the file are ment to fill this list */
/* with useable DAs.                                                       */
/*=========================================================================*/


/*-------------------------------------------------------------------------*/
void KnownDASaveHints()
/*-------------------------------------------------------------------------*/
{
    int         fd;
#ifdef WIN32
    fd = creat(SLPGetProperty("net.slp.HintsFile"),
               _S_IREAD | _S_IWRITE);
#else  /* UNIX */
    fd = creat(SLPGetProperty("net.slp.HintsFile"),
               S_IROTH | S_IWOTH | S_IRGRP| S_IWGRP | S_IRUSR | S_IWUSR);
#endif
              
    if (fd >= 0)
    {
        SLPDAEntryListWrite(fd, &G_KnownDAList);
        close(fd);
    }

}

/*-------------------------------------------------------------------------*/
SLPBoolean KnownDADiscoveryCallback(SLPError errorcode, 
                                    SLPMessage msg, 
                                    void* cookie)
/*-------------------------------------------------------------------------*/
{
    SLPSrvURL*      srvurl;
    SLPDAEntry*     entry;
    struct hostent* he;
    int*            count   = (int*)cookie;

    if (errorcode == 0)
    {
        if (msg && msg->header.functionid == SLP_FUNCT_DAADVERT)
        {
            if (msg->body.srvrply.errorcode == 0)
            { 
                /* NULL terminate scopelist */
                *((char*)msg->body.daadvert.scopelist + msg->body.daadvert.scopelistlen) = 0;
                if (SLPParseSrvURL(msg->body.daadvert.url, &srvurl) == 0)
                {
                    he = gethostbyname(srvurl->s_pcHost);
                    if (he)
                    {
                        entry = SLPDAEntryCreate((struct in_addr*)(he->h_addr_list[0]),
                                                 msg->body.daadvert.bootstamp,
                                                 msg->body.daadvert.scopelist,
                                                 msg->body.daadvert.scopelistlen);
                        SLPListLinkTail(&G_KnownDAList,(SLPListItem*)entry);
                        *count = *count + 1;
                    }

                    SLPFree(srvurl);
                }
            }
        }
    }

    return 1;
}


/*-------------------------------------------------------------------------*/
int KnownDADiscoveryRqstRply(int sock, 
                             struct sockaddr_in* peeraddr)
/* Returns: number of DAs discovered                                       */
/*-------------------------------------------------------------------------*/
{
    int         result      = 0;
    char*       buf;
    char*       curpos;
    int         bufsize;

    /*-------------------------------------------------------------------*/
    /* determine the size of the fixed portion of the SRVRQST            */
    /*-------------------------------------------------------------------*/
    bufsize  = 31; /*  2 bytes for the srvtype length */
                   /* 23 bytes for "service:directory-agent" srvtype */
                   /*  2 bytes for scopelistlen */
                   /*  2 bytes for predicatelen */
                   /*  2 bytes for sprstrlen */

    /* TODO: make sure that we don't exceed the MTU */
    buf = curpos = (char*)malloc(bufsize);
    if (buf == 0)
    {
        return 0;
    }
    memset(buf,0,bufsize);

    /*------------------------------------------------------------*/
    /* Build a buffer containing the fixed portion of the SRVRQST */
    /*------------------------------------------------------------*/
    /* service type */
    ToUINT16(curpos,23);
    curpos = curpos + 2;
    memcpy(curpos,"service:directory-agent",23);
    /* scope list zero length */
    /* predicate zero length */
    /* spi list zero length */

    NetworkRqstRply(sock,
                    peeraddr,
                    "en",
                    buf,
                    SLP_FUNCT_DASRVRQST,
                    bufsize,
                    KnownDADiscoveryCallback,
                    &result);

    free(buf);

    return result;       
}


/*-------------------------------------------------------------------------*/
int KnownDADiscoveryByMulticast()
/* Locates  DAs via multicast convergence                                  */
/*                                                                         */
/* Returns: number of DAs discovered                                       */
/*-------------------------------------------------------------------------*/
{
    int                 result      = 0;
    int                 sock;
    struct sockaddr_in  peeraddr;

    sock = NetworkConnectToMulticast(&peeraddr);
    if (sock >= 0)
    {
        result = KnownDADiscoveryRqstRply(sock, &peeraddr);
        close(sock);
    }

    return result;
}


/*-------------------------------------------------------------------------*/
int KnownDADiscoveryByProperties(struct timeval* timeout)
/* Locates DAs from a list of DA hostnames                                 */
/*                                                                         */
/* Returns: number of DAs discovered                                       */
/*-------------------------------------------------------------------------*/
{
    int                 result      = 0;
    char*               temp;
    char*               tempend;
    char*               slider1;
    char*               slider2;
    int                 sock;
    struct hostent*     he;
    struct sockaddr_in  peeraddr;

    memset(&peeraddr,0,sizeof(peeraddr));
    peeraddr.sin_family = AF_INET;
    peeraddr.sin_port = htons(SLP_RESERVED_PORT);

    slider1 = slider2 = temp = strdup(SLPGetProperty("net.slp.DAAddresses"));
    if (temp)
    {
        tempend = temp + strlen(temp);
        while (slider1 != tempend)
        {
            while (*slider2 && *slider2 != ',') slider2++;
            *slider2 = 0;

            he = gethostbyname(slider1);
            if (he)
            {
                peeraddr.sin_addr.s_addr = *((unsigned long*)(he->h_addr_list[0]));
                result += 1;

                sock = SLPNetworkConnectStream(&peeraddr,timeout);
                if (sock >= 0)
                {
                    result += KnownDADiscoveryRqstRply(sock, &peeraddr);
                    close(sock);
                }
            }

            slider1 = slider2;
            slider2++;
        }

        free(temp);
    }

    return result;
}

/*=========================================================================*/
int KnownDADiscover(struct timeval* timeout)
/* Returns: the number of DAs discovered                                   */
/*=========================================================================*/
{
    int         fd;
    int         result      = 0;

    /* TODO THIS FUNCTION MUST BE SYNCRONIZED !! */
    /* two threads must not be in here at the same time */
    
    /*----------------------------------------------------*/
    /* Check values from the net.slp.DAAddresses property */
    /*----------------------------------------------------*/
    result = KnownDADiscoveryByProperties(timeout);
    if (result)
    {
        KnownDASaveHints();
        return result;
    }
    

    /*------------------------------*/
    /* Check data from DHCP Options */
    /*------------------------------*/
    /* TODO put code here when you can */


    /*-----------------------------------*/
    /* Load G_KnownDAListhead from hints */
    /*-----------------------------------*/
    fd = open(SLPGetProperty("net.slp.HintsFile"),O_RDONLY);
    if (fd >= 0)
    {
        SLPDAEntryListRead(fd, &G_KnownDAList);
        close(fd);
        if (G_KnownDAList.count)
        {
            return G_KnownDAList.count;
        }
    }
    

    /*-------------------*/
    /* Multicast for DAs */
    /*-------------------*/
    if (SLPPropertyAsBoolean(SLPGetProperty("net.slp.activeDADetection")) &&
        SLPPropertyAsInteger(SLPGetProperty("net.slp.DAActiveDiscoveryInterval")))
    {
        result = KnownDADiscoveryByMulticast();
        if (result)
        {
            KnownDASaveHints();
            return result;
        }
    }
    
    return 0;
}


/*=========================================================================*/
int KnownDAConnect(const char* scopelist, 
                   int scopelistlen,
                   struct sockaddr_in* peeraddr,
                   struct timeval* timeout)
/*=========================================================================*/
{
    int                 sock;
    SLPDAEntry*         entry;
    SLPDAEntry*         del   = 0;

    /* TODO THIS FUNCTION MUST BE SYNCRONIZED !! */

    memset(peeraddr,0,sizeof(struct sockaddr_in));
    peeraddr->sin_family = AF_INET;
    peeraddr->sin_port   = htons(SLP_RESERVED_PORT);

    entry = (SLPDAEntry*)G_KnownDAList.head;
    while (entry)
    {
        if (SLPIntersectStringList(entry->scopelistlen,
                                   entry->scopelist,
                                   scopelistlen,
                                   scopelist))
        {
            peeraddr->sin_addr   = entry->daaddr;
            sock = SLPNetworkConnectStream(peeraddr,timeout);
            if (sock >= 0)
            {
                return sock;
            }

            del = entry;
        }

        entry = (SLPDAEntry*) entry->listitem.next;

        if (del)
        {
            SLPDAEntryFree((SLPDAEntry*)SLPListUnlink(&G_KnownDAList,(SLPListItem*)del));
            KnownDASaveHints();
        }
    }

    return -1;
}

