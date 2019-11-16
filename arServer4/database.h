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

#ifndef __DATABASE_H__
#define __DATABASE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <dbi/dbi.h>
#include <dirent.h>
#include "arserver.h"
#include "session.h"

struct dbErr{
	const char *message;
	unsigned char flag;
};

void DumpDBDriverList(ctl_session *session, char *buf, size_t size);
unsigned char MakeLogEntry(ProgramLogRecord *rec);
unsigned char updateLogMeta(uint32_t uid);
void DeleteLogEntry(void *inRef);
short dbPLGetNextMeta(uint32_t index, uint32_t ID, uint32_t UID);
unsigned char dbTaskRunner(uint32_t UID, unsigned char load);
void clearCachedFingerprint(void);
unsigned int getFingerprint(void);
char *dbGetInfo(const char *property);
unsigned char db_initialize(struct dbErr *inRec);
void dbPick(taskRecord *parent);
void folderPick(taskRecord *parent);
uint32_t dbGetFillID(time_t *when);
char *dbGetItemName(uint32_t ID);
void dbMacroReplace(char **query);
uint32_t dbGetNextScheduledItem(dbi_result *db_result, time_t *targetTime, short *priority, time_t from_t, time_t to_t, unsigned char highOnly);
void dbSaveFilePos(uint32_t UID, float position);
char *dbGetReqestComment(time_t theTime);
char *dbGetCurrentMSG(void);
char *FindFromMeta(uint32_t UID);											
void GetItemMetaData(uint32_t UID, const char *url);			
uint32_t IDSearchMarkedFile(const char *path, const char *Hash);
void dbFileSync(ctl_session *session, unsigned char silent);
void dbFileSearch(ctl_session *session, unsigned char silent, const char *Path, uint32_t pace);
		
#ifdef __cplusplus
}
#endif
		
#endif
