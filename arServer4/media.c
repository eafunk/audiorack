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

#include "media.h"
#include "data.h"
#include "utilities.h"
#include "dispatch.h"	// for logging of errors in server log
#include "session.h"
#include "database.h"
#include "automate.h"
#include "md5.h"
#include "tasks.h"
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pwd.h>
#include <libgen.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>
#include <string.h>
#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>
#include <fnmatch.h>

struct locals{
	uint32_t localUID;
	uint32_t plMeta;
	FILE *fp;
};

/* this is the only source code file that makes use of the gstreamer library */

GstDiscoverer *discoverer = NULL;
pthread_mutex_t discoverMutex;

uint32_t crc_table[256];

char mediaInit(void){
	GError *err = NULL;

	pthread_mutex_init(&discoverMutex, NULL);

	/* init checksum table */
	chksum_crc32gentab(crc_table);

	/* Initialize GStreamer: used to read media meta data */
	if(gst_init_check(NULL, NULL, &err)){
		if(err)
			g_error_free(err);
		discoverer = gst_discoverer_new(5 * GST_SECOND, NULL);
		return 1;
	}
	if(err)
		g_error_free(err);
	return 0;
}

void mediaShutdown(void){
	if(discoverer)
		g_object_unref(discoverer);

	pthread_mutex_destroy(&discoverMutex);
	gst_deinit();
}

char *getFilePrefixPoint(char **file){
	char *prefixList;
	char *pattern, *slice, *prefix;
	unsigned int i;
	
	/* see if any of the prefixes in the prefix list match the file path.
	 * if so, return the prefix that matches. On return, file points 
	 * to the remaining  path after the prefix, and needs to be freed. 
	 * Returns NULL with file set to NULL for no match */
	
	prefixList = GetMetaData(0, "file_prefixes", 0);
	i = 0;
	while(pattern = str_NthField(prefixList, ",", i)){
		prefix = strdup(*file);
		slice = str_prefixSpan(prefix, pattern);
		if(slice != prefix){
			free(pattern);
			*file = strdup(slice);
			*slice = 0; 
			free(prefixList);
			return prefix;
		}
		free(prefix);
		free(pattern);
		i++;
	}
	/* no prefix match: prefix substitution can not be used for this file */
	*file = NULL;
	return NULL;
}

char GetFileMD5(const char *file, unsigned char *result){
	/* note: result must point to a 16 byte array */
	FILE *fp;
	MD5_CTX md5;
	unsigned char block[8192];
	size_t len, b1, b2;
	int bSize;

	// get md5 hash of the 4K blocks 1/3 and 2/3 into the file     
	if((fp = fopen(file, "r")) == NULL)
		return 0;
	// go to the end of the file
	fseek(fp, 0, SEEK_END);
	len = ftell(fp) / 3;
	b1 = (len / 4096) * 4096;
	b2 = (len / 2048) * 4096;
	fseek(fp, b1, SEEK_SET);
	bSize = fread(block, 1, 4096, fp);
	fseek(fp, b2, SEEK_SET);
	bSize = bSize + fread(block+bSize, 1, 4096, fp);
	fclose(fp);

	MD5_Init(&md5);
	MD5_Update(&md5, block, bSize);
	MD5_Final(result, &md5);
	return 1;
}

char GetFileCRC(const char *file, unsigned char *result){
	/* note: result must point to a 16 byte array */
	FILE *fp;
	unsigned char blockA[4096], blockB[4096], blockC[4096];
	size_t fsize, len, b1, b2, b3;
	int sizeA, sizeB, sizeC;
	struct packing{	// storage is big-endian byte order
		uint32_t		fsize;
		uint32_t		crcA;
		uint32_t		crcB;
		uint32_t		crcC;
	};
	
	// get alternate hash of the 4K blocks 1/4, 1/2 and 3/4 into the file  
	if((fp = fopen(file, "r")) == NULL)
		return 0;
	// go to the end of the file
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	len = fsize / 4;
	b1 = (len / 4096) * 4096;
	b2 = (len / 2048) * 4096;
	b3 = (3 * len / 4096) * 4096;
	fseek(fp, b1, SEEK_SET);
	sizeA = fread(blockA, 1, 4096, fp);
	fseek(fp, b2, SEEK_SET);
	sizeB = fread(blockB, 1, 4096, fp);
	fseek(fp, b3, SEEK_SET);
	sizeC = fread(blockC, 1, 4096, fp);
	fclose(fp);
	
	((struct packing *)result)->crcA = htonl(chksum_crc32(blockA, sizeA, crc_table));
	((struct packing *)result)->crcB = htonl(chksum_crc32(blockB, sizeB, crc_table));
	((struct packing *)result)->crcC = htonl(chksum_crc32(blockC, sizeC, crc_table));
	((struct packing *)result)->fsize = htonl(fsize & 0xffffffff);
	return 1;
}

char *GetFileHash(const char *path){
	unsigned char md5Ptr[16];
	char buf[32];
	char *str;
	
	if(GetFileMD5(path, md5Ptr)){
		str = NULL;
		for(int i=0; i<16; i++){
			snprintf(buf, sizeof(buf), "%02x", md5Ptr[i]);
			str_appendstr(&str, buf);
		}
		if(GetFileCRC(path, md5Ptr)){
			for(int i=0; i<16; i++){
				snprintf(buf, sizeof(buf), "%02x", md5Ptr[i]);
				str_appendstr(&str, buf);
			}	
			return str;				
		}
		free(str);
	}
	return NULL;	
}

unsigned char CheckFileHashMatch(const char *path, const char *hash){	
	unsigned char md5Ptr[16];
	char buf[32];
	char *fHash;
	
	if(hash == NULL)
		return 0;
	if(fHash = GetFileHash(path)){
		if(strcmp(hash, fHash) == 0){
			// Hash code agrees with database
			free(fHash);
			return 1;
		}
		free(fHash);
	}
	return 0;
}

char *getScriptFromFile(const char *file, unsigned char silent){
	FILE *fp;
	char str[4096];
	char *tmp = NULL;
	char *result = NULL;

	if(!(fp = fopen(file, "r"))){
		if(!silent){
			str_setstr(&tmp, "[main] getScriptFromFile-");
			str_appendstr(&tmp, file);
			str_appendstr(&tmp, ": couldn't open file for reading");
			serverLogMakeEntry(tmp);
			free(tmp);
		}
		return result;
	}
	// read upto 4K of the file
	while(fgets(str, sizeof(str), fp)){
		str_appendstr(&result, str);
		if(strlen(result) >= 8*1024)
			// limit to 8KiB
			break;
	}
	fclose(fp);
	return result;
}

unsigned char filePLDetermineType(FILE *fpl)
{
	char tag[12];
	
	if(fread(tag, 11, 1, fpl) == 1){
		tag[11] = 0; // null terminate
		if(strcmp(tag, "Type\tfilepl") == 0){
			// we have a Audiorack native .fpl file here... 
			rewind(fpl);
			return 1;
		}
		tag[10] = 0;
		if(strcmp(tag, "[playlist]") == 0){
			// we have a .pls type file playlist... 
			rewind(fpl);
			return 2;
		}
		tag[7] = 0;
		if(strcmp(tag, "#EXTM3U") == 0){
			// we have a .m3u type file playlist... 
			rewind(fpl);
			return 3;
		}
	}
	rewind(fpl);
	return 0;
}

char fplPLGetNextMeta(FILE *fp, uint32_t UID)
{
	char line[4096];
	char *save_pointer;
	char *key;
	
	while(fgets(line, sizeof(line), fp)){
		// strip LF and CR off then end
		strtok_r(line, "\r", &save_pointer);
		strtok_r(line, "\n", &save_pointer);
		key = strtok_r(line, "\t", &save_pointer);
		if((key == NULL) || (*key == 0) || isspace(*key)){
			// we are done!
			return 0; // OK
		}else{
			// set meta data to key/value pair
			if((save_pointer != NULL) && (*save_pointer != 0)){
				if(UID) 
					SetMetaData(UID, key, save_pointer);
			}
		}
	}
	// if we got here something bad happened... the header must be
	// corrupt, or we reached the end of the list
	return -1; // error
}

