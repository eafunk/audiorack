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

#include "utilities.h"
#include <ctype.h>
#include <time.h>

/******************************************************************
 *  Some handy untility functions for manipulating strings, 
 *  URLs, numbers as strings, hashing, tolkenizing, etc.
 ******************************************************************/
char printable(char val){
	if((val > 31) && (val < 127))
		return val;
	else
		return '.';
}

void str_hexDump(const char *ptr){
	unsigned int size = strlen(ptr);
	char buf[16];
	unsigned int i = 0;
	unsigned int cnt;
	
	while(i < size){
		cnt = size - i;
		if(cnt > 16)
			cnt = 16;
		memset(buf, 0, sizeof(buf));
		memcpy(buf, ptr, cnt);
		fprintf(stderr, "%04X:%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c\n", 
			i ,ptr[0],ptr[1],ptr[2],ptr[3],ptr[4],ptr[5],ptr[6],ptr[7],ptr[8],ptr[9],ptr[10],ptr[11],ptr[12],ptr[13],ptr[14],ptr[15],
			printable(ptr[0]),printable(ptr[1]),printable(ptr[2]),printable(ptr[3]),printable(ptr[4]),printable(ptr[5]),
			printable(ptr[6]),printable(ptr[7]),printable(ptr[8]),printable(ptr[9]),printable(ptr[10]),
			printable(ptr[11]),printable(ptr[12]),printable(ptr[13]),printable(ptr[14]),printable(ptr[15]));
		ptr = ptr + 16;
		i = i + 16;
	}
}

uint32_t ELFHash(uint32_t hash, const char* str, unsigned int len)
{
	uint32_t x;
	uint32_t i;
	
	for(i = 0; i < len; str++, i++){
		hash = (hash << 4) + (*str);
		if((x = hash & 0xF0000000L) != 0){
			hash ^= (x >> 24);
		}
		hash &= ~x;
	}
	return hash;
}

// CRC32 checksum: based on implementation by Finn Yannick Jacobs
void chksum_crc32gentab(uint32_t *table){
	uint32_t crc, poly;
	int i, j;
	
	poly = 0xEDB88320L;
	for(i = 0; i < 256; i++){
		crc = i;
		for(j = 8; j > 0; j--){
			if (crc & 1)
				crc = (crc >> 1) ^ poly;
			else
				crc >>= 1;
		}
		table[i] = crc;
	}
}

uint32_t chksum_crc32(unsigned char *block, size_t length, uint32_t *table){
	register uint32_t crc;
	size_t i;
	
	crc = 0xFFFFFFFF;
	for(i = 0; i < length; i++)
		crc = ((crc >> 8) & 0x00FFFFFF) ^ table[(crc ^ *block++) & 0xFF];
	return(crc ^ 0xFFFFFFFF);
}

void str_setstr(char **string, const char *cStr){
	/* if *string is NULL, this will allocate memory for it as well */
	unsigned int size;
	
	if(cStr == NULL)
		return;
	if(*string)
		free(*string);
	*string = strdup(cStr);
}

void str_appendstr(char **string, const char *cStr){
	/* if *string is NULL, this will allocate memory for it as well */
	unsigned int size;
	
	if(cStr == NULL)
		return;
	size = strlen(cStr) + 1;
	if(*string == NULL)
		*string = calloc(1, size);
	size = strlen(*string) + size;
	*string = realloc(*string, size);
	strcat(*string, cStr);
}

void str_appendchr(char **string, char chr){
	unsigned int size;
	char str[2]; 
	
	if(chr == 0)
		return;
	if(*string == NULL){
		*string = calloc(1, 2);
		**string = chr;
	}else{
		size = strlen(*string) + 2;
		*string = realloc(*string, size);
		size--; 
		*(*string + size) = 0;
		size--;
		*(*string + size) = chr;
	}	
}

