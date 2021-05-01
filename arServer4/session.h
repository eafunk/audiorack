/*
  Copyright (C) 2019 Ethan Funk
  
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.
  
  You should have received a copy of the GNU Lesser General Public License
  along with this program; if not, write to the Free Software 
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#ifndef _SESSION_H
#define _SESSION_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <pthread.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <dirent.h>
#include <pwd.h>
#include <arpa/inet.h>

#include "arserver.h"
#include "utilities.h"
#include "media.h"

#define		rError	0
#define		rOK		1
#define		rNone	2

typedef struct {
	pthread_t sessionThread;
	int cs;						// client socket
	unsigned char silent;
	char *save_pointer;
	char *errMSG;
	struct sockaddr_in6 client;
	unsigned char notify_meters;
	unsigned char use_tcp;
	uint32_t sender;
	int16_t lastPlayer;
	uint32_t lastUID;
	uint32_t lastAID;
} ctl_session;

char *initSessions(unsigned int maxSessions, short *tcpPort);
void shutdownSessions(void);
int my_send(ctl_session *session, const char *buf, int tx_length, unsigned char silent, int flags);
unsigned char processCommand(ctl_session *session, char *command, unsigned char *passResult);
unsigned char loadConfiguration(ctl_session *session, char *file_path);
int noticeSend(const char *buf, int tx_length, unsigned char isVU);

uint32_t getSenderID(void);

#ifdef __cplusplus
}
#endif

#endif