char plsPLGetNextMeta(FILE *fp, uint32_t UID, char *filePath)
{
	char line[4096], path[4096];
	char *save_pointer;
	char *key;
	char *enc;
	char *value = NULL;
	char *dir;
	char *copy;

	if(filePath){
		// truncate path to containing directory
		copy = strdup(filePath);
		dir = dirname(copy);
		while(fgets(line, sizeof(line), fp)){
			// strip LF and CR off the end
			strtok_r(line, "\r", &save_pointer);
			strtok_r(line, "\n", &save_pointer);
			key = strtok_r(line, "=", &save_pointer);
			if((key != NULL) && (*key != 0) && !isspace(*key)){
				if((save_pointer != NULL) && (*save_pointer != 0)){
					if(UID){
						if(strstr(key, "Length") == key){
							// we have a length property
							SetMetaData(UID, "Duration", save_pointer);
							continue;
						}
						if(strstr(key, "Title") == key){
							// we have a name property
							SetMetaData(UID, "Name", save_pointer);
							continue;
						}
						if(strstr(key, "File") == key){
							// we have a file property - full path, partial path or URL?
							if((strstr(save_pointer, "http://") == save_pointer) || (strstr(save_pointer, "HTTP://") == save_pointer)){
								// URL
								SetMetaData(UID, "URL", save_pointer);
								continue;
							}
							if(save_pointer[0] == directoryToken){
								// full path
								enc = uriEncodeKeepSlash(save_pointer);
								str_setstr(&value, "file://");
								str_appendstr(&value, enc);
								free(enc);
								SetMetaData(UID, "URL", value);
								free(value);
								value = NULL;
							}else{
								// relative path
								str_setstr(&value, dir);
								str_appendstr(&value, directoryTokenStr);
								str_appendstr(&value, save_pointer);
								enc = uriEncodeKeepSlash(value);
								free(value);
								value = NULL;
								str_setstr(&value, "file://");
								str_appendstr(&value, enc);
								free(enc);
								SetMetaData(UID, "URL", value);
								free(value);
								value = NULL;
							}
						}
					}
				}
			}else{
				// blank line: end of record
				free(copy);
				return 0;
			}
		}
		free(copy);
	}
	return -1;
}

void plsPLGetProperties(FILE *fp, uint32_t UID)
{
	char line[4096];
	char *save_pointer;
	char *key;
	float dur, val;
	char *tmp;
	char *url_str;
	char *path;
	
	// set type
	SetMetaData(UID, "Type", "filepl");
	
	// set name to file name extracted from URL
	url_str = GetMetaData(UID, "URL", 0);
    if(tmp = str_NthField(url_str, "://", 1)){
		// ignore host, if any
		if(path = strchr(tmp, '/')){
			path = uriDecode(path);
		}else{
			path = uriDecode(tmp);			
		}
		free(tmp);
		tmp = basename(path);
		SetMetaData(UID, "Name", tmp);
		free(path);
		
		// calculate and set duration
		dur = 0.0;
		while(fgets(line, sizeof(line), fp)){
			// strip LF and CR off the end
			strtok_r(line, "\r", &save_pointer);
			strtok_r(line, "\n", &save_pointer);
			key = strtok_r(line, "=", &save_pointer);
			if((key != NULL) && (*key != 0) && !isspace(*key)){
				if((save_pointer != NULL) && (*save_pointer != 0)){
					if(strstr(key, "Length") == key){
						// we have a length property
						if((val = atof(save_pointer)) > -1) 
							dur = dur + val;
					}
				}
			}
		}
	}
	tmp = fstr(dur, 1);
	SetMetaData(UID, "Duration", tmp);
	free(tmp);
}

char m3uPLGetNextMeta(FILE *fp, uint32_t UID, char *filePath)
{
	char line[4096];
	char *save_pointer;
	char *value = NULL;
	char *tmp;
	char *enc;
	char *dir;
	char *copy;
	char ok;

	ok = -1;
	if(filePath){
		// truncate path to containing directory
		copy = strdup(filePath);
		dir = dirname(copy);
		while(fgets(line, sizeof(line), fp)){
			// strip LF and CR off the end
			strtok_r(line, "\r", &save_pointer);
			strtok_r(line, "\n", &save_pointer);
			if(strlen(line) > 1){
				if(UID){
					if(strstr(line, "#EXTINF:") == line){
						// extended properties tag: strip "#EXTINF:" off the front
						strtok_r(line, ":", &save_pointer);
						if(tmp = save_pointer){
							// and get the value that follows upto the first comma, if any
							strtok_r(tmp, ",", &save_pointer);
							if(strlen(tmp)){
								// we have a length property
								SetMetaData(UID, "Duration", tmp);
								if(tmp = save_pointer){
									strtok_r(tmp, ",", &save_pointer);
									// title property should follow
									if(strlen(tmp))
										SetMetaData(UID, "Name", tmp);
								}
							}
						}
					}else{
						// we have a file property - full path, partial path or URL?
						if((strstr(line, "http://") == line) || (strstr(line, "HTTP://") == line)){
							// URL
							SetMetaData(UID, "URL", line);
							continue;
						}
						if(line[0] == directoryToken){
							// full path
							enc = uriEncodeKeepSlash(line);
							str_setstr(&value, "file://");
							str_appendstr(&value, enc);
							free(enc);
							SetMetaData(UID, "URL", value);
							free(value);
							value = NULL;
						}else{
							// relative path
							str_setstr(&value, dir);
							str_appendstr(&value, directoryTokenStr);
							str_appendstr(&value, line);
							enc = uriEncodeKeepSlash(value);
							free(value);
							value = NULL;
							str_setstr(&value, "file://");
							str_appendstr(&value, enc);
							free(enc);
							SetMetaData(UID, "URL", value);
							free(value);
							value = NULL;
						}
						ok = 0;
					}
				}
			}else{
				// blank line: end of record
				free(copy);
				return 0;
			}
		}
		free(copy);
	}
	return ok;
}
void m3uPLGetProperties(FILE *fp, uint32_t UID)
{
	char line[4096];
	char *save_pointer;
	char *val;
	float dur, fv;
	char *url_str;
	char *path;
	char *tmp;
	
	dur = 0.0;
	// set type
	SetMetaData(UID, "Type", "filepl");
	// set name to file name extracted from URL
	url_str = GetMetaData(UID, "URL", 0);
    if(tmp = str_NthField(url_str, "://", 1)){
		// ignore host, if any
		if(path = strchr(tmp, '/'))
			path = uriDecode(path);
		else
			path = uriDecode(tmp);
		free(tmp);
		tmp = basename(path);
		SetMetaData(UID, "Name", tmp);	
		free(path);
		
		// calculate and set duration
		while(fgets(line, sizeof(line), fp)){
			// strip LF and CR off the end
			strtok_r(line, "\r", &save_pointer);
			strtok_r(line, "\n", &save_pointer);
			if(strstr(line, "#EXTINF:") == line){
				// strip "#EXTINF:" off the front
				strtok_r(line, ":", &save_pointer);
				if(val = save_pointer){
					// and get the value that follows upto the first comma, if any
					val = strtok_r(val, ",", &save_pointer);
					if(strlen(val)){
						// we have a length property
						if((fv = atof(val)) > -1)
							dur = dur + fv;
					}
				}
			}
		}
	}
	tmp = fstr(dur, 1);
	SetMetaData(UID, "Duration", tmp);
	free(tmp);
}

gchar *gvalToString(GValue *val){
	gchar *str;
	if(G_VALUE_HOLDS_STRING(val))
		str = g_value_dup_string(val);
	else
		str = gst_value_serialize(val);
}

static void tagToMetadata(const GstTagList *tags, const gchar *tag, gpointer user_data) {
	GValue val = { 0, };
	gchar *str;
	const char *prop;
	uint32_t uid = GPOINTER_TO_UINT(user_data);

	gst_tag_list_copy_value(&val, tags, tag);
	prop = gst_tag_get_nick(tag);
		
	str = NULL;
	if(!strcmp(prop, GST_TAG_TITLE)){
		str = gvalToString(&val);
		SetMetaData(uid, "Name", str);
	}else if(!strcmp(prop, GST_TAG_ARTIST)){
		str = gvalToString(&val);
		SetMetaData(uid, "Artist", str);
	}else if(!strcmp(prop, GST_TAG_ALBUM)){
		str = gvalToString(&val);
		SetMetaData(uid, "Album", str);
	}else if(!strcmp(prop, GST_TAG_TRACK_NUMBER)){
		str = gvalToString(&val);
		SetMetaData(uid, "Track", str);	
	}else if(!strcmp(prop, GST_TAG_ALBUM_ARTIST)){
		str = gvalToString(&val);
		SetMetaData(uid, "AlbumArtist", str);	
	}else if(!strcmp(prop, GST_TAG_ISRC)){
		str = gvalToString(&val);
		SetMetaData(uid, "ISRC", str);	
	}
	if(str)
		g_free(str);
		
	g_value_unset(&val);
}