void str_appendbytes(char **data, unsigned int *length, const char *frag, unsigned int size){
	unsigned int total;
	/* This function adds raw byte to the end of a memory allocation.
	 * It DOES NOT add a NULL termination! */
	if(frag == NULL)
		return;
	if(*data == NULL)
		*data = calloc(1, size);
	total = *length + size;
	*data = realloc(*data, total);
	memcpy(*data+*length, frag, size);
	*length = total;
}

char *str_substring(char *string, unsigned int from, unsigned int length){
	// returns NULL if from index is outside of the string
	// otherwise returnes a new string with the segment that needs to be freed
	char *result;
	unsigned int len;
	len = strlen(string);
	if(from >= len)
		return NULL;
	if(len < (length + from))	// limit to end of source string
		length = len - from;
	result = calloc(1, length+1);
	if(length)
		memcpy(result, string+from, length);
	return result;
}

void str_insertstr(char **string, const char *cStr, unsigned int pos){
	unsigned int sizeA, sizeB;
	
	if(cStr == NULL)
		return;
	if(*string == NULL)
		return;
	sizeA = strlen(*string) + 1;
	sizeB = strlen(cStr);
	if(pos > sizeA)
		return;
	*string = realloc(*string, sizeA + sizeB);
	memmove(*string + sizeB + pos, *string + pos, sizeA-pos);
	memcpy(*string + pos, cStr, sizeB);
}

void str_cutstr(char **string, unsigned int pos, unsigned int length){
	unsigned int size;
	
	if(*string == NULL)
		return;
	size = strlen(*string) + 1;
	if(pos >= size)
		return;
	if(pos + length > size)
		length = size - pos - 1;
	memmove(*string + pos, *string + pos + length, size - length - pos);
	*string = realloc(*string, size - length);
}

unsigned int str_CountFields(char *string, const char *token){
	unsigned int result;
	int tsize;
	
	result = 0;
	if(tsize = strlen(token)){
		while(string = strstr(string, token)){
			result++;
			string = string + tsize;
		}
	}
	return result;
}

void str_strip_lfcr(char *string){
	char *str = string;
	while(str = strchr(str, '\r'))
		*str = ' ';
	str = string;
	while(str = strchr(str, '\n'))
		*str = ' ';
}

char *str_firstnonspace(char *string){
	while(*string){
		if(!isspace(*string))
			return string;
		string++;	
	}
	return NULL;
}

char *str_NthField(const char *string, const char *token, unsigned int field){
	unsigned int tsize;
	char *result, *tmp;
	
	/* if result is not NULL, string has been copied and will need to be freed */
	result = NULL;	
	if(tsize = strlen(token)){
		while(field > 0){
			if(string = strstr(string, token)){
				field--;
				string = string + tsize;
			}else
				return result;
		}
		result = strdup(string);
		if(tmp = strstr(result, token))
			*tmp = 0;
	}
	return result;
}

char *str_prefixSpan(char *string, const char *prefix){
	char *pattern, *segment, *saveptr;
	char *end, *tmp;
	/* This function accepts '*' chars in the prefix as a wildcard
	 * to match any character or charactors.  This function returns
	 * a pointer within string to the first character that doesn't
	 * match the prefix pattern. This result point to the start
	 * of the string if there is no match. */ 
	end = string;
	saveptr = pattern = strdup(prefix);
	while(segment = strtok_r(saveptr, "*", &saveptr)){
		if(tmp = strstr(string, segment)){
			if((end == string) && (tmp != string))
				/* First segment match MUST be at start of string
				 * otherwise there is an implied wildcard at the start
				 * of the prefix, and we can't have that. */
				break;
			end = tmp + strlen(segment);
		}else
			break;
	}
	free(pattern);
	if(segment)
		end = string; // incomplete match..., discard matches so far
	return end;	
}

