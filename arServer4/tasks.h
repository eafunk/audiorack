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

#ifndef __TASKS_H__
#define __TASKS_H__

#include <pthread.h>
#include <time.h>
#include "utilities.h"

struct __attribute__((packed)) modbusData {	
	uint16_t			transID;
	uint16_t			protoID;
	unsigned char		pading;
	unsigned char		length;
	unsigned char		unit;
	unsigned char		function;
	unsigned char		data[254];
};

struct modbusPollRec{	
	int					sock;
	int					period;
	unsigned char		unitID;
	unsigned int		inputID;
	char				addr[256];
	char				conf[256];
};

extern pthread_rwlock_t taskLock;
extern taskRecord *taskList;

// function prototypes
void initTaskList(void);
void freeTaskList(void);
void createTaskItem(char *theName, void (*theProc)(void *), void *usrDataPtr, uint32_t theUID, int16_t thePlayer, uint32_t theLifeTime, unsigned char allowDelete);
void AddSelf(void);
void taskCleanUp(void *pass);
void executeCleanUp(void *pass);
void ExecuteCommand(void *refIn);
void ExecuteProcess(char *command, uint32_t UID, uint32_t timeOut);
void WaitPID(void *refIn);
void loadConfigFromTask(void *refIn);
void modbusPoll(void *refIn);

#endif