char hasAudioTrack(GstDiscovererStreamInfo *info){
	GstDiscovererStreamInfo *next;
	const char *nick;
	char result = 0;

	if(!info)
		return result;

	nick = gst_discoverer_stream_info_get_stream_type_nick(info);
	if(!strcmp(nick, "audio"))
		result = 1;

	next = gst_discoverer_stream_info_get_next(info);
	if(next){
		nick = gst_discoverer_stream_info_get_stream_type_nick(next);
		if(!strcmp(nick, "audio"))
			result = 1;		
		gst_discoverer_stream_info_unref(next);
		
	}else if(GST_IS_DISCOVERER_CONTAINER_INFO(info)){
		GList *tmp, *streams;

		streams = gst_discoverer_container_info_get_streams(GST_DISCOVERER_CONTAINER_INFO(info));
		for(tmp = streams; tmp; tmp = tmp->next) {
			GstDiscovererStreamInfo *tmpinf = (GstDiscovererStreamInfo *)tmp->data;
			nick = gst_discoverer_stream_info_get_stream_type_nick(tmpinf);
			if(!strcmp(nick, "audio"))
				result = 1;		
		}
		gst_discoverer_stream_info_list_free(streams);
	}
	return result;
}

void handle_discovered(GstDiscoverer *discoverer, GstDiscovererInfo *info, uint32_t UID){
	GstDiscovererResult result;
	const gchar *uri;
	const GstTagList *tags;
	GstDiscovererStreamInfo *sinfo;
	const GstStructure *s;
	char *tmp;
	char buf[4096];
	char hasAudio = 0;
		
	uri = gst_discoverer_info_get_uri(info);
	result = gst_discoverer_info_get_result(info);
	switch(result) {
		case GST_DISCOVERER_URI_INVALID:
			snprintf(buf, sizeof buf, "[media] handle_discovered-tags: Invalid URI for %s", uri);
			serverLogMakeEntry(buf);
			break;
		case GST_DISCOVERER_ERROR:
			snprintf(buf, sizeof buf, "[media] handle_discovered-%s: Discovery error", uri);
			serverLogMakeEntry(buf);
			break;
		case GST_DISCOVERER_TIMEOUT:
			snprintf(buf, sizeof buf, "[media] handle_discovered-%s: Discovery timeout", uri);
			serverLogMakeEntry(buf);
			break;
		case GST_DISCOVERER_BUSY:
			snprintf(buf, sizeof buf, "[media] handle_discovered-%s: Invalid URI", uri);
			serverLogMakeEntry(buf);
			break;
		case GST_DISCOVERER_MISSING_PLUGINS:{
			s = gst_discoverer_info_get_misc(info);
			tmp = gst_structure_to_string(s);
			snprintf(buf, sizeof buf, "[media] handle_discovered-%s: missing gstreamer plugin %s", uri, tmp);
			serverLogMakeEntry(buf);
			free(tmp);
			break;
		}
	}

	if(result != GST_DISCOVERER_OK){
		snprintf(buf, sizeof buf, "[media] handle_discovered-%s:failed to read tags", uri);
		serverLogMakeEntry(buf);
		SetMetaData(UID, "Missing", "1");
		return;
	}

	/* No error: Show the retrieved information */
	tmp = fstr(gst_discoverer_info_get_duration(info) / (double)GST_SECOND, 2);
	SetMetaData(UID, "Duration", tmp);
	free(tmp);

	if(gst_discoverer_info_get_seekable(info))
		SetMetaData(UID, "Seekable", "1");
	else
		SetMetaData(UID, "Seekable", "0");

	tags = gst_discoverer_info_get_tags(info);
	if(tags)
		gst_tag_list_foreach(tags, tagToMetadata, GUINT_TO_POINTER(UID));

	sinfo = gst_discoverer_info_get_stream_info(info);
	
	if(sinfo){
		hasAudio = hasAudioTrack(sinfo);
		gst_discoverer_stream_info_unref(sinfo);
	}
	if(hasAudio)
		SetMetaData(UID, "Missing", "0");
	else
		SetMetaData(UID, "Missing", "1");
}

void GetGstDiscoverMetaData(uint32_t UID, const char *url_str){
	GstDiscovererInfo *info;
// GstDiscoverer *discoverer;
		
	pthread_mutex_lock(&discoverMutex);

// discoverer = gst_discoverer_new(5 * GST_SECOND, NULL);
// g_object_ref(discoverer);
		
	if(info = gst_discoverer_discover_uri(discoverer, url_str, NULL)){
		handle_discovered(discoverer, info, UID);

	}else{
		char buf[4096];
		SetMetaData(UID, "Missing", "1");
		snprintf(buf, sizeof buf, "[media] GetGstDiscoverMetaData-%s:failed to get media tag", url_str);
		serverLogMakeEntry(buf);
	}

//	g_object_unref(discoverer);
	pthread_mutex_unlock(&discoverMutex);
}

void GetInputMetaData(uint32_t UID, const const char *url){
	inputRecord *rec = NULL;
	char *name, *tmp;
	
	if(tmp = str_NthField(url, ":///", 1)){
		name = uriDecode(tmp);
		free(tmp);
		pthread_rwlock_rdlock(&inputLock);		
		if(rec = getRecordForInput((inputRecord *)&inputList, name)){
			SetMetaData(UID, "Name", name);
			SetMetaData(UID, "portList", rec->portList);
			if(rec->mmList)
				SetMetaData(UID, "MixMinusList", rec->mmList);
		}
		pthread_rwlock_unlock(&inputLock);		
		free(name);
	}
	if(rec)
		SetMetaData(UID, "Missing", "0");
	else
		SetMetaData(UID, "Missing", "1");
}
/*
void GetIAXMetaData(uint32_t UID, CFURLRef url)
{
	unsigned char isAbsolutePath;
	CFStringRef input_str;
	char buf[256];
	char *callerid;
	int line;

	input_str = CFURLCopyStrictPath(url, &isAbsolutePath);
	if(input_str == NULL){
		SetMetaData(UID, "Missing", "1");
		return;
	}
	CFStringGetCString(input_str, buf, sizeof(buf), kCFStringEncodingUTF8);
	CFRelease(input_str);

    // get iax telephone line info
	line = atoi(buf);
	if(iaxp_get_callerid(line, buf, sizeof(buf)))
		callerid = buf;
	else
		callerid = "unknown";
	line++;
	if(iaxp_is_ARS_codec(line))
		SetMetaData(UID, "Name", "Remote "+istr(line));
	else
		SetMetaData(UID, "Name", "TelLine "+istr(line));
	SetMetaData(UID, "Comment", string(callerid));
}
*/
void GetFileMetaData(uint32_t UID, const char *url){
	char *copy;
	char *path = NULL;
	char *prefix = NULL;
	char *tmp, *enc;
	FILE *fp = NULL;
	unsigned char md5Ptr[16], crcPtr[16];
	char buf[4096];
	uint32_t dbID;
	
    if(tmp = str_NthField(url, "://", 1)){
		// ignore host, if any
		if(path = strchr(tmp, '/'))
			path = uriDecode(path);
		else
			path = uriDecode(tmp);
		free(tmp);
		// check if it is a playlist file
		if(fp = fopen(path, "rb")){
			switch(filePLDetermineType(fp)){
				case 1:		// .fpl file (AudioRack file playlist)
					fplPLGetNextMeta(fp, UID);
					goto finish;

				case 2:		// .pls file
					plsPLGetProperties(fp, UID);
					goto finish;

				case 3:		// .m3u file
					m3uPLGetProperties(fp, UID);
					goto finish;
			}
			fclose(fp);
			fp = NULL;
		}
	
		// not a playlist file... see if it is an audio file we can play.
		// set name tag to the file name... updated later if there is a name meta tag in the file.
		copy = strdup(path);
		tmp = basename(copy);
		SetMetaData(UID, "Name", tmp);
		free(copy);
		
		tmp = path;
		if(prefix = getFilePrefixPoint(&tmp)){
			SetMetaData(UID, "Prefix", prefix);
			SetMetaData(UID, "Path", tmp);
			if(tmp != path)
				free(tmp);
		}

		if(tmp = GetFileHash(path)){
			SetMetaData(UID, "Hash", tmp);
			SetMetaData(UID, "Missing", "0");
			// check database for a matching file (FileID/Mount/MD5)
			dbID = IDSearchMarkedFile(path, tmp);
			free(tmp);
			if(dbID){
				snprintf(buf, sizeof(buf), "item:///%u", (unsigned int)dbID);
				GetItemMetaData(UID, buf);
			}
			else
				GetGstDiscoverMetaData(UID, url);
		}else{
			SetMetaData(UID, "Missing", "1");
			goto finish;
		}	

		// check for an associated script text file
		str_appendstr(&path, ".txt");
		if(tmp = getScriptFromFile(path, 1)){
			if(enc = uriEncodeKeepSpace(tmp)){
				SetMetaData(UID, "Script", enc);
				free(enc);
			}
			free(tmp);
		}	
	}else{
		SetMetaData(UID, "Missing", "1");
	}
	
finish:
	if(prefix)
		free(prefix);
	if(path)
		free(path);
	if(fp)
		fclose(fp);
}