void str_ReplaceAll(char **theStr, const char *find, const char *replace){
	char *ptr, *last;
	unsigned int sizeA, sizeB, loc;
	
	loc = 0;
	if(theStr){
		sizeA = strlen(find);
		sizeB = strlen(replace);
		while(ptr = strstr((*theStr + loc), find)){
			loc = ptr - *theStr;
			str_cutstr(theStr, loc, sizeA);
			str_insertstr(theStr, replace, loc);
			loc = loc + sizeB;
		}
	}
}

char *fstr(double num, int dp){
	/* result will need to be freed */
	char buf[32];
	snprintf(buf, sizeof buf, "%.*f", dp, num);
	return strdup(buf);
}

char *istr(long num){
	/* result will need to be freed */
	char buf[32];
	snprintf(buf, sizeof buf, "%ld", num);
	return strdup(buf);
}

char *ustr(unsigned long num){
	/* result will need to be freed */
	char buf[32];
	snprintf(buf, sizeof buf, "%lu", num);
	return strdup(buf);
}

char *hstr(uint32_t num, int places){
	/* result will need to be freed */
	char buf[32];
	snprintf(buf, sizeof buf, "%0*x", places, (unsigned int)num);
	return strdup(buf);	
}

char ctohex(char code) {
	static char hex[] = "0123456789abcdef";
	return hex[code & 15];
}

char *timeToStr(time_t when){
	struct tm whenRec;
	char buf[32];
	localtime_r(&when, &whenRec);
	snprintf(buf, sizeof buf, "%01d:%02d:%02d", whenRec.tm_hour, whenRec.tm_min, whenRec.tm_sec);
	return strdup(buf);	
}

char *uriDecode(const char *in_uri){
	// returns a string pointer (that you must then free) containing a % decoded URI.
	char *out_uri, *loc;
	char valstr[3];
	
	out_uri = NULL;
	if(in_uri && (out_uri = (char *)malloc(strlen(in_uri)+1))){
		strcpy(out_uri, in_uri);
		loc = out_uri;
		while((loc = strchr(loc, '%')) && (strlen(loc) > 2)){
			strncpy(valstr, loc+1, 2);
			valstr[2] = 0;
			if(*loc = (char)strtoul(valstr, NULL, 16)){
				loc = loc + 1;
				memmove(loc, loc+2, strlen(loc+2)+1);
			}else
				memmove(loc, loc+3, strlen(loc+3)+1);
		}
	}
	return out_uri;
}

char *uriEncode(const char *in_uri){
	// returns a string pointer (that you must then free) containing a % encoded URI.
	char *out_uri, *outPtr;
	char ch;

	out_uri = NULL;
	if(in_uri && (out_uri = (char *)malloc((strlen(in_uri) * 3) + 1))){
		outPtr = out_uri;
		while(ch = *in_uri){
			if(isalnum(ch) || (ch == '*') || (ch == '-') || (ch == '_') || (ch == '.') || (ch == '~')) 
				*outPtr++ = *in_uri;
			else{
				*outPtr++ = '%';
				*outPtr++ = ctohex(*in_uri >> 4);
				*outPtr++ = ctohex(*in_uri & 0x0f);
			}
			in_uri++;
		}
		*outPtr = 0;
	}
	return out_uri;
}

char *uriEncodeKeepSpace(const char *in_uri){
	// returns a string pointer (that you must then free) containing a % encoded URI.
	char *out_uri, *outPtr;
	char ch;

	out_uri = NULL;
	if(in_uri && (out_uri = (char *)malloc((strlen(in_uri) * 3) + 1))){
		outPtr = out_uri;
		while(ch = *in_uri){
			if(isalnum(ch) || (ch == ' ') || (ch == '*') || (ch == '-') || (ch == '_') || (ch == '.') || (ch == '~')) 
				*outPtr++ = *in_uri;
			else{
				*outPtr++ = '%';
				*outPtr++ = ctohex(*in_uri >> 4);
				*outPtr++ = ctohex(*in_uri & 0x0f);
			}
			in_uri++;
		}
		*outPtr = 0;
	}
	return out_uri; 
}

