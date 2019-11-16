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

#ifndef _MEDIA_H
#define _MEDIA_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "arserver.h"
#include "utilities.h"
#include "mix_engine.h"

char mediaInit(void);
void mediaShutdown(void);
char *GetFileHash(const char *path);
char *getFilePrefixPoint(char **file);
void GetFileMetaData(uint32_t UID, const char *url);
unsigned char CheckFileHashMatch(const char *path, const char *hash);	
uint32_t LoadPlayer(int *pNum, const char *url_str, uint32_t UID, unsigned char noCheck);
void GetURLMetaData(uint32_t UID, const char *url);

char fplPLGetNextMeta(FILE *fp, uint32_t UID);
char plsPLGetNextMeta(FILE *fp, uint32_t UID, char *filePath);
char m3uPLGetNextMeta(FILE *fp, uint32_t UID, char *filePath);
void plsPLGetProperties(FILE *fp, uint32_t UID);
void m3uPLGetProperties(FILE *fp, uint32_t UID);

unsigned char plTaskRunner(uint32_t UID);
void PLOpen(taskRecord *parent);
void dbPLOpen(uint32_t UID);
void filePLOpen(uint32_t UID);

int CreateFPLHeader(FILE *fpl, char *Name);
void CloseFPL(FILE *fpl, float duration, int durFilePos);
unsigned char AddFPLEntryFromUID(FILE *fpl, float Offset, uint32_t UID);
void AddFPLEntryFromProgramLogStruct(FILE *fpl, float Offset, ProgramLogRecord *Rec);

char *getScriptFromFile(const char *file, unsigned char silent);

float associatedPLNext(FILE *fp, float curPlayPos);
unsigned char associatedPLLog(FILE *fp, uint32_t parent, unsigned int busses, unsigned char idOK);
char associatedPLOpen(const char *url, FILE **fp);

#ifdef __cplusplus
}
#endif

#endif