void GetURLMetaData(uint32_t UID, const char *url){
	char *type = NULL;
	unsigned int idx;

	if(UID == 0)
		return;
    	
	if(type = str_NthField(url, ":", 0)){
		// make lower case
		for(idx = 0; idx < strlen(type); idx++)
			type[idx] = tolower(type[idx]);

		SetMetaData(UID, "Type", type);
		
		if(!strcmp(type, "file"))
			GetFileMetaData(UID, url);
		else if(!strcmp(type, "item"))
			GetItemMetaData(UID, url);
		else if(!strcmp(type, "input"))
			GetInputMetaData(UID, url);
//		else if(!strcmp(type, "iax"))
//			GetIAXMetaData(UID, url);
		else if(!strcmp(type, "stop")){
			SetMetaData(UID, "Name", "--- Play List Stop ---");	
			SetMetaData(UID, "Missing", "0");
		}else
			GetGstDiscoverMetaData(UID, url);
	}

	if(type)
		free(type);
}

uint32_t LoadJackPlayer(int pNum, const char *url_str, uint32_t UID){
	/* note: url format for connection list is, by example:
	 * jack:///client:port1+client:port2&client:port3+client:port4
	 * connects the input's first channel to client port1 AND port2, then
	 * connects the input's second channel to client port3 AND port4. */
	 
	inChannel *instance;
	uint32_t locUID, result;
	jack_port_t **in_port;
	int i, c, cmax;
	unsigned char isConnected;
	char *portName, *portList, *chanList, *sourceName, *tmp;
	char *decodedName;

	result = 0;
    instance = &mixEngine->ins[pNum];
	if(UID == 0){
		locUID = createMetaRecord(url_str, NULL);
	}else{ 
		locUID = UID;
		retainMetaRecord(UID);
	}
	instance->UID = locUID;
	isConnected = 0;
	sourceName = NULL;
	
	// set up mixer channel
	double val;
	instance->busses = GetMetaInt(instance->UID, "def_bus", NULL);
	if(instance->busses == 0)
		instance->busses = def_busses;

	val = GetMetaFloat(instance->UID, "Volume", NULL);
	if((val == 0.0) || (val > 10)) 
		val = def_vol;
	instance->vol = val;
	
	instance->fadePos = GetMetaFloat(instance->UID, "FadeOut", NULL);
	instance->fadeTime = GetMetaFloat(instance->UID, "FadeTime", NULL);
		
	/* make jack connections */
	if(portList = str_NthField(url_str, ":///", 1)){
		in_port = instance->in_jPorts;
		cmax = mixEngine->chanCount;
		for(c=0; c<cmax; c++){
			if(chanList = str_NthField(portList, "&", c)){
				i = 0;
				while(portName = str_NthField(chanList, "+", i)){
					if(strlen(portName)){
						decodedName = uriDecode(portName);
						if(!jack_connect(mixEngine->client, decodedName, jack_port_name(*in_port))){
							isConnected++;
							if(!sourceName)
								sourceName = str_NthField(decodedName, ":", 0);
						}
						free(decodedName);
					}
					free(portName);
					i++;
				}
				free(chanList);
			}
			in_port++;
		}
		free(portList);
	}
	if(isConnected){
		/* set name property to connected application name */
		if(sourceName){
			SetMetaData(locUID, "Name", sourceName);
			free(sourceName);
		}
	
		tmp = hstr(ctl_vol | ctl_fade, 8);
		SetMetaData(instance->UID, "Controls", tmp); 
		free(tmp);

		result = locUID;
	}else{
		/* this will cause the deletion of the UID record as well by the 
		 * playerChangeWatcher dispatch thread */
		setInChanToDefault(instance);
		instance->status = status_empty;
	}
	return result;
}

uint32_t LoadInputPlayer(int pNum, const char *url_str, uint32_t UID){
	inputRecord *rec;
	inChannel *instance;
	uint32_t locUID, result, busses, controls;
	jack_port_t **in_port;
	int i, c, cmax;
	unsigned char isConnected;
	char *name, *tmp;
	char *portList, *mmList, *chanList, *portName, *decodedName;

	busses = 0;
	result = 0;
    instance = &mixEngine->ins[pNum];
	if(UID == 0){
		locUID = createMetaRecord(url_str, NULL);
	}else{ 
		locUID = UID;
		retainMetaRecord(UID);
	}
	instance->UID = locUID;
	isConnected = 0;
	portList = NULL;
	mmList = NULL;
	
	// set up mixer channel
	double val;

	val = GetMetaFloat(locUID, "Volume", NULL);
	if((val == 0.0) || (val > 10)) 
		val = def_vol;
	instance->vol = val;

	instance->fadePos = GetMetaFloat(locUID, "FadeOut", NULL);
	instance->fadeTime = GetMetaFloat(locUID, "FadeTime", NULL);
	
	/* make jack connections */
	if(tmp = str_NthField(url_str, ":///", 1)){
		name = uriDecode(tmp);
		free(tmp);
		pthread_rwlock_rdlock(&inputLock);
		if(rec = getRecordForInput((inputRecord *)&inputList, name)){
			busses = rec->busses;
			controls = rec->controls;
			portList = strdup(rec->portList);
			SetMetaData(locUID, "portList", portList);
			if(rec->mmList){
				mmList = strdup(rec->mmList);
				SetMetaData(locUID, "MixMinusList", mmList);
			}
		}
		pthread_rwlock_unlock(&inputLock);	
		if(rec && portList){
			in_port = instance->in_jPorts;
			cmax = mixEngine->chanCount;
			for(c=0; c<cmax; c++){
				if(chanList = str_NthField(portList, "&", c)){
					i = 0;
					while(portName = str_NthField(chanList, "+", i)){
						if(strlen(portName)){
							decodedName = uriDecode(portName);
							if(!jack_connect(mixEngine->client, decodedName, jack_port_name(*in_port))){
								isConnected++;
							}
							free(decodedName);
						}
						free(portName);
						i++;
					}
					free(chanList);
				}
				in_port++;
			}
		}
		if(portList)
			free(portList);
	}
	/* NOTE: mmList connections are handled by the calling function: LoadPlayer() */
	if(isConnected){
		instance->persist = persistConnected;
		instance->busses = busses;
		if(instance->busses == 0)
			instance->busses = def_busses;
			
		/* set name property to connected application name */
		if(name)
			SetMetaData(locUID, "Name", name);
		
		tmp = hstr(ctl_vol | ctl_fade, 8);
		SetMetaData(locUID, "Controls", tmp); 
		free(tmp);

		result = locUID;
		
	}else{
		/* this will cause the deletion of the UID record as well by the 
		 * playerChangeWatcher dispatch thread */
		setInChanToDefault(instance);
		instance->status = status_empty;
	}
	if(name)
		free(name);
	return result;
}