char *uriEncodeKeepSlash(const char *in_uri){
	// returns a string pointer (that you must then free) containing a % encoded URI.
	char *out_uri, *outPtr;
	char ch;

	out_uri = NULL;
	if(in_uri && (out_uri = (char *)malloc((strlen(in_uri) * 3) + 1))){
		outPtr = out_uri;
		while(ch = *in_uri){
			if(isalnum(ch) || (ch == '/') || (ch == '*') || (ch == '-') || (ch == '_') || (ch == '.') || (ch == '~')) 
				*outPtr++ = *in_uri;
			else{
				*outPtr++ = '%';
				*outPtr++ = ctohex(*in_uri >> 4);
				*outPtr++ = ctohex(*in_uri & 0x0f);
			}
			in_uri++;
		}
		*outPtr = 0;
	}
	return out_uri;
}
/******************************************************************
 *  Some handy untility functions for audio VU data
 ******************************************************************/
float ftodb(float linMag){
	// linMag is assumed to be magnitude squared!
	return 10.0 * log10f(linMag);
}

unsigned char ftovu(float linMag){
	// convert linMag to 1.7 fixed point format

	float scale;
	
	// linMag is assumed to be magnitude squared!
	scale = 255.0;
	linMag = sqrtf(linMag);			// un-squares the value passed in
	linMag = scale * sqrtf(linMag);	// square root again to make the vu scale close to an analog meter
	if(linMag > 255.0)
		return 255;
	else
		return (unsigned char)linMag;
}

/******************************************************************
 *  Some handy untility functions for manipulating linked-lists
 *  NOTE: a mutex/lock must be used to protect the entire list 
 *        when using these functions from more than one thread.
 ******************************************************************/
LinkedListEntry	*unlinkNode(LinkedListEntry *node, LinkedListEntry *startNode, uint32_t  id){ 
	LinkedListEntry	*prev, *current; 	
	/* for special use only.... not used by a typical application. */
	prev = startNode;
	while((current = prev->next) != NULL){ 
		if((!id && (current == node)) || (id && (current->ID == id))){ 
			prev->next = current->next;
			node->next = NULL;
			return current; 
		} 
		prev = current; 
	} 
    return NULL; 
}

LinkedListEntry	*moveAfterNode(LinkedListEntry *node, LinkedListEntry *toNode, LinkedListEntry *startNode){ 
	// returns the node if the move was sucessful.
	// Node will be move to the list position just after toNode
	if(toNode){
		if(unlinkNode(node, startNode, 0)){
			node->next = toNode->next;
			toNode->next = node;
			return node;
		}
	}
    return NULL; 
}

inline LinkedListEntry *insertAfterNode(LinkedListEntry *prevNode, LinkedListEntry *node){
	// assumes node is not currently linked in a list
	node->next = prevNode->next;
	prevNode->next = node;
}

void appendNode(LinkedListEntry *fromNode, LinkedListEntry *node){
	// links node to the end of the list
	LinkedListEntry	*prev, *current; 	
	
	prev = fromNode;
	while((current = prev->next) != NULL){
		prev = current; 
	}
	prev->next = node;
	node->next = NULL; 
}

LinkedListEntry	*findNode(LinkedListEntry *fromNode, uint32_t id, uint32_t *index, LinkedListEntry **prevNode){ 
	LinkedListEntry	*prev, *current;
	
	if(index)
		*index = 0;
	prev = fromNode;
	while((current = prev->next) != NULL){ 
		if(index)
			(*index)++;
		if(current->ID == id){
			if(prevNode)
				*prevNode = prev;
			return current; 
		}
		prev = current; 
	} 
    return NULL; 
}

inline LinkedListEntry *getNextNode(LinkedListEntry *fromNode){
	return fromNode->next;
}
 
