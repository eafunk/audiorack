/*
 Copyright (c) 2019 Ethan Funk
 
 Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated 
 documentation files (the "Software"), to deal in the Software without restriction, including without limitation 
 the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, 
 and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all copies or substantial portions 
 of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED 
 TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
 THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF 
 CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
 DEALINGS IN THE SOFTWARE.
*/

#ifndef _UTILITIES_H
#define _UTILITIES_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
	void *next;			// next record in list, or NULL for end
	uint32_t refCnt;
	uint32_t ID;		// ID number for this list entry			
	char item[];
} LinkedListEntry;

typedef struct {
	void *next;			// next record in list, or NULL for end
	uint32_t refCnt;
	uint32_t UID;		// Unique ID number for this list entry	
	char type;		
	void *child;
	void (*freeChild)(void *);	//function pointer to free the child
	unsigned char silent;		// set to non-zero to supress notices on change
	uint32_t rev;
} uidRecord;

typedef struct {
	void *next;	// next record in list, or NULL for end
	uint32_t refCnt;
	uint32_t keyHash;		// An ELF string hash created from the key string
	char *key;
	char *value;
} keyValueRecord;

typedef struct {
	void *next;	// next record in list, or NULL for end
	uint32_t refCnt;
	uint32_t SrcHash;		// An ELF string hash created from the jack src port name string
	char *src;
	uint32_t DestHash;		// An ELF string hash created from the jack dest port name string
	char *dest;
} connRecord;

typedef struct {
	void *next;	// next record in list, or NULL for end
	uint32_t refCnt;
	uint32_t NameHash;		// An ELF string hash created from the key string
	char *Name;
	uint32_t busses;
	uint32_t controls;
	char *portList;
	char *mmList;
} inputRecord;

typedef struct {
	void *next;	// next record in list, or NULL for end
	uint32_t refCnt;
    uint32_t taskID;  
    uint32_t UID;  
    int16_t player;
	char *name;
    void *userData;
    void (*Proc)(void *);
    unsigned char finished;     
    pthread_t thread;
	time_t started;
    int timeOut;
	unsigned char cancelThread;
    unsigned char delUID;
	pid_t pid;
} taskRecord;

typedef struct {
	void *next;			// next record in list, or NULL for end
	uint32_t refCnt;
	uint32_t UID;		// Unique ID number for this entry, corrisponds to a uidRecord		
	uint32_t status;
	int16_t player;		// player index + 1, zero for no player
	double	endTime;
} queueRecord;

uint32_t ELFHash(uint32_t hash, const char* str, unsigned int len);

// CRC32 checksum: based on implementation by Finn Yannick Jacobs
void chksum_crc32gentab(uint32_t *table);
uint32_t chksum_crc32(unsigned char *block, size_t length, uint32_t *table);

void str_setstr(char **string, const char *cStr);
void str_appendstr(char **string, const char *cStr);
void str_appendchr(char **string, char chr);
void str_appendbytes(char **data, unsigned int *length, const char *frag, unsigned int size);
void str_insertstr(char **string, const char *cStr, unsigned int pos);
void str_cutstr(char **string, unsigned int pos, unsigned int length);
unsigned int str_CountFields(char *string, const char *token);
char *str_prefixSpan(char *string, const char *prefix);
char *str_NthField(const char *string, const char *token, unsigned int field);
void str_strip_chr(char **string, const char find);
char *str_firstnonspace(char *string);
void str_ReplaceAll(char **theStr, const char *find, const char *replace);
char *fstr(double num, int dp);
char *istr(long num);
char *ustr(unsigned long num);
char *hstr(uint32_t num, int places);
char *timeToStr(time_t when);

char *uriEncode(const char *in_uri);
char *uriEncodeKeepSpace(const char *in_uri);
char *uriEncodeKeepSlash(const char *in_uri);
char *uriDecode(const char *in_uri);

float ftodb(float linMag);
unsigned char ftovu(float linMag);

LinkedListEntry	*unlinkNode(LinkedListEntry *node, LinkedListEntry *fromNode, uint32_t  id);
LinkedListEntry	*moveAfterNode(LinkedListEntry *node, LinkedListEntry *toNode, LinkedListEntry *startNode);
LinkedListEntry *insertAfterNode(LinkedListEntry *prevNode, LinkedListEntry *node);
void appendNode(LinkedListEntry *fromNode, LinkedListEntry *node);
LinkedListEntry	*findNode(LinkedListEntry *fromNode, uint32_t id, uint32_t *index, LinkedListEntry **prevNode);
LinkedListEntry	*getNextNode(LinkedListEntry *fromNode);
LinkedListEntry	*getNthNode(LinkedListEntry *fromNode, uint32_t  index);
unsigned int countNodesAfter(LinkedListEntry *fromNode);
void retainListRecord(LinkedListEntry *rec);	// NOTE: can be used on all child types thru type-casting.
												// However, each child needs it's own release function to 
												// handle it's unique properties.

uidRecord *newUIDRecord(uidRecord *root, uint32_t uid, void (*freeChild)(void *));
unsigned char releaseUIDRecord(uidRecord *root, uidRecord *rec);
/* Node functions work on UID lists through type casting */

void releaseTaskRecord(taskRecord *root, taskRecord *rec);

keyValueRecord *setValueForKey(keyValueRecord *root, const char *key, const char *val);
const char *getValueForKey(keyValueRecord *root, const char *key);
keyValueRecord *getRecordForKey(keyValueRecord *root, const char *key);
void releaseKeyValueRecord(keyValueRecord *root, keyValueRecord *rec);
void releaseAllKV(void *root);
/* Node functions work on KV lists through type casting, with the keyHash as a uid  */

inputRecord *setValuesForInput(inputRecord *root, const char *Name, uint32_t busses, uint32_t controls, const char *portList);
inputRecord *getRecordForInput(inputRecord *root, const char *Name);
void releaseInputRecord(inputRecord *root, inputRecord *rec);
/* Node functions work on KV lists through type casting, with the keyHash as a uid  */

connRecord *setValuesForConn(connRecord *root, const char *Src, const char *Dest);
connRecord *findConnRecord(connRecord *root, const char *Src, const char *Dest);
void releaseConnRecord(connRecord *root, connRecord *rec);

#ifdef __cplusplus
}
#endif

#endif