uint32_t LoadURLPlayer(int pNum, const char *url_str, uint32_t UID){
	inChannel *instance;
	char command[1024];
	char *tmp;
	float vol;	
	char *wdir, *bin;
	int i, fd;
	uint32_t locUID, result;
	
	struct execRec{
		char **argv;
		pid_t child;
	} *recPtr;
    
    result = 0;
    instance = &mixEngine->ins[pNum];

	if(UID == 0){
		locUID = createMetaRecord(url_str, NULL);
		GetURLMetaData(locUID, url_str);
	}else{ 
		locUID = UID;
		retainMetaRecord(UID);
	}
	instance->UID = locUID;

	// allocate record for execution var's
	recPtr = (struct execRec *)calloc(1, sizeof(struct execRec));
	// make array for holding arguments
	recPtr->argv = (char **)calloc(mixEngine->chanCount+7, sizeof(char*));
	// populate argument strings
	bin = GetMetaData(0, "file_bin_dir", 0);
	if(strlen(bin)){
		if(strrchr(bin, directoryToken) != (bin + strlen(bin)))
			// no trailing slash... include it
			str_appendstr(&bin, "/arPlayer4");
		else
			// already has training slashq
			str_appendstr(&bin, "arPlayer4");
		recPtr->argv[0] = bin;
	}else{
		free(bin);
		recPtr->argv[0] = strdup("/opt/audiorack/bin/arPlayer4");
	}
	
	recPtr->argv[1] = strdup("-u");
	recPtr->argv[2] = strdup(url_str);
	recPtr->argv[3] = strdup(mixEngine->ourJackName);
	recPtr->argv[4] = ustr(pNum);
	recPtr->argv[5] = NULL;
	for(i=0; i<mixEngine->chanCount; i++){
		if(i < (mixEngine->chanCount - 1))
			snprintf(command, sizeof command, "%s:In%dch%d&", mixEngine->ourJackName, pNum, i);
		else
			snprintf(command, sizeof command, "%s:In%dch%d", mixEngine->ourJackName, pNum, i);
		str_appendstr(&recPtr->argv[5], command);
	}
	recPtr->argv[6] = NULL;

	// set up mixer channel
	double val;
	instance->busses = GetMetaInt(instance->UID, "def_bus", NULL);
	if(instance->busses == 0)
		instance->busses = def_busses;

	val = GetMetaFloat(instance->UID, "Volume", NULL);
	if((val == 0.0) || (val > 10)) 
		val = def_vol;
	instance->vol = val;
	
	tmp = hstr(ctl_vol | ctl_fade, 8);
	SetMetaData(instance->UID, "Controls", tmp); 
	free(tmp);
	
	instance->fadePos = GetMetaFloat(instance->UID, "FadeOut", NULL);
	instance->fadeTime = GetMetaFloat(instance->UID, "FadeTime", NULL);
	
	// fork and execute;
	if((recPtr->child = fork()) < 0)
		goto end;
	else if(recPtr->child == 0){
		// We are the forked child
		
		// set working dir as determined at arserver startup
		if(strlen(wdir_path) > 0)
			chdir(wdir_path);

		// redirect the standard descriptors to /dev/null
		fd = open("/dev/null", O_RDONLY);
		if(fd != STDIN_FILENO){
			dup2(fd, STDIN_FILENO);
			close(fd);
		}
		fd = open("/dev/null", O_WRONLY);
		if(fd != STDOUT_FILENO) {
			dup2(fd, STDOUT_FILENO);
			close(fd);
		}
		fd = open("/dev/null", O_WRONLY);
		if(fd != STDERR_FILENO) {
			dup2(fd, STDERR_FILENO);
			close(fd);
		}
		// close all other file descriptors
		for(fd=(getdtablesize()-1); fd >= 0; --fd){
			if((fd != STDERR_FILENO) && (fd != STDIN_FILENO) && (fd != STDOUT_FILENO))
				close(fd); // close all descriptors we are not interested in
		}
		// unblock all signals and set to default handlers
		sigset_t sset;
		sigemptyset(&sset);
		pthread_sigmask(SIG_SETMASK, &sset, NULL);

		// obtain a new process group 
		setsid();

		// and run...	
		execvp(recPtr->argv[0], recPtr->argv);
		// if execution fails...
		i = 0;
		while(recPtr->argv[i]){
			free(recPtr->argv[i]);
			i++;
		}
		free(recPtr->argv);
		free(recPtr);
		exit(0);
	}
	// Continuation of the parent here...

	instance->attached = recPtr->child;

	result = locUID;

end:
	if(!result){
		setInChanToDefault(instance);
		instance->status = status_empty;
	}
	i = 0;
	while(recPtr->argv[i]){
		free(recPtr->argv[i]);
		i++;
	}

	free(recPtr->argv);
	free(recPtr);

	return result;
}


uint32_t LoadDBItemPlayer(int *pNum, const char *url_str, uint32_t UID){
	uint32_t localUID;
	char *tmp;

	// get metadata for URL
	// create a meta data record to hold results
	if(UID == 0){
		localUID = createMetaRecord(url_str, NULL);
		// fill the metadata record
		GetURLMetaData(localUID, url_str);
	}else{
		localUID = UID;
		retainMetaRecord(localUID);
	}
			
	// make sure we have resolved the URL into something other than a database item again.
	tmp = GetMetaData(localUID, "Type", 0);
	if(strcmp(tmp, "item") == 0){
		// resolved back into a db item... done with the metadata record... delete
		releaseMetaRecord(localUID);
		free(tmp);
		return 0;	
	}
	
	// handle according to new type
	if(strcmp(tmp, "task") == 0){
		// try running the task
		dbTaskRunner(localUID, 1);
		releaseMetaRecord(localUID);
		*pNum = -1; // not actually in a player
		free(tmp);
		return 0;
	}

	if(strcmp(tmp, "playlist") == 0){
		// ***** plistaylists can't be loaded into a player! do nothing
		releaseMetaRecord(localUID);
		*pNum = -1; // not actually in a player
		free(tmp);
		return 0;
	}
	free (tmp);
	
	if(!GetMetaInt(localUID, "Missing", NULL)){
		// And make sure the URL has changed so we don't end up in a recursive loop!
		tmp = GetMetaData(localUID, "URL", 0);
		if(strcmp(tmp, url_str) == 0){
			// resolved back into the same URL... done with the metadata record... delete
			releaseMetaRecord(localUID);
			free(tmp);
			return 0;	
		}
		// re-enter the load player cycle with the newly resoved URL
		if(LoadPlayer(pNum, tmp, localUID, 1)){
			// and we are done!
			releaseMetaRecord(localUID);
			free(tmp);
			return localUID;
		}
	}
	// failed... done with the metadata record... delete
	releaseMetaRecord(localUID);
	return 0;
}

uint32_t LoadPlayer(int *pNum, const char *url_str, uint32_t UID, unsigned char noCheck){
	char *tmp;
	char *type;
	char rval;
	int i, maxp, p, c, cmax;
	uint32_t idx;
	inChannel *instance;
    uint32_t result;
    char *portName, *chanList, *mmList;
	jack_port_t **port;
	
	result = 0;
	p = -1;
	if(url_str == NULL)
		return 0;
	
	if(strlen(url_str) < 5)
		return 0;
	
	if((*pNum >= 0) && (!checkPnumber(*pNum)))
        return 0;
	if(type = str_NthField(url_str, ":", 0)){
		// make lower case
		for(idx = 0; idx < strlen(type); idx++)
			type[idx] = tolower(type[idx]);
		
		if(*pNum < 0){
			// load next available player		
			maxp = GetMetaInt(0, "client_players_visible", NULL);
			
			maxp = 0;
			if(maxp <= 0)
				maxp = 8;
			if(maxp >= mixEngine->inCount)
				maxp = mixEngine->inCount;
				
			for(i = 0; i < maxp; i++){
				p = lastp + i;
				if(p >= maxp)
					p = p - maxp;
				instance = &mixEngine->ins[p];
				if(!instance->status && !instance->UID){
					instance->status = status_loading;
					break;
				}
				instance++;
			}
			if(i == maxp){
				// no available players
				*pNum = -1;
				p = -1;
				goto bail;
			}else
				*pNum = p;
		}else{
			instance = &mixEngine->ins[*pNum];
			if(!noCheck){
				if(!instance->status && !instance->UID){
					instance->status = status_loading;
				}else{
					// player is busy
					goto bail; 
				}
			}
		}

		if(!strcmp(type, "input"))
			result = LoadInputPlayer(*pNum, url_str, UID);
		else if(!strcmp(type, "jack"))
			result = LoadJackPlayer(*pNum, url_str, UID);
/*!!!		else if(!strcmp(type, "iax"))
			result = LoadIAXPlayer(*pNum, url_str, UID);
*/		else if(!strcmp(type, "item"))
			result = LoadDBItemPlayer(pNum, url_str, UID);
		else
			result = LoadURLPlayer(*pNum, url_str, UID);
		
		
		if(result){
			tmp = GetMetaData(result, "Type", 0);
			if(!strlen(tmp)){
				// set type if not already set...
				SetMetaData(result, "Type", type);
				free(tmp);
			}else{
				// if type has already been set, i.e. LoadDBItem may 
				// have set the type based on database properties...
				free(type);
				type = tmp;				
			}
			if(!strcmp(type, "file") && checkPnumber(*pNum)){
				// Handle apl files, if any
				instance = &mixEngine->ins[*pNum];
				FILE **fpp = &instance->aplFile;	// gcc bug work around
				rval = associatedPLOpen(url_str, fpp);
				if(rval >= 0){
					instance-> aplFPmatch = rval;
					// find the next record after 0.1 sec, used to exclude any
					// eronious item at 0.0 second offset (invalid offset time)
					instance->nextAplEvent = associatedPLNext(instance->aplFile, 0.1);
				}
			}
			
			// send out metadata change notifications
			notifyData	data;
			data.senderID = getSenderID();
			data.reference = htonl(UID);
			data.value.iVal = 0;
			notifyMakeEntry(nType_bus, &data, sizeof(data));
		
			mmList = GetMetaData(UID, "MixMinusList", 0);
			if(!strlen(mmList))
				free(mmList);
			else{
				/* connect requested monitor mix-minus ports */
				port = instance->mm_jPorts;
				cmax = mixEngine->chanCount;
				for(c=0; c<cmax; c++){
					if(chanList = str_NthField(mmList, "&", c)){
						i = 0;
						while(portName = str_NthField(chanList, "+", i)){
							if(strlen(portName))
								jack_connect(mixEngine->client, jack_port_name(*port), portName);
							free(portName);
							i++;
						}
						free(chanList);
					}
					port++;
				}
			}	
		}else
			instance->status = status_empty;

		free(type);
	}
	
bail:
	if(p > -1){
		lastp = p;
	}
	
    return result;
}