LinkedListEntry	*getNthNode(LinkedListEntry *fromNode, uint32_t  index){
	// index of zero returns fromNode, 1 returns the next node, etc.
	// end or beyond returns NULL
	LinkedListEntry	*prev, *current; 	
	
	prev = fromNode;
	current = prev;
	while(index && ((current = prev->next) != NULL)){ 
		index--;
		prev = current; 
	} 
	return current;
}

unsigned int countNodesAfter(LinkedListEntry *fromNode){ 
	LinkedListEntry	*prev, *current; 	
	unsigned int count;
	
	count = 0;
	prev = fromNode;
	while((current = prev->next) != NULL){ 
		count++;
		prev = current; 
	} 
    return count; 
}

inline void retainListRecord(LinkedListEntry *rec){
	rec->refCnt++;
}

/******************************************************************
 *  Some handy untility functions for manipulating uid 
 *  collections, built ontop of the link list structure.
 ******************************************************************/
uidRecord *newUIDRecord(uidRecord *root, uint32_t uid, void (*freeChild)(void *)){
	uidRecord *newRec;
	
	// check if uid is unique
	if(!findNode((LinkedListEntry *)root, uid, NULL, NULL)){
		if(newRec = calloc(1, sizeof(uidRecord))){
			newRec->refCnt = 1;
			newRec->UID = uid;
			newRec->child = NULL;
			newRec->freeChild = freeChild;
			appendNode((LinkedListEntry *)root, (LinkedListEntry *)newRec);
			return newRec;
		}
	}
	return NULL;
}

unsigned char releaseUIDRecord(uidRecord *root, uidRecord *rec){
	uidRecord *prev, *current;
    
	/* will call freeChild function on final release, if set */
    rec->refCnt--;
	if(rec->refCnt == 0){
		/* if reference count is zero, unhook and free the record */
		prev = root;
		while((current = prev->next) != NULL){ 
			if(current == rec){ 
				/* unhook from list */
				
				prev->next = current->next;
				break; 
			} 
			prev = current; 
		} 
		/* free record */
		if(rec->child && rec->freeChild){
			rec->freeChild(&rec->child); 
		}
		free(rec);
		return 1;
	}
	return 0;
}

/******************************************************************
 *  Some handy untility functions for manipulating key-value
 *  lists, built ontop of the link list structure.
 ******************************************************************/

keyValueRecord *setValueForKey(keyValueRecord *root, const char *key, const char *val){
	/* value will be copied to a new string pointer 
	 * If key already exists, the existing value will be replaced 
	 * with the new value. If key does not exist, and new key-value
	 * record will be inserted in the list, in alphabetical order, by
	 * key ascending. If sucessful, also returns the record pointer.*/

	keyValueRecord	*prev, *current, *newRec; 	
	uint32_t keyHash;
	char *lastKey;
	keyHash = ELFHash(0, key, strlen(key));
	prev = root;
	while((current = prev->next) != NULL){ 
		if(current->keyHash == keyHash){
			if(strcmp(key, current->key) == 0){
				/* key found in list... update it's value */	
				if(current->value)
					free(current->value);
				current->value = strdup(val);
				return current;
			}
		}
		prev = current; 
	} 
    /* key not found in list... create a new entry for it */
    newRec = calloc(1, sizeof(keyValueRecord));
    newRec->refCnt = 1;
    newRec->key = strdup(key);
    newRec->keyHash = keyHash;
    newRec->value = strdup(val);

    /* insert into linked list at alphebetical order location */
    prev = root;
	while((current = prev->next) != NULL){ 
		if(strcmp(current->key, key) > 0){
			/* link before this current item */
			newRec->next = current;
			prev->next = newRec;
			return newRec;
		}
		prev = current; 
	}
	/* link at end of list */
	newRec->next = NULL;
	prev->next = newRec;
	return newRec;
}

