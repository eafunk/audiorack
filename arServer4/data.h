#ifndef _DATA_H
#define _DATA_H

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

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#include "arserver.h"
#include "utilities.h"

struct itemGroup {
	int first_index;
	double first_start_time;
	int last_index;
	double last_end_time;
	int highest_priority_value;
	double highest_priority_error;
	double highest_priority_target;
	double highest_priority_start_time;
};
typedef struct itemGroup itemGroupRec;

pthread_rwlock_t dataLock;
pthread_rwlock_t inputLock;
pthread_rwlock_t queueLock;
pthread_rwlock_t connLock;
uidRecord *metaList;
inputRecord *inputList;
queueRecord *queueList;
connRecord *connList;

void initDataLists(void);
void freeDataLists(void);
uint32_t createMetaRecord(const char *url, uint32_t *reqID, unsigned char silent);
void createSettingsRecord(const char *version);
void releaseMetaRecord(uint32_t uid);
void retainMetaRecord(uint32_t uid);
unsigned char MetaDoesKeyExist(uint32_t uid, const char *key);
char *GetMetaData(uint32_t uid, const char *key, unsigned char allowNull);
long GetMetaInt(uint32_t uid, const char *key, unsigned char *isEmpty);
double GetMetaFloat(uint32_t uid, const char *key, unsigned char *isEmpty);
unsigned char SetMetaData(uint32_t uid, const char *key, const char *value);
unsigned char UpdateMetaData(uint32_t uid, const char *key, const char *value);
uint32_t GetMetaRev(uint32_t uid);
unsigned int GetMetaKeysAndValues(uint32_t uid, char ***keys, char ***values);
uint32_t FindUidForKeyAndValue(const char *key, const char *value, unsigned int index);
unsigned char DelMetaData(uint32_t uid, const char *key);
void resolveStringMacros(char **theStr, uint32_t uid);

queueRecord *createQueueRecord(uint32_t uid);
unsigned char releaseQueueRecord(queueRecord *root, queueRecord *rec, unsigned char force);
unsigned char releaseQueueEntry(uint32_t uid);
void retainQueueEntry(uint32_t uid);
uint32_t getQueueRecStatus(queueRecord *rec, inChannel **input);
unsigned char getQueuePos(uint32_t *ref);
unsigned int queueCount(void);
unsigned int queueGetNextSegPos(int *thisP);
void UpdateQueueEndTimes(unsigned char sort);
time_t queueGetEndTime(void);

#ifdef __cplusplus
}
#endif

#endif