unsigned char plTaskRunner(uint32_t UID)
{
	taskRecord *task;
	
	// check to make sure it isn't already running
	pthread_rwlock_rdlock(&taskLock);
	task = (taskRecord *)&taskList;
	while(task = task->next){
		if(task->UID == UID){
			pthread_rwlock_unlock(&taskLock);
			return 0;
		}
	}
	pthread_rwlock_unlock(&taskLock);
	
	createTaskItem("Open Play List", (void (*)(void *))PLOpen, NULL, UID, -1, 300L, 1); // time out in 5 minutes
	return 1;
}

void plOpenCleanUp(void *pass){
	struct locals *rec = (struct locals *)pass;
	if(rec->localUID)
		releaseMetaRecord(rec->localUID);
	if(rec->plMeta) 
		releaseMetaRecord(rec->plMeta);
	if(rec->fp) 
		fclose(rec->fp);
}

void dbPLOpen(uint32_t UID){
	struct locals pass;
	uint32_t newUID;
	uint32_t ID, index;
	char *tmp;
	char *itemURL;
	float ldur, idur;
	unsigned char first;
	time_t targetTime;

	targetTime = 0;
	first = 1;
	if(UID == 0)
		return;
	ID = GetMetaInt(UID, "ID", NULL);
	if(ID == 0)
		return;
	pass.plMeta = 0;
	pass.fp = NULL;
	pass.localUID = createMetaRecord("", NULL);

	pthread_cleanup_push((void (*)(void *))plOpenCleanUp, (void *)&pass);

	index = 0;

	while(!dbPLGetNextMeta(index, ID, pass.localUID)){
		pthread_testcancel();
		// loop through reading playlist items, resolve and add
		itemURL = FindFromMeta(pass.localUID);
		if(!strlen(itemURL)){
			// failed to resolve
			tmp = GetMetaData(pass.localUID, "Name", 0);
			str_insertstr(&tmp, "[media] dbPLOpen-", 0);
			str_appendstr(&tmp, ": couldn't resolve item ");
			serverLogMakeEntry(tmp);
			free(tmp);
		}else{
			// found... add to playlist in parent item's position
			if(newUID = SplitItem(UID, itemURL, 0)){
				// first resolved item inherits target time of the parent playlist, if set
				if(first){
					first = 0;
					targetTime = atoll(tmp = GetMetaData(UID, "TargetTime", 0));
					free(tmp);
				}
					
				if(targetTime){
					// if a target time was set, set for this child item too
					tmp = istr(targetTime);
					SetMetaData(newUID, "TargetTime", tmp);
					free(tmp);
					// and adjust for next and parent item based on this item's durtion;
					idur = GetMetaFloat(newUID, "Duration", NULL);
					targetTime = targetTime + (long)roundf(idur);
					tmp = istr(targetTime);
					SetMetaData(UID, "TargetTime", tmp);
					free(tmp);
				}
				// Note: SplitItem() already sent change notifications
			}
		}
		releaseMetaRecord(pass.localUID);
		pass.localUID = 0;
		// set up for the next time through
		pthread_testcancel();
		pass.localUID = createMetaRecord("", NULL);
		index++;
	}
	pthread_cleanup_pop(1);
}

void fplFilePLOpen(struct locals *locBlock, uint32_t plUID){
	uint32_t pos, newUID;
	char *itemURL, *tmp, *fp;
	unsigned char idOK;
	float ldur, idur;
	unsigned char first;
	time_t targetTime;
	
	targetTime = 0;
	first = 1;
	
	itemURL = GetMetaData(plUID, "URL", 0);
	if(fplPLGetNextMeta(locBlock->fp, locBlock->plMeta)){
		// error... 		
		str_insertstr(&itemURL, "[media] fplFilePLOpen-", 0);
		str_appendstr(&itemURL, ": corrupt file header for fpl");
		serverLogMakeEntry(itemURL);
		free(itemURL);
		return;
	}
	
	retainMetaRecord(plUID);
	locBlock->plMeta = plUID;
	fp = dbGetInfo("Fingerprint");
	tmp = GetMetaData(plUID, "Fingerprint", 0);
	if(!strcmp(fp, tmp)){
		idOK = 1;
	}else{
		idOK = 0;
		str_insertstr(&itemURL, "[media] fplFilePLOpen-", 0);
		str_appendstr(&itemURL, ": different current/saved databases; item searches disabled for fpl ");
		serverLogMakeEntry(itemURL);
	}
	free(fp);
	free(tmp);
	free(itemURL);

	locBlock->localUID = createMetaRecord("", NULL);

	while(!fplPLGetNextMeta(locBlock->fp, locBlock->localUID)){
		pthread_testcancel();
		SetMetaData(locBlock->localUID, "FPL", (tmp = GetMetaData(plUID, "URL", 0)));
		free(tmp);
		tmp = GetMetaData(locBlock->localUID, "URL", 0);
		if(!idOK){
			// playlist used a different data base... invalidate ID and item:/// URL's
			SetMetaData(locBlock->localUID, "ID", "0");
			// clear/invalidate database items... since we are using a different database
			if(strstr(tmp, "item://") == tmp)
				SetMetaData(locBlock->localUID, "URL", "");	
		}
		// loop through reading playlist items, resolve and add
		itemURL = FindFromMeta(locBlock->localUID);
		if(!strlen(itemURL)){
			// failed to resolve
			str_insertstr(&tmp, "[media] fplFilePLOpen-", 0);
			str_appendstr(&tmp, ": couldn't resolve item");
			serverLogMakeEntry(tmp);
			free(tmp);
		}else{
			free(tmp);
			// found... add to playlist in parent item's position
			if(newUID = SplitItem(plUID, itemURL, 0)){
				// first resolved item inherits target time of the parent playlist, if set
				if(first){
					first = 0;
					targetTime = atoll(tmp = GetMetaData(plUID, "TargetTime", 0));
					free(tmp);
				}
				
				if(targetTime){
					// if a target time was set, set for this child item too
					tmp = istr(targetTime);
					SetMetaData(newUID, "TargetTime", tmp);
					free(tmp);
					// and adjust for next and parent item based on this item's durtion;
					idur = GetMetaFloat(newUID, "Duration", NULL);
					targetTime = targetTime + (long)roundf(idur);
					tmp = istr(targetTime);
					SetMetaData(plUID, "TargetTime", tmp);
					free(tmp);
				}
				// Note: SplitItem() already sent change notifications
			}
		}
		releaseMetaRecord(locBlock->localUID);
		locBlock->localUID = 0;
		// set up for the next time through
		pthread_testcancel();
		locBlock->localUID = createMetaRecord("", NULL);
	}
}