const char *getValueForKey(keyValueRecord *root, const char *key){
	keyValueRecord	*prev, *current; 	
	uint32_t keyHash;
	
	/* Returned pointer should be considered read-only & short term 
	 * use. Callers should copy the string for further use out side 
	 * of the list. In particular, Mutithreaded use should copy the 
	 * result string BEFORE releasing the list mutex/lock. With use of 
	 * a list mutex, it is safe to use the string while the mutex is 
	 * being held */
	 
	keyHash = ELFHash(0, key, strlen(key));
	prev = root;
	while((current = prev->next) != NULL){ 
		if(current->keyHash == keyHash){
			if(strcmp(key, current->key) == 0)
				return current->value;
		}
		prev = current; 
	} 
   return NULL; 	
}

keyValueRecord *getRecordForKey(keyValueRecord *root, const char *key){
	keyValueRecord	*prev, *current; 	
	uint32_t keyHash;
	
	keyHash = ELFHash(0, key, strlen(key));
	prev = root;
	while((current = prev->next) != NULL){ 
		if(current->keyHash == keyHash){
			if(strcmp(key, current->key) == 0)
				return current;
		}
		prev = current; 
	} 
    return NULL; 
}

void releaseKeyValueRecord(keyValueRecord *root, keyValueRecord *rec){
	keyValueRecord	*prev, *current; 	
    
    rec->refCnt--;
	if(rec->refCnt == 0){
		/* if reference count is zero, unhook and free the record */
		prev = root;
		while((current = prev->next) != NULL){ 
			if(current == rec){ 
				/* unhook from list */
				prev->next = current->next;
				break; 
			} 
			prev = current; 
		} 
		/* free record and strings */
		if(rec->value)
			free(rec->value);
		if(rec->key)
			free(rec->key);

		free(rec);
	}
}

void releaseAllKV(void *ptr){
	/* ptr points to a uidRecord to which the KV list is attached */
	keyValueRecord	*next, *current, *root; 
		
	 
	root = (keyValueRecord*)ptr;
	next = root->next;
	while(current = next){ 
		next = current->next; 
		releaseKeyValueRecord(root, current);
	}
}

/******************************************************************
 *  Some handy untility functions for manipulating input def.
 *  lists, built ontop of the link list structure.
 ******************************************************************/
 
inputRecord *setValuesForInput(inputRecord *root, const char *Name, uint32_t busses, uint32_t controls, const char *portList){
	/* updates values of existing named entry, or creates a new one if required */
	inputRecord	*prev, *current, *newRec; 	
	uint32_t nameHash;

	nameHash = ELFHash(0, Name, strlen(Name));
	prev = root;
	while((current = prev->next) != NULL){ 
		if(current->NameHash == nameHash){
			if(strcmp(Name, current->Name) == 0){
				/* Name found in list... update it's values */	
				current->controls = controls;
				current->busses = busses;
				if(current->portList)
					free(current->portList);
				current->portList = strdup(portList);
				return current;
			}
		}
		prev = current; 
	}
	/* key not found in list... create a new entry for it */
	newRec = calloc(1, sizeof(inputRecord));
	newRec->refCnt = 1;
	newRec->Name = strdup(Name);
	newRec->NameHash = nameHash;
	newRec->controls = controls;
	newRec->busses = busses;
	newRec->mmBus = 0;
	newRec->mmVol = 1.0;
	newRec->portList = strdup(portList);
	/* link at end of list */
	prev = root;
	while((current = prev->next) != NULL)
		prev = current; 
	newRec->next = NULL;
	prev->next = newRec;
	return newRec;
}

inputRecord *getRecordForInput(inputRecord *root, const char *Name){
	inputRecord	*prev, *current, *newRec; 	
	uint32_t nameHash;

	nameHash = ELFHash(0, Name, strlen(Name));
	prev = root;
	while(current = prev->next){ 
		if(current->NameHash == nameHash){
			if(strcmp(Name, current->Name) == 0){
				/* Name found in list... return it. */	
				return current;
			}
		}
		prev = current; 
	}
	return NULL;
}