void plsFilePLOpen(struct locals *locBlock, uint32_t plUID, char *filePath){
	uint32_t newUID;
	char *itemURL, *tmp;
	float ldur, idur;
	unsigned char first;
	time_t targetTime;
	
	targetTime = 0;
	first = 1;

	// get playlist metadata
	locBlock->plMeta = plUID;
	retainMetaRecord(plUID);
	
	plsPLGetProperties(locBlock->fp, locBlock->plMeta);
	rewind(locBlock->fp);

	// read past header
	if(plsPLGetNextMeta(locBlock->fp, 0, filePath)){
		// error... 
		tmp = strdup(filePath);
		str_insertstr(&tmp, "[media] fplFilePLOpen-", 0);
		str_appendstr(&tmp, ": corrupt file header for pls ");
		serverLogMakeEntry(tmp);
		free(tmp);
		return;
	}
	locBlock->localUID = createMetaRecord("", NULL);
	while(!plsPLGetNextMeta(locBlock->fp, locBlock->localUID, filePath)){
		pthread_testcancel();
		tmp = GetMetaData(plUID, "URL", 0);
		SetMetaData(locBlock->localUID, "FPL", tmp);
		free(tmp);
		tmp = GetMetaData(locBlock->localUID, "URL", 0);
		// loop through reading playlist items, resolve and add
		itemURL = FindFromMeta(locBlock->localUID);
		if(!strlen(itemURL)){
			// failed to resolve
			str_insertstr(&tmp, "[media] fplFilePLOpen-", 0);
			str_appendstr(&tmp, ": couldn't resolve item ");
			serverLogMakeEntry(tmp);
			free(tmp);
		}else{
			free(tmp);
			// found... add to playlist in parent item's position
			if(newUID = SplitItem(plUID, itemURL, 0)){
				// first resolved item inherits target time of the parent playlist, if set
				if(first){
					first = 0;
					targetTime = atoll(tmp = GetMetaData(plUID, "TargetTime", 0));
					free(tmp);
				}
				
				if(targetTime){
					// if a target time was set, set for this child item too
					tmp = istr(targetTime);
					SetMetaData(newUID, "TargetTime", tmp);
					free(tmp);
					// and adjust for next and parent item based on this item's durtion;
					idur = GetMetaFloat(newUID, "Duration", NULL);
					free(tmp);
					targetTime = targetTime + (long)roundf(idur);
					tmp = istr(targetTime);
					SetMetaData(plUID, "TargetTime", tmp);
					free(tmp);
				}
				// Note: SplitItem() already sent change notifications
			}			
		}
		releaseMetaRecord(locBlock->localUID);
		locBlock->localUID = 0;
		// set up for the next time through
		pthread_testcancel();
		locBlock->localUID = createMetaRecord("", NULL);
	}
}

void m3uFilePLOpen(struct locals *locBlock, uint32_t plUID, char *filePath){
	uint32_t pos, newUID;
	char *itemURL, *tmp;
	float ldur, idur;
	unsigned char first;
	time_t targetTime;

	targetTime = 0;
	first = 1;

	// get playlist metadata
	locBlock->plMeta = plUID;
	retainMetaRecord(plUID);
	
	m3uPLGetProperties(locBlock->fp, locBlock->plMeta);
	rewind(locBlock->fp);

	// read past header
	if(m3uPLGetNextMeta(locBlock->fp, 0, filePath)){
		tmp = strdup(filePath);
		str_insertstr(&tmp, "[media] m3uFilePLOpen-", 0);
		str_appendstr(&tmp, ": corrupt file header for pls");
		serverLogMakeEntry(tmp);
		free(tmp);
		return;
	}
	locBlock->localUID = createMetaRecord("", NULL);
	while(!m3uPLGetNextMeta(locBlock->fp, locBlock->localUID, filePath)){
		pthread_testcancel();
		tmp = GetMetaData(plUID, "URL", 0);
		SetMetaData(locBlock->localUID, "FPL", tmp);
		free(tmp);
		tmp = GetMetaData(locBlock->localUID, "URL", 0);
		// loop through reading playlist items, resolve and add
		itemURL = FindFromMeta(locBlock->localUID);
		if(!strlen(itemURL)){
			// failed to resolve
			str_insertstr(&tmp, "[media] m3uFilePLOpen-", 0);
			str_appendstr(&tmp, ": couldn't resolve item");
			serverLogMakeEntry(tmp);
			free(tmp);
		}else{
			free(tmp);
			// found... add to playlist in parent item's position
			if(newUID = SplitItem(plUID, itemURL, 0)){
				// first resolved item inherits target time of the parent playlist, if set
				if(first){
					first = 0;
					targetTime = atoll(tmp = GetMetaData(plUID, "TargetTime", 0));
					free(tmp);
				}
				if(targetTime){
					// if a target time was set, set for this child item too
					tmp = istr(targetTime);
					SetMetaData(newUID, "TargetTime", tmp);
					free(tmp);
					// and adjust for next and parent item based on this item's durtion;
					idur = GetMetaFloat(newUID, "Duration", NULL);
					targetTime = targetTime + (long)roundf(idur);
					tmp = istr(targetTime);
					SetMetaData(plUID, "TargetTime", tmp);
					free(tmp);
				}
				// Note: SplitItem() already sent change notifications
			}
		}
		releaseMetaRecord(locBlock->localUID);
		locBlock->localUID = 0;
		// set up for the next time through
		pthread_testcancel();
		locBlock->localUID = createMetaRecord("", NULL);
	}
}

void filePLOpen(uint32_t UID){
	struct locals locBlock;
	char *url, *path, *tmp;

	if(UID == 0)
		return;
		
	locBlock.plMeta = 0;
	locBlock.localUID = 0;
	locBlock.fp = NULL;
	pthread_cleanup_push((void (*)(void *))plOpenCleanUp, (void *)&locBlock);

	//get path from file url
	url = GetMetaData(UID, "URL", 0);
	tmp = str_NthField(url, "://", 1);
	if(!tmp){
		str_insertstr(&url, "[media] filePLOpen-", 0);
		str_appendstr(&url, ": bad URL");
		serverLogMakeEntry(url);
		free(url);
		goto cleanup;   
	}
	free(url);
	// ignore host, if any
	if(path = strchr(tmp, '/')){
		path = uriDecode(path);
	}else{
		path = uriDecode(tmp);			
	}
	free(tmp);

	if((locBlock.fp = fopen(path, "r")) == NULL){
		str_insertstr(&path, "[media] filePLOpen-", 0);
		str_appendstr(&path, ": couldn't open file for reading");
		serverLogMakeEntry(path);
		free(path);
		goto cleanup;
	}
	
	switch(filePLDetermineType(locBlock.fp)){
		case 1:		// .fpl file (AudioRack file playlist)
			fplFilePLOpen(&locBlock, UID);
			break;
		case 2:		// .pls file
			plsFilePLOpen(&locBlock, UID, path);
			break;
		case 3:		// .m3u file
			m3uFilePLOpen(&locBlock, UID, path);
			break;
		default:	// unknown file type
			str_insertstr(&url, "[media] filePLOpen-", 0);
			str_insertstr(&path, "filePLOpen: unknown header in file ", 0);
			serverLogMakeEntry(path);
	}
	free(path);
cleanup:
	pthread_cleanup_pop(1);
}

void PLOpen(taskRecord *parent){
	char *tmp;
	if(parent->UID){
		// play list items should not have a segout set...
		SetMetaData(parent->UID, "SegOut", "0.0");
		tmp = GetMetaData(parent->UID, "Type", 0);
		if(!strcmp(tmp, "playlist")){
			// it's a database playlist... handle accordingly
			dbPLOpen(parent->UID);
		}else if(!strcmp(tmp, "filepl")){
			// it's a file playlist... handle accordingly
			filePLOpen(parent->UID);
		}
		free(tmp);
	}
}

int CreateFPLHeader(FILE *fpl, char *Name){
	char *tmp = NULL;
	char *fp;
	int durFilePos;
	
	if(fpl == NULL)
		return -1;
	fputs("Type\tfilepl\n", fpl);
	fputs("Revision\t1.0\n", fpl);
	durFilePos = ftell(fpl);
	fputs("Duration\t0\n-------------\n", fpl);
	str_setstr(&tmp, "Name\t");
	str_appendstr(&tmp, Name);
	str_appendstr(&tmp,"\n");
	fputs(tmp, fpl);	
	str_setstr(&tmp, "Fingerprint\t");
	str_appendstr(&tmp, (fp = dbGetInfo("Fingerprint")));
	free(fp);
	str_appendstr(&tmp, "\n");
	fputs(tmp, fpl);
	fputs("\n", fpl);
	fflush(fpl);
	free(tmp);
	return durFilePos;
}

void CloseFPL(FILE *fpl, float duration, int durFilePos){	
	char *tmp = NULL;
	char *dur;
	
	if(fpl == NULL)
		return;
	if(durFilePos <= 0)
		return;
	if(fseek(fpl, durFilePos, SEEK_SET) == 0){
		str_setstr(&tmp, "Duration\t");
		str_appendstr(&tmp, (dur = fstr(duration, 1)));
		free(dur);
		str_appendstr(&tmp, "\n");
		fputs(tmp, fpl);	
	}
	free(tmp);
	fclose(fpl);
}

unsigned char AddFPLEntryFromUID(FILE *fpl, float Offset, uint32_t UID){	
	char *str = NULL;
	char *tmp;
	char **keys;
	char *key;
	char **values;
	unsigned int count, i;

	if(fpl == NULL)
		return 0;
	if(UID == 0)
		return 0;
		 
	if(count = GetMetaKeysAndValues(UID, &keys, &values)){
		for(i=0; i<count; i++){
			key = keys[i];
			if(!strcmp(key, "URL") || !strcmp(key, "Name") || !strcmp(key, "Artist") || 
					!strcmp(key, "Album") || !strcmp(key, "ArtistID") || !strcmp(key, "AlbumID") || 
					!strcmp(key, "ID") || !strcmp(key, "Type") ||  
					!strcmp(key, "Hash") || !strcmp(key, "Mount")){
				str_setstr(&str, key);
				str_appendstr(&str, "\t");
				str_appendstr(&str, values[i]);
				str_appendstr(&str, "\n");
				fputs(str, fpl);
			}	
			free(keys[i]);
			free(values[i]);
		}
		free(keys);
		free(values);
		
		str_setstr(&str, "Offset\t");
		str_appendstr(&str, (tmp = fstr(Offset, 1)));
		free(tmp);
		str_appendstr(&str, "\n\n");
		fputs(str, fpl);
		fflush(fpl);
		free(str);
		return 1;
	}		
	return 0;
}

void AddFPLEntryFromProgramLogStruct(FILE *fpl, float Offset, ProgramLogRecord *Rec){	
	char *tmp;
	char *line = NULL;

	if(fpl == NULL)
		return;
	if(Rec == NULL)
		return;
	
	// Use full UID meta data if available
	if(Rec->UID)
		if(AddFPLEntryFromUID(fpl, Offset, Rec->UID))
			return;
		
	// otherwise, use the data in the ProgramLogStruct
	// set up strings
	if(Rec->name && strlen(Rec->name)){
		str_setstr(&line, "Name\t");
		str_appendstr(&line, Rec->name);
		str_appendstr(&line, "\n");
		fputs(line, fpl);
	}
	if(Rec->artist && strlen(Rec->artist)){
		str_setstr(&line, "Artist\t");
		str_appendstr(&line, Rec->artist);
		str_appendstr(&line, "\n");
		fputs(line, fpl);
	}
	if(Rec->album && strlen(Rec->album)){
		str_setstr(&line, "Album\t");
		str_appendstr(&line, Rec->album);
		str_appendstr(&line, "\n");
		fputs(line, fpl);
	}
	str_setstr(&line, "URL\t");
	str_appendstr(&line, Rec->source);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	// set up Integers
	str_setstr(&line, "ID\t");
	str_appendstr(&line, (tmp = ustr(Rec->ID)));
	free(tmp);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	str_setstr(&line, "ArtistID\t");
	str_appendstr(&line, (tmp = ustr(Rec->artistID)));
	free(tmp);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	str_setstr(&line, "AlbumID\t");
	str_appendstr(&line, (tmp = ustr(Rec->albumID)));
	free(tmp);
	str_appendstr(&line, "\n");
	fputs(line, fpl);
	
	// set up offset
	str_setstr(&line, "Offset\t");
	str_appendstr(&line, (tmp = fstr(Offset, 1)));
	free(tmp);
	str_appendstr(&line, "\n\n");
	fputs(line, fpl);
	fflush(fpl);
	free(line);
}

float associatedPLNext(FILE *fp, float curPlayPos){
	// returns the offset time of the next item after curPlayPos,
	// with the file position set to the start of that item's data,
	// or 0.0 if there was an error, end-of-file, etc.
	
	uint32_t localUID;
	float nextTime;
	long curFilePos;
	char *tmp;
	
	nextTime = 0.0;
	do{
		curFilePos = ftell(fp);
		localUID = createMetaRecord("", NULL);
		if((fplPLGetNextMeta(fp, localUID)) == 0){
			// get offset time for next item
			nextTime = GetMetaFloat(localUID, "Offset", NULL);
			releaseMetaRecord(localUID);
		}else{
			releaseMetaRecord(localUID);
			break;
		}
	}while(nextTime < curPlayPos);
	fseek(fp, curFilePos, SEEK_SET);
	return nextTime;
}

unsigned char associatedPLLog(FILE *fp, uint32_t parent, unsigned int busses, unsigned char idOK){
	// returns with the file position set to the next item's data
	// return value is non-zero if entry was made (no error)
	uint32_t localUID;
	ProgramLogRecord *entryRec; 
	char *tmp;
	
	if(!(busses & 2L)){			
		// not in cue
		if(entryRec = calloc(1, sizeof(ProgramLogRecord))){
			localUID = createMetaRecord("", NULL);
			if(!fplPLGetNextMeta(fp, localUID)){
				// get offset time for next item
				if(!idOK){
					// playlist used a different database... invalidate ID and item:// URL's
					SetMetaData(localUID, "ID", "0");
					tmp = GetMetaData(localUID, "URL", 0);
					if(strstr(tmp, "item") == tmp)
						SetMetaData(localUID, "URL", "");	
					free(tmp);	
				}
				// log the UID meta data!
				entryRec->name = GetMetaData(localUID, "Name", 0);
				entryRec->artist = GetMetaData(localUID, "Artist", 0);
				entryRec->album = GetMetaData(localUID, "Album", 0);
				entryRec->ID = GetMetaInt(localUID, "ID", NULL);
				entryRec->location = GetMetaInt(0, "db_loc", NULL);
				entryRec->albumID = GetMetaInt(localUID, "AlbumID", NULL);
				entryRec->artistID = GetMetaInt(localUID, "ArtistID", NULL);
				entryRec->source = GetMetaData(localUID, "URL", 0);
				if(parent){
					entryRec->owner = GetMetaData(parent, "Name", 0);
					entryRec->webURL = GetMetaData(parent, "WebURL", 0);
				}
				
				if(GetMetaInt(localUID, "NoLog", NULL)){
					entryRec->added = 2;
					entryRec->post = 0;
				}else{
					entryRec->added = 0;
					if(GetMetaInt(localUID, "NoPost", NULL))
						entryRec->post = 0;
					else
						entryRec->post = 1;
				}
				entryRec->played = (busses & 0xFF);
				entryRec->UID = 0;
				
				programLogMakeEntry(entryRec);
				releaseMetaRecord(localUID);
				return 1;

			}
			releaseMetaRecord(localUID);
		}
	}
	return 0;
}

char associatedPLOpen(const char *url, FILE **fp){
	// returns -1 on failure, 0 for no DB fingerprint match, and 1 for match.
	char *tmp, *path, *dbfp;
	char tag[12];
	uint32_t plMeta;
	char result;

	if(*fp)
		fclose(*fp);
	*fp = NULL;
	plMeta = 0;
	result = -1;
	path = NULL;
	if(tmp = str_NthField(url, "://", 1)){
		// ignore host, if any
		if(path = strchr(tmp, '/')){
			path = uriDecode(path);
		}else{
			path = uriDecode(tmp);			
		}
		free(tmp);
		str_appendstr(&path, ".fpl");
		// check if it is a playlist file
		if((*fp = fopen(path, "r")) == NULL)
			goto cleanup;
		if(fread(tag, 11, 1, *fp) != 1)
			goto cleanup;
		rewind(*fp);
			
		tag[11] = 0; // null terminate
		if(strcmp(tag, "Type\tfilepl") != 0)
			goto cleanup;
		
		// get playlist metadata
		plMeta = createMetaRecord(url, NULL);
		if(fplPLGetNextMeta(*fp, plMeta))
			goto cleanup;

		tmp = GetMetaData(plMeta, "Fingerprint", 0);
		dbfp = dbGetInfo("Fingerprint");
		if(!strcmp(tmp, dbfp))
			result = 1;
		else
			result = 0;
		free(tmp);
		free(dbfp);
	}
	
cleanup:
	if(plMeta)
		releaseMetaRecord(plMeta);
	if(path)
		free(path);
	return result;
}