void releaseInputRecord(inputRecord *root, inputRecord *rec){
	inputRecord	*prev, *current; 	
    
    rec->refCnt--;
	if(rec->refCnt == 0){
		/* if reference count is zero, unhook and free the record */
		prev = root;
		while((current = prev->next) != NULL){ 
			if(current == rec){ 
				/* unhook from list */
				prev->next = current->next;
				break; 
			} 
			prev = current; 
		} 
		/* free record and strings */
		if(rec->Name)
			free(rec->Name);
		if(rec->portList)
			free(rec->portList);
		if(rec->mmList)
			free(rec->mmList);
		free(rec);
	}	
}

/******************************************************************
 *  Some handy untility functions for manipulating task
 *  lists, built ontop of the link list structure.
 ******************************************************************/
void releaseTaskRecord(taskRecord *root, taskRecord *rec){
	taskRecord *prev, *current;
    
	/* A running task should never be released to reference count 0 
	 * when the task completes, it will self-release */
	rec->refCnt--;
	if(rec->refCnt == 0){
		/* if reference count is zero, unhook and free the record */
		prev = root;
		while((current = prev->next) != NULL){ 
			if(current == rec){ 
				/* unhook from list */
				prev->next = current->next;
				break; 
			} 
			prev = current; 
		} 
		/* free record */
		if(rec->name)
			free(rec->name); 
		if(rec->userData)
			free(rec->userData);
		free(rec);
	}
}

/******************************************************************
 *  Some handy untility functions for manipulating connection
 *  lists, built ontop of the link list structure.
 ******************************************************************/
connRecord *setValuesForConn(connRecord *root, const char *Src, const char *Dest){
	/* creates a new record if entry doesn't already exist */
	connRecord	*prev, *current, *newRec; 	

	if(prev = findConnRecord(root, Src, Dest))
		return prev;
	else{
		/* not found in list... create a new entry for it */	
		newRec = calloc(1, sizeof(connRecord));
		newRec->refCnt = 1;
		newRec->src = strdup(Src);
		newRec->SrcHash = ELFHash(0, Src, strlen(Src));
		newRec->dest = strdup(Dest);
		newRec->DestHash = ELFHash(0, Dest, strlen(Dest));
		
		/* link at end of list */
		prev = root;
		while((current = prev->next) != NULL)
			prev = current; 
		newRec->next = NULL;
		prev->next = newRec;
		return newRec;
	}
}

connRecord *findConnRecord(connRecord *root, const char *Src, const char *Dest){
	connRecord	*prev, *current, *newRec; 	
	uint32_t srcHash, destHash;
	unsigned char match;

	if(Src)
		srcHash = ELFHash(0, Src, strlen(Src));
	if(Dest)
		destHash = ELFHash(0, Dest, strlen(Dest));
	prev = root;
	while(current = prev->next){ 
		match = 0;
		if(Src){
			if(current->SrcHash == srcHash){
				if(strcmp(Src, current->src) == 0)
					match = 1;
				else
					match = 0;
			}else
				match = 0;
		}
		if(Dest){
			if(current->DestHash == destHash){
				if(strcmp(Dest, current->dest) == 0)
					match = 1;
				else
					match = 0;
			}else
				match = 0;
		}
		if(match)
			return current;
		prev = current; 
	} 
	return NULL;
}

void releaseConnRecord(connRecord *root, connRecord *rec){
	connRecord *prev, *current;
    
    rec->refCnt--;
	if(rec->refCnt == 0){
		/* if reference count is zero, unhook and free the record */
		prev = root;
		while((current = prev->next) != NULL){ 
			if(current == rec){ 
				/* unhook from list */
				prev->next = current->next;
				break; 
			} 
			prev = current; 
		} 
		/* free record */
		if(rec->src)
			free(rec->src); 
		if(rec->dest)
			free(rec->dest);
		free(rec);
	}
}
