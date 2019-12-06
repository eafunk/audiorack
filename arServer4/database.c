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

#include <sys/types.h> 
#include <sys/stat.h> 
#include <fts.h>
#include <sys/param.h>
#include <sys/mount.h>
#include <unistd.h>
#include <math.h>
#include <glob.h>
#include <ctype.h>
#include "database.h"
#include "data.h"
#include "media.h"
#include "dispatch.h"
#include "utilities.h"
#include "tasks.h"
#include "automate.h"

#define def_mount_list	"/private/var/automount/Network,/Network,/Volumes"

pthread_key_t gthread_dbi_inst = 0;
pthread_key_t gthread_dbi_conn = 0;
pthread_once_t gthread_dbi_key_once = PTHREAD_ONCE_INIT;
pthread_mutex_t dbiMutex = PTHREAD_MUTEX_INITIALIZER;

unsigned int dbFPcache = 0;

void dbi_instance_free(void *value){
	dbi_conn conn;
	
	if(conn = pthread_getspecific(gthread_dbi_conn)){
		dbi_conn_error_handler(conn, NULL, NULL);	
		dbi_conn_close(conn);
		pthread_setspecific(gthread_dbi_conn, NULL);
	}
	pthread_mutex_lock(&dbiMutex);
	dbi_shutdown_r((dbi_inst)value);
	pthread_mutex_unlock(&dbiMutex);
	pthread_setspecific(gthread_dbi_inst, NULL);
}

void make_dbi_keys(){	
	// only dbi_inst needs a distructor function set since both inst and conn are set up 
	// together, should be freed together to ensure the proper order for freeing: conn first then inst.
	(void) pthread_key_create(&gthread_dbi_inst, dbi_instance_free);
	(void) pthread_key_create(&gthread_dbi_conn, NULL);
	pthread_setspecific(gthread_dbi_inst, NULL);
	pthread_setspecific(gthread_dbi_conn, NULL);
}

dbi_inst get_thread_dbi(dbi_conn *conn){
	dbi_inst inst;
	
	*conn = NULL;

	// create thread keys for a per-thread dbi instance and connections if they don't yet exist
	pthread_once(&gthread_dbi_key_once, make_dbi_keys);
	
	// check to see if this thread has it's own dbi instance yet
	if((inst = pthread_getspecific(gthread_dbi_inst)) == NULL) {
		// create instance
		if(dbi_initialize_r(dbi_path, &inst) < 0){
			inst = NULL;
		}
		pthread_setspecific(gthread_dbi_inst, inst);
	}
	if(inst){
		if(*conn = pthread_getspecific(gthread_dbi_conn)){
			// see if the connection is still active... but clear any error handler first,
			// so that a ping failure doesn't try to use a previous error handler from another stack frame.
			dbi_conn_error_handler(*conn, NULL, NULL);	
			if(!dbi_conn_ping(*conn)){
				// connection has closed... release the connection instance and set to null,
				// indicating that we need a new connection.
				dbi_conn_close(*conn);
				pthread_setspecific(gthread_dbi_conn, NULL);
				*conn = NULL;
			}
		}		
	}else
		serverLogMakeEntry("[database] dbi_initialize_r failed to create libdbi instance for current string!");
	return inst;
}

void HandleDBerror(dbi_conn Conn, void *user_argument){
	struct dbErr *userData = (struct dbErr*)user_argument;
	const char *msg;
	char *str = NULL;
	
	userData->flag = 1;
	dbi_conn_error(Conn, &msg);
	if(msg){
		str_setstr(&str, "[libdbi] ");
		str_appendstr(&str, userData->message);
		str_appendstr(&str, "-");
		str_appendstr(&str, msg);
		serverLogMakeEntry(str);
		free(str);
	}
}

void DumpDBDriverList(ctl_session *session, char *buf, size_t size){
	dbi_driver dvr;
	dbi_inst instance;
	const char *name, *version;
	int tx_length;

	dvr = NULL;
	instance = NULL;
    dbi_initialize_r(dbi_path, &instance);
	if(instance){
		while(dvr = dbi_driver_list_r(dvr, instance)){
			name = dbi_driver_get_name(dvr);
			version = dbi_driver_get_version(dvr);
			tx_length = snprintf(buf, size, "%s\t%s\n", name, version);
			my_send(session, buf, tx_length, 0);
		}
		dbi_shutdown_r(instance);
	}
}

dbi_conn dbSetupConnection(dbi_inst instance, char dbName){
	dbi_conn conn;
	struct dbErr errRec;
	char *tmp;
	short port;
	char isValid;

	if(instance == NULL)
		return NULL;
		
	tmp = GetMetaData(0, "db_type", 0);
	conn = dbi_conn_new_r(tmp, instance);
	free(tmp);
	if(conn == NULL){
		return NULL;
	}
	
	errRec.flag = 0;
	errRec.message = "SetupConnection ";
	dbi_conn_error_handler(conn, HandleDBerror, (void*)(&errRec));	

	// parameters for all database types
	if(dbName){
		tmp = GetMetaData(0, "db_name", 0);
		dbi_conn_set_option(conn, "dbname", tmp);
		free(tmp);
		tmp = GetMetaData(0, "db_timeout", 0);
		if(strlen(tmp))
			dbi_conn_set_option(conn, "timeout", tmp);
		free(tmp);
	}
	
	tmp = GetMetaData(0, "db_type", 0);
	if(!strcmp(tmp, "sqlite3")){
		free(tmp);
		// for sqlite3 only
		tmp = GetMetaData(0, "db_dir", 0);
		dbi_conn_set_option(conn, "sqlite3_dbdir", tmp);
		free(tmp);
	}else{
		free(tmp);
		// all other database types
		tmp = GetMetaData(0, "db_server", 0);
		dbi_conn_set_option(conn, "host", tmp);
		free(tmp);
		port = GetMetaInt(0, "db_port", &isValid);
		if(isValid)
			dbi_conn_set_option_numeric(conn, "port", port);
		else
			dbi_conn_set_option_numeric(conn, "port", 0);
		tmp = GetMetaData(0, "db_user", 0);
		dbi_conn_set_option(conn, "username", tmp);
		free(tmp);
		tmp = GetMetaData(0, "db_pw", 0);
		dbi_conn_set_option(conn, "password", tmp);
		free(tmp);
	}
	pthread_mutex_lock(&dbiMutex);
	if(dbi_conn_connect(conn)){
		pthread_mutex_unlock(&dbiMutex);
		dbi_conn_error_handler(conn, NULL, NULL);	
		return NULL;
	}
	pthread_mutex_unlock(&dbiMutex);
	if(errRec.flag){
		dbi_conn_error_handler(conn, NULL, NULL);	
		dbi_conn_close(conn);
		return NULL;
	}
		
	if(dbName)
		// only set the thread connection if a database was selected,
		// otherwise, a independent connection was requested
		pthread_setspecific(gthread_dbi_conn, conn);
	dbi_conn_error_handler(conn, NULL, NULL);	
	return conn;
}

unsigned char MakeLogEntry(ProgramLogRecord *rec){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result = NULL;
	char *Name = NULL;
	char *Artist = NULL;
	char *Album = NULL;
	char *Comment = NULL;
	char *Source = NULL;
	char *Owner = NULL;
	char *prefix = NULL;
	char *tmp;
	uint32_t recID;
	struct dbErr errRec;
	unsigned char ret_val = 0;
	
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}

	prefix = GetMetaData(0, "db_prefix", 0);
	
	errRec.message = "MakeLogEntry";
	dbi_conn_error_handler(conn, HandleDBerror, (void*)(&errRec));	
	
	// make a new entry:
	// allocate, copy and encode the strings in the db's format
	Name = strdup(rec->name);
	dbi_conn_quote_string(conn, &Name);
	
	Artist = strdup(rec->artist);
	dbi_conn_quote_string(conn, &Artist);
	
	Album = strdup(rec->album);
	dbi_conn_quote_string(conn, &Album);
	
	Source = strdup(rec->source);
	dbi_conn_quote_string(conn, &Source);
	
	Comment = strdup(rec->comment);
	dbi_conn_quote_string(conn, &Comment);

	Owner = strdup(rec->owner);
	dbi_conn_quote_string(conn, &Owner);
			
	if(rec->logID && rec->played){
		result = dbi_conn_queryf(conn, "UPDATE %slogs SET Item = %lu, Time = %ld, Name = %s, Artist = %s, Album = %s, "
						"Added = %u, ArtistID = %lu, AlbumID = %lu, OwnerID = %lu, Comment = %s, Source = %s, Owner = %s WHERE ID = %lu", 
						prefix, (unsigned long)rec->ID, (long)rec->when, Name, Artist, Album, (unsigned int)rec->added, (unsigned long)rec->artistID, 
						(unsigned long)rec->albumID, (unsigned long)rec->ownerID, Comment, Source, Owner, (unsigned long)rec->logID);

		if(result){
			if(dbi_result_get_numrows_affected(result)){
				ret_val = 1;
				goto cleanup;
			}
			dbi_result_free(result);
		}
	} 

	// perform the sql insert function
	result = dbi_conn_queryf(conn, "INSERT INTO %slogs (Item, Location, Time, Name, Artist, Album, Added, ArtistID, AlbumID, OwnerID, "
				"Comment, Source, Owner) VALUES (%lu, %lu, %ld, %s, %s, %s, %u, %lu, %lu, %lu, %s, %s, %s)", 
				prefix, (unsigned long)rec->ID, (unsigned long)rec->location, (long)rec->when, Name, Artist, Album, (unsigned int)rec->added, 
				(unsigned long)rec->artistID, (unsigned long)rec->albumID, (unsigned long)rec->ownerID, Comment, Source, Owner);
	ret_val = 1;
	// Get new log ID, if any
	if(result && rec->UID){
		dbi_result_free(result);
		result = dbi_conn_queryf(conn, "SELECT ID FROM %slogs WHERE Item = %lu AND Location = %lu AND Time = %ld AND Source = %s",
				prefix, (unsigned long)rec->ID, (unsigned long)rec->location, (long)rec->when, Source);
		if(result){
			if(dbi_result_has_next_row(result)){
				if(dbi_result_next_row(result)){ 
					recID = dbi_result_get_uint(result, "ID");
					tmp = ustr(recID);
					SetMetaData(rec->UID, "logID", tmp);
					free(tmp);
					rec->logID = recID;
				}
			}
		}
	}
	
cleanup:
	// clean up
	if(result)
		dbi_result_free(result);
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	// free char strings
	if(prefix)
		free(prefix);
	if(Name)
		free(Name);
	if(Artist)
		free(Artist);
	if(Album)
		free(Album);
	if(Comment)
		free(Comment);
	if(Source)
		free(Source);
	if(Owner)
		free(Owner);
	
	return ret_val;
}

unsigned char updateLogMeta(uint32_t uid){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result = NULL;
	unsigned char ret_val = 0;
	char *Name = NULL;
	char *Artist = NULL;
	char *Album = NULL;
	char *Comment = NULL;
	char *Source = NULL;
	char *Owner = NULL;
	char *prefix = NULL;
	char *tmp = NULL;
	uint32_t recID;
	struct dbErr errRec;
	
	if(recID = GetMetaInt(uid, "logID", NULL)){
		instance = get_thread_dbi(&conn);
		if(conn == NULL){
			// no connection yet for this thread... 
			if((conn = dbSetupConnection(instance, 1)) == NULL)	
				goto cleanup;
		}		
	
		prefix = GetMetaData(0, "db_prefix", 0);
		
		errRec.message = "updateLogMeta";
		dbi_conn_error_handler(conn, HandleDBerror, (void*)(&errRec));	
		
		// make a new entry:
		Name = GetMetaData(uid, "Name", 0);
		Artist = GetMetaData(uid, "Artist", 0);
		Album = GetMetaData(uid, "Album", 0);
		Source = GetMetaData(uid, "URL", 0);
		Comment = GetMetaData(uid, "Tag", 0);
		Owner = GetMetaData(uid, "Owner", 0);

		// encode the strings in the db's format

		dbi_conn_quote_string(conn, &Name);
		dbi_conn_quote_string(conn, &Artist);
		dbi_conn_quote_string(conn, &Album);
		dbi_conn_quote_string(conn, &Source);
		dbi_conn_quote_string(conn, &Comment);
		dbi_conn_quote_string(conn, &Owner);
		
		// modify  entry
		result = dbi_conn_queryf(conn, "UPDATE %slogs SET Name = %s, Artist = %s, "
					"Album = %s, Comment = %s, Source = %s, Owner = %s WHERE ID = %lu AND (Added & 1) = 1", 
					prefix, Name, Artist, Album, Comment, Source, Owner, (unsigned long)recID);
		ret_val = 1;
		dbi_conn_error_handler(conn, NULL, NULL);

	}

cleanup:
	if(result)
		dbi_result_free(result);
	// free char strings
	if(prefix)
		free(prefix);
	if(Name)
		free(Name);
	if(Artist)
		free(Artist);
	if(Album)
		free(Album);
	if(Comment)
		free(Comment);
	if(Source)
		free(Source);
	if(Owner)
		free(Owner);
	
	return ret_val;
}

void DeleteLogEntry(void *inRef){
	uint32_t *logID;
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result = NULL;
	char *prefix = NULL;
	struct dbErr errRec;
	taskRecord *parent = (taskRecord *)inRef;
	
	if((logID = (uint32_t *)(parent->userData)) == 0)
		return;
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);
	
	errRec.message = "DeleteLogEntry";
	dbi_conn_error_handler(conn, HandleDBerror, (void*)(&errRec));	
	result = dbi_conn_queryf(conn, "DELETE FROM %slogs WHERE ID = %lu AND (Added & 1) = 1", prefix, *logID);

cleanup:
	if(prefix)
		free(prefix);
	if(result)
		dbi_result_free(result);
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
}

short dbPLGetNextMeta(uint32_t index, uint32_t ID, uint32_t UID){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result = NULL;
	short result;
	const char *valStr;
	const char *propStr;
	char *prefix = NULL;
	struct dbErr errRec;
	
	result = -1;
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);
	
	errRec.message = "dbPLGetNextMeta";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function
    db_result = dbi_conn_queryf(conn, "SELECT * FROM %splaylist WHERE ID = %lu AND Position = %lu", prefix, ID, index);
	if(db_result == NULL){
		dbi_result_free(db_result);
		goto cleanup;
	}
	while(dbi_result_has_next_row(db_result)){
		if(dbi_result_next_row(db_result)){
			if(valStr = (const char*)dbi_result_get_string(db_result, "Value")){
				if(propStr = (const char*)dbi_result_get_string(db_result, "Property")){
					SetMetaData(UID, propStr, valStr);
					result = 0;
				}
			}
		}
	}
	dbi_result_free(db_result);
	
cleanup:
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	if(prefix)
		free(prefix);
	return result;
}

unsigned char dbTaskRunner(uint32_t UID, unsigned char load){
	taskRecord *task;
	char *subType = NULL;
	char *value = NULL;
	char *supress = NULL;
	char *prefix = NULL;
	char *qStr = NULL;
	char *theCommand = NULL; 
	char *include;
	char *loc;
	char *encString;
	unsigned char suc = 0;
		
	// check to make sure this isn't already running
	pthread_rwlock_rdlock(&taskLock);
	task = (taskRecord *)&taskList;
	while(task = task->next){
		if(task->UID == UID){
			pthread_rwlock_unlock(&taskLock);
			goto cleanup;
		}
	}
	pthread_rwlock_unlock(&taskLock);

	// if we made it here, then it's not running
	prefix = GetMetaData(0, "db_prefix", 0);
	subType = GetMetaData(UID, "Subtype", 0);
	if(!strlen(subType))
		goto cleanup;
	if(!load){
		// playlist add 
		if(!strcmp(subType,"Pick")){
			// pick subtypes are run at the time they are added to the playlist
			value = GetMetaData(UID, "Query", 0);
			if(strlen(value)){
				// database custom query pick
				createTaskItem("DB Query Pick", (void (*)(void *))dbPick, NULL, UID, -1, 300L, 1); // time out in 5 minutes
				suc = 1;
				goto cleanup;
			}
			free(value);
			value = GetMetaData(UID, "Category", 0);
			if(strlen(value)){
				// category pick
				supress = GetMetaData(UID, "Supress", 0);
				include = GetMetaData(0, "db_include_loc", 0);
				loc = GetMetaData(0, "db_loc", 0);
				if(strlen(include)){
					str_insertstr(&include, ",", 0);
					str_insertstr(&include, loc, 0);
				}else
					str_setstr(&include, loc);
				SetMetaData(UID, "Mode", "weighted");
				if(!strcmp(supress,"Artist")){
					str_setstr(&qStr,
						"SELECT [PFX]file.ID as ID, MIN((UNIX_TIMESTAMP() - [PFX]logs.Time) * cnt.num) AS weighted "
						"FROM ([PFX]category_item, [PFX]file) "
						"LEFT JOIN ("
							"SELECT [PFX]file.Artist AS artID, COUNT([PFX]file.ID) AS num "
							"FROM [PFX]file "
							"WHERE NOT ([PFX]file.Missing <=> 1) "
							"GROUP BY [PFX]file.Artist"
						") AS cnt ON (cnt.artID = [PFX]file.Artist) "
						"LEFT JOIN [PFX]logs ON ("
							"[PFX]file.Artist = [PFX]logs.ArtistID AND [PFX]logs.Time > (UNIX_TIMESTAMP() - 604800) "
							"AND [PFX]logs.Location IN (");
					str_appendstr(&qStr,include);
					str_appendstr(&qStr,") ) "
						"LEFT JOIN [PFX]rest ON ("
							"[PFX]file.ID = [PFX]rest.Item AND [PFX]rest.Location = ");
					str_appendstr(&qStr,loc);
					str_appendstr(&qStr,") "
						"WHERE NOT ([PFX]file.Missing <=> 1) AND [PFX]file.ID = [PFX]category_item.Item "
							"AND [PFX]category_item.Category = ");
					str_appendstr(&qStr,value);
					str_appendstr(&qStr,
							" AND [PFX]rest.Added IS NULL "
						"GROUP BY [PFX]file.ID "
						"ORDER BY ISNULL([PFX]logs.Time) DESC, weighted DESC, RAND();");

				}else if(!strcmp(supress,"Album")){
					str_setstr(&qStr,
						"SELECT [PFX]file.ID as ID, MIN((UNIX_TIMESTAMP() - [PFX]logs.Time) * cnt.num) AS weighted "
						"FROM ([PFX]category_item, [PFX]file) "
						"LEFT JOIN ("
							"SELECT [PFX]file.Album AS albID, COUNT([PFX]file.ID) AS num "
							"FROM [PFX]file "
							"WHERE NOT ([PFX]file.Missing <=> 1) "
							"GROUP BY [PFX]file.Album"
						") AS cnt ON (cnt.albID = [PFX]file.Album) "
						"LEFT JOIN [PFX]logs ON ("
							"[PFX]file.Album = [PFX]logs.AlbumID AND [PFX]logs.Time > (UNIX_TIMESTAMP() - 604800) AND "
							"[PFX]logs.Location IN (");
					str_appendstr(&qStr,include);
					str_appendstr(&qStr,")) "
						"LEFT JOIN [PFX]rest ON ([PFX]file.ID = [PFX]rest.Item AND "
							"[PFX]rest.Location = ");
					str_appendstr(&qStr,loc);
					str_appendstr(&qStr,") "
						"WHERE NOT ([PFX]file.Missing <=> 1) AND [PFX]file.ID = [PFX]category_item.Item AND "
							"[PFX]category_item.Category = ");
					str_appendstr(&qStr,value);
					str_appendstr(&qStr,
							" AND [PFX]rest.Added IS NULL "
						"GROUP BY [PFX]file.ID "
						"ORDER BY ISNULL([PFX]logs.Time) DESC, weighted DESC, RAND();");

				}else if(!strcmp(supress,"Name")){
					str_setstr(&qStr,
						"SELECT [PFX]toc.ID as ID, MAX([PFX]logs.Time) AS Time "
						"FROM ([PFX]category_item, [PFX]toc) "
						"LEFT JOIN [PFX]logs ON ("
							"[PFX]toc.Name = [PFX]logs.Name AND [PFX]logs.Time > (UNIX_TIMESTAMP() - 604800) "
							"AND [PFX]logs.Location IN (");
					str_appendstr(&qStr,include);
					str_appendstr(&qStr,")) "
						"LEFT JOIN [PFX]file ON ([PFX]toc.ID = [PFX]file.ID) "
						"LEFT JOIN [PFX]rest ON ([PFX]toc.ID = [PFX]rest.Item AND "
							"[PFX]rest.Location = ");
					str_appendstr(&qStr,loc);
					str_appendstr(&qStr,") "
						"WHERE NOT ([PFX]file.Missing <=> 1) AND [PFX]toc.ID = [PFX]category_item.Item "
							"AND [PFX]category_item.Category = ");
					str_appendstr(&qStr,value);
					str_appendstr(&qStr," AND [PFX]rest.Added IS NULL "
						"GROUP BY [PFX]toc.ID "
						"ORDER BY Time, RAND();");
				}else{ // order by item ID last played
					str_setstr(&qStr,
						"SELECT [PFX]toc.ID as ID, MAX([PFX]logs.Time) AS Time "
						"FROM ([PFX]category_item, [PFX]toc) "
						"LEFT JOIN [PFX]logs ON ([PFX]toc.ID = [PFX]logs.Item AND "
							"[PFX]logs.Time > (UNIX_TIMESTAMP() - 604800) AND "
							"[PFX]logs.Location IN (");
					str_appendstr(&qStr,include);
					str_appendstr(&qStr,")) "
						"LEFT JOIN [PFX]file ON ([PFX]toc.ID = [PFX]file.ID) "
						"LEFT JOIN [PFX]rest ON ([PFX]toc.ID = [PFX]rest.Item AND "
							"[PFX]rest.Location = ");
					str_appendstr(&qStr,loc);
					str_appendstr(&qStr,") "
						"WHERE NOT ([PFX]file.Missing <=> 1) AND [PFX]toc.ID = [PFX]category_item.Item "
							"AND [PFX]category_item.Category = ");
					str_appendstr(&qStr,value);
					str_appendstr(&qStr,
							" AND [PFX]rest.Added IS NULL "
						"GROUP BY [PFX]toc.ID "
						"ORDER BY Time, RAND();");
				}
/*				if(supress == "Artist")
					qStr = "SELECT "+prefix+"file.ID as ID, MAX("+prefix+"logs.Time) AS Time FROM ("+prefix+"category_item, "
					+prefix+"file) LEFT JOIN "+prefix+"logs ON ("+prefix+"file.Artist = "
					+prefix+"logs.ArtistID AND "+prefix+"logs.Time > (UNIX_TIMESTAMP() - 604800) AND "
					+prefix+"logs.Location IN ("+include+")) LEFT JOIN "+prefix+"rest ON ("+prefix+"file.ID = "+prefix+"rest.Item AND "
					+prefix+"rest.Location = "+GetMetaData(0, "db_loc")+") WHERE NOT ("
					+prefix+"file.Missing <=> 1) AND "+prefix+"file.ID = "+prefix+"category_item.Item AND "
					+prefix+"category_item.Category = "+value+" AND "+prefix+"rest.Added IS NULL GROUP BY "+prefix+"file.ID ORDER BY Time, RAND()";
				else if(supress == "Album")
					qStr = "SELECT "+prefix+"file.ID as ID, MAX("+prefix+"logs.Time) AS Time FROM ("+prefix+"category_item, "
					+prefix+"file) LEFT JOIN "+prefix+"logs ON ("+prefix+"file.Album = "
					+prefix+"logs.AlbumID AND "+prefix+"logs.Time > (UNIX_TIMESTAMP() - 604800) AND "
					+prefix+"logs.Location IN ("+include+")) LEFT JOIN "+prefix+"rest ON ("+prefix+"file.ID = "+prefix+"rest.Item AND "
					+prefix+"rest.Location = "+GetMetaData(0, "db_loc")+") WHERE NOT ("
					+prefix+"file.Missing <=> 1) AND "+prefix+"file.ID = "+prefix+"category_item.Item AND "
					+prefix+"category_item.Category = "+value+" AND "+prefix+"rest.Added IS NULL GROUP BY "+prefix+"file.ID ORDER BY Time, RAND()";
				else if(supress == "Name")
					qStr = "SELECT "+prefix+"toc.ID as ID, MAX("+prefix+"logs.Time) AS Time FROM ("+prefix+"category_item, "
					+prefix+"toc) LEFT JOIN "+prefix+"logs ON ("+prefix+"toc.Name = "+prefix+"logs.Name AND "
					+prefix+"logs.Time > (UNIX_TIMESTAMP() - 604800) AND "
					+prefix+"logs.Location IN ("+include+")) LEFT JOIN "+prefix+"file ON ("
					+prefix+"toc.ID = "+prefix+"file.ID) LEFT JOIN "+prefix+"rest ON ("+prefix+"toc.ID = "+prefix+"rest.Item AND "
					+prefix+"rest.Location = "+GetMetaData(0, "db_loc")+") WHERE NOT ("+prefix+"file.Missing <=> 1) AND "
					+prefix+"toc.ID = "+prefix+"category_item.Item AND "+prefix+"category_item.Category = "
					+value+" AND "+prefix+"rest.Added IS NULL GROUP BY "+prefix+"toc.ID ORDER BY Time, RAND()";
				else // order by item ID last played
					qStr = "SELECT "+prefix+"toc.ID as ID, MAX("+prefix+"logs.Time) AS Time FROM ("+prefix+"category_item, "
					+prefix+"toc) LEFT JOIN "+prefix+"logs ON ("+prefix+"toc.ID = "+prefix+"logs.Item AND "
					+prefix+"logs.Time > (UNIX_TIMESTAMP() - 604800) AND "
					+prefix+"logs.Location IN ("+include+")) LEFT JOIN "+prefix+"file ON ("
					+prefix+"toc.ID = "+prefix+"file.ID) LEFT JOIN "+prefix+"rest ON ("+prefix+"toc.ID = "+prefix+"rest.Item AND "
					+prefix+"rest.Location = "+GetMetaData(0, "db_loc")+") WHERE NOT ("+prefix+"file.Missing <=> 1) AND "
					+prefix+"toc.ID = "+prefix+"category_item.Item AND "+prefix+"category_item.Category = "
					+value+" AND "+prefix+"rest.Added IS NULL GROUP BY "+prefix+"toc.ID ORDER BY Time, RAND()";
*/				
				// note: "logs.Time > (UNIX_TIMESTAMP() - 604800)" limits search through logs back 1 week
				// RAND() sorts all items with the same times (i.e. NULL) randomy 
				
				str_ReplaceAll(&qStr, "[PFX]", prefix);

				free(loc);
				free(include);

				// URL type escape (%nn) the query string, except for " " chars.
				encString = uriEncodeKeepSpace(qStr);
				SetMetaData(UID, "Query", encString);
				createTaskItem("Category Pick", (void (*)(void *))dbPick, NULL, UID, -1, 300L, 1); // time out in 5 minutes
				free(qStr);
				free(encString);
				suc = 1;
				goto cleanup;

			}
			free(value);

			value = GetMetaData(UID, "Folder", 0);
			if(strlen(value)){
				// folder pick
				createTaskItem("Folder Pick", (void (*)(void *))folderPick, NULL, UID, -1, 300L, 1);
				
				suc = 1;
				goto cleanup;
			}
		}else{
			suc = 1;
			goto cleanup; // not running, but don't detete... will run later on player load
		}
	}else{		
		// player load attempt
		// all of these are run only when a player load is attempted
		// media.c:LoadItemPlayer
		if(!strcmp(subType,"Command")){
			value = GetMetaData(UID, "Command", 0);
			if(strlen(value)){
				str_setstr(&theCommand, value);
				str_appendchr(&theCommand,'\n');  // add LF to end of command: an extra one will not hurt
				createTaskItem(theCommand, ExecuteCommand, (void *)theCommand, UID, -1, 0L, 1); // no time out (not cancelable)
				suc = 1;
				goto cleanup;
			}
		}
		if(!strcmp(subType,"Open")){
			// open (on a new virtual terminal) an application
			value = GetMetaData(UID, "Path", 0);
			str_setstr(&theCommand, "openvt ");
			str_appendstr(&theCommand, value);
			ExecuteProcess(theCommand, UID, 180);
			suc = 1;
			goto cleanup;
		}
		if(!strcmp(subType,"Execute")){
			// execute a unix shell script, command, program, etc
			value = GetMetaData(UID, "Command", 0);
			ExecuteProcess(value, UID, 180);
			suc = 1;
			goto cleanup;
		}
	}
cleanup:
	if(supress)
		free(supress);
	if(subType)
		free(subType);
	if(prefix)
		free(prefix);
	if(value)
		free(value);
	return suc;
}

char *dbGetInfo(const char *property){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result = NULL;
	const char *Str;
	char *prefix = NULL;
	char *result;
	char *propCpy = NULL;
	struct dbErr errRec;
	
	result = strdup("");
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	propCpy = strdup(property);
	prefix = GetMetaData(0, "db_prefix", 0);
	dbi_conn_quote_string(conn, &propCpy);

	errRec.message = "dbGetInfo";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function
    db_result = dbi_conn_queryf(conn, "SELECT Value FROM %sinfo WHERE Property = %s", prefix, propCpy);
	if(db_result == NULL)
		goto cleanup;
	// get first record (should be the only record)
	if(!dbi_result_has_next_row(db_result)){
		dbi_result_free(db_result);
		goto cleanup;
	}
	if(dbi_result_next_row(db_result)){ 
		Str = (const char*)dbi_result_get_string(db_result, "Value");
		if(Str)
			str_setstr(&result, Str);
	}else{
		dbi_result_free(db_result);
		goto cleanup;
	}
	dbi_result_free(db_result);

cleanup:
	if(prefix)
		free(prefix);
	if(propCpy)
		free(propCpy);
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	return result;
}

inline void clearCachedFingerprint(void){
	dbFPcache = 0;
}

unsigned int getFingerprint(void){
	char *tmp;
	
	if(!dbFPcache){
		tmp = dbGetInfo("Fingerprint");
		dbFPcache = atoi(tmp);
		free(tmp);
	}
	return dbFPcache;
}

unsigned char db_initialize(struct dbErr *inRec){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result = NULL;
	char *dbName;
	char *sqlstr = NULL;
	char *typeStr = NULL;
	char *versionStr = NULL;
	char *ini_file_path = NULL;
	char *tmp = NULL;
	FILE *fp;
	char line[4096];
	struct dbErr errRec;
	
	errRec.message = "dbInitialize";
	errRec.flag = 1;
	fp = NULL;
	
	typeStr = GetMetaData(0, "db_type", 0);
	dbName = GetMetaData(0, "db_name", 0);

	instance = get_thread_dbi(&conn);
	// ignore conn... we want to create our own connection just for this
	// with out a database selected
	if((conn = dbSetupConnection(instance, 0)) == NULL)	
		goto cleanup;
	
	dbi_conn_error_handler(conn, NULL, NULL);	
	
	if(!dbi_conn_select_db(conn, dbName))
		versionStr = dbGetInfo("Version");
	if(versionStr && strlen(versionStr)){
		// this is an upgrade to an existing database
		str_setstr(&ini_file_path, AppSupportDirectory);
		str_appendstr(&ini_file_path, typeStr);
		str_appendstr(&ini_file_path, versionStr);
		str_appendstr(&ini_file_path, ".dbi");
	}else{
		// create new database
		str_setstr(&ini_file_path, AppSupportDirectory);
		str_appendstr(&ini_file_path, typeStr);
		str_appendstr(&ini_file_path, ".dbi");
		str_setstr(&sqlstr, "CREATE DATABASE IF NOT EXISTS ");
		str_appendstr(&sqlstr, dbName);
		db_result = dbi_conn_query(conn, sqlstr);
		if(db_result)
			dbi_result_free(db_result);
		else
			goto cleanup;
		if(dbi_conn_select_db(conn, dbName))
			goto cleanup;
	}

	if(inRec == NULL)
		inRec = &errRec;
	
	if((fp = fopen(ini_file_path, "r")) == NULL){
		if(!versionStr || !strlen(versionStr)){
			str_setstr(&tmp, "[database] dbInitialize- template file '");
			str_appendstr(&tmp, ini_file_path);
			str_appendstr(&tmp, "': Could not open file for reading");
			serverLogMakeEntry(tmp);
			free(tmp); 
		}else
			errRec.flag = 0;
		goto cleanup;
	}	
	
	errRec.flag = 0;
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(inRec));	
	while(fgets(line, sizeof line, fp) != NULL){
		// each line in the .dbi file is an sql command to execute
		str_setstr(&sqlstr, line);
		dbMacroReplace(&sqlstr);
		db_result = dbi_conn_query(conn, sqlstr);
		if(db_result) 
			dbi_result_free(db_result);
	}
	if(versionStr && strlen(versionStr))
		// re-enter for another go-around so we upgrade
		// all they way to the latest version.
		db_initialize(inRec);
	
cleanup:
	if(sqlstr)
		free(sqlstr);
	if(typeStr)
		free(typeStr);
	if(ini_file_path)
		free(ini_file_path);
	if(fp)
		fclose(fp);
	if(conn){
		dbi_conn_error_handler(conn, NULL, NULL);	
		dbi_conn_close(conn);
	}
	if(!errRec.flag){
		versionStr = dbGetInfo("Version");
		str_setstr(&tmp, " [database] dbInitialize-");
		str_appendstr(&tmp, dbName);
		str_appendstr(&tmp, ": initialized/updated to version ");
		str_appendstr(&tmp, versionStr);
		serverLogMakeEntry(tmp);
		free(dbName);
		free(versionStr);
		return 1;
	}else{
		if(versionStr)
			free(versionStr);
		if(dbName)
			free(dbName);
	}
	return 0;
}

float GaussianNumber(void){
	float x, y, r;

	// normal (gaussian) distribution random pick
	do{
		x = drand48();
		//x = (rnd * 2.0) -1.0
		y = drand48();
		//  y = (rnd * 2.0) -1.0
		r = powf(x, 2) + powf(y, 2);
	}while((r > 1.0) || (r == 0.0));
	r = sqrtf(r);
	y = sqrtf(-2.0 * logf(r)/r);
	
	return fabsf(y * x);
}

static inline float RandomNumber(void){
	return drand48();
}

void dbPick(taskRecord *parent){
	dbi_result result;
	dbi_result lastResult;
	dbi_result prevResult;
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	unsigned int *id_array;
	unsigned int i;
	unsigned long field;
	unsigned long long Item;
	unsigned long count;
	unsigned long row;
	unsigned char last;
	uint32_t newUID;
	int size;
	char *mode = NULL;
	char *query;
	char *qStr;
	char *tmp;
	char *name;
	char *single;
	double targetTime;
	struct dbErr errRec; 

	tmp = GetMetaData(parent->UID, "Query", 0);
	if(strlen(tmp) < 8){
		free(tmp);
		return;
	}
	qStr = uriDecode(tmp);
	free(tmp);
	
	if(parent->UID){
		str_ReplaceAll(&qStr, "[thisID]", (tmp = GetMetaData(parent->UID, "ID", 0)));
		free(tmp);
	}		

	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL){
			free(qStr);
			return;
		}
	}
	field = 0;
	lastResult = NULL;

	errRec.message = "dbPick";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// parse query string for multiple queries, separated with ';' char
	single = str_NthField(qStr, ";", field);

	while(!parent->cancelThread && single && strlen(single)){
		// perform the sql query function
		// replace any special function macros
		dbMacroReplace(&single);
		result = dbi_conn_query(conn, single);
		if(result){
			count = dbi_result_get_numrows(result);
			if(count > 0){
				prevResult = lastResult;
				lastResult = result;
				if(prevResult)
					dbi_result_free(prevResult);
			}else{
				dbi_result_free(result);
			}
		}
		field++;
		free(single);
		single = str_NthField(qStr, ";", field);
	}
	free(qStr);
	if(single)
		free(single);
		
	if(!parent->cancelThread){
		if(lastResult){
			if(count = dbi_result_get_numrows(lastResult)){
				mode = GetMetaData(parent->UID, "Mode", 0);
				if(!strcmp(mode,"random")){
					row = (unsigned long)(count * RandomNumber());
					if(dbi_result_seek_row(lastResult, row+1)){ 
						Item = dbi_result_get_ulonglong(lastResult, "ID");
						dbi_result_free(lastResult);
						lastResult = NULL;
						if(Item){
							tmp = ustr(Item);
							str_insertstr(&tmp, "item:///", 0);
							newUID = SplitItem(parent->UID, tmp, 1);
							free(tmp);
							if(newUID == 0){
								tmp = ustr(Item);
								str_insertstr(&tmp, ": split failed on item:///", 0);
								str_insertstr(&tmp, (name = GetMetaData(parent->UID, "Name", 0)), 0);
								free(name);
								str_insertstr(&tmp, "[database] dbPick-", 0);
								serverLogMakeEntry(tmp);
								free(tmp);
							}
						}
					}
				}
				else if(!strcmp(mode,"weighted")){
					row = (unsigned long)(count * GaussianNumber());
					if(dbi_result_seek_row(lastResult, row+1)){ 
						Item = dbi_result_get_ulonglong(lastResult, "ID");	
						dbi_result_free(lastResult);
						lastResult = NULL;
						if(Item){
							tmp = ustr(Item);
							str_insertstr(&tmp, "item:///", 0);
							newUID = SplitItem(parent->UID, tmp, 1);
							free(tmp);
							if(newUID == 0){
								tmp = ustr(Item);
								str_insertstr(&tmp, ": split failed on item:///", 0);
								str_insertstr(&tmp, (name = GetMetaData(parent->UID, "Name", 0)), 0);
								free(name);
								str_insertstr(&tmp, "[database] dbPick-", 0);
								serverLogMakeEntry(tmp);
								free(tmp);
							}
						}
					}
				}
				else if(!strcmp(mode,"first")){
					if(dbi_result_has_next_row(lastResult)){
						if(dbi_result_next_row(lastResult)){ 
							Item = dbi_result_get_ulonglong(lastResult, "ID");
							dbi_result_free(lastResult);
							lastResult = NULL;
							if(Item){
								tmp = ustr(Item);
								str_insertstr(&tmp, "item:///", 0);
								newUID = SplitItem(parent->UID, tmp, 1);
								free(tmp);
								if(newUID == 0){
									tmp = ustr(Item);
									str_insertstr(&tmp, ": split failed on item:///", 0);
									str_insertstr(&tmp, (name = GetMetaData(parent->UID, "Name", 0)), 0);
									free(name);
									str_insertstr(&tmp, "[database] dbPick-", 0);
									serverLogMakeEntry(tmp);
									free(tmp);
								}
							}
						}
					}
				}
				else if(!strcmp(mode,"all")){
					// get target-time of parent for inheritance by items, and increment by each previous item's duration.
					count = dbi_result_get_numrows(lastResult);
					targetTime = GetMetaFloat(parent->UID, "TargetTime", NULL);
					
					// allocate id_array
					if(id_array = (unsigned int *)calloc(count, sizeof(unsigned int))){
						// fill id_array
						i=0;
						while(dbi_result_has_next_row(lastResult)){
							if(dbi_result_next_row(lastResult)){
								id_array[i] = dbi_result_get_ulonglong(lastResult, "ID");	
								i++;
							}
						}
						count = i;
						dbi_result_free(lastResult);
						lastResult = NULL;
								
						for(i=0; i<count; i++){
							if(Item = id_array[i]){
								tmp = ustr(Item);
								str_insertstr(&tmp, "item:///", 0);
								last = 0;
								if(i == count-1)
									last = 1;
								newUID = SplitItem(parent->UID, tmp, last);
								free(tmp);
								if(newUID == 0){
									tmp = ustr(Item);
									str_insertstr(&tmp, ": split failed on item:///", 0);
									str_insertstr(&tmp, (name = GetMetaData(parent->UID, "Name", 0)), 0);
									free(name);
									str_insertstr(&tmp, "[database] dbPick-", 0);
									serverLogMakeEntry(tmp);
									free(tmp);
								}else{								
									// increase the parent target time by the new item's duration
									if(targetTime){
										targetTime = targetTime + GetMetaFloat(newUID, "Duration", NULL);
										SetMetaData(parent->UID, "TargetTime", (tmp = fstr(targetTime, 1)));
										free(tmp);
									}								
								}
							}
						}
						free(id_array);
					}
				}
				free(mode);
			}else{
				tmp = GetMetaData(parent->UID, "Name", 0);
				str_insertstr(&tmp, "[database] dbPick-", 0);
				str_appendstr(&tmp, ": no result.");
				serverLogMakeEntry(tmp);
				free(tmp);
			}
		}else{
			tmp = GetMetaData(parent->UID, "Name", 0);
			str_insertstr(&tmp, "[database] dbPick-", 0);
			str_appendstr(&tmp, ": NULL result.");
			serverLogMakeEntry(tmp);
			free(tmp);
		}
	}else{
		tmp = GetMetaData(parent->UID, "Name", 0);
		str_insertstr(&tmp, "[database] dbPick-", 0);
		str_appendstr(&tmp, ": timeout.");
		serverLogMakeEntry(tmp);
		free(tmp);
	}
	// all done... clean up!

	if(lastResult)
		dbi_result_free(lastResult);
				
	if(conn && (conn == pthread_getspecific(gthread_dbi_conn)))
		dbi_conn_error_handler(conn, NULL, NULL);
}

void CheckFolderResultReadable(char **urlStr){
	uint32_t localUID;
	int length;
	char *subString;
	char *tmp;
	FILE *fp;

	// check for .fpl files
	length = strlen(*urlStr);
	if(length > 4){
		subString = *urlStr + (length - 4);
		if((!strcmp(subString, ".fpl")) && (strstr(*urlStr, "file://") == *urlStr)){
			if(tmp = str_NthField(*urlStr, "://", 1)){
				// ignore host, if any
				if(subString = strchr(tmp, '/'))
					subString = uriDecode(subString);
				else
					subString = uriDecode(tmp);
				free(tmp);
				length = strlen(subString);
				if(length > 4){
					// check if an associated audio file exists
					subString[(length - 4)] = 0;
					if(fp = fopen(subString, "rb")){
						// file exists... do not use the associated play list!
						fclose(fp);
						str_setstr(urlStr, "");
						free(subString);
						return;
					}
				}
				free(subString);
			}
		}
	}

	// replaces the input url string with an empty string if the item the url references is not readable
	localUID = createMetaRecord(*urlStr, NULL);
	
	// fill the metadata record
	GetFileMetaData(localUID, *urlStr);
	if(GetMetaInt(localUID, "Missing", NULL)){
		// not a file we can read
		str_setstr(urlStr, "");
	}
	releaseMetaRecord(localUID);
}

void folderPickCleanUp(void *pass){
	int index;
	struct locals{
		dbi_result dbResult;
		struct dirent **entList;
		int count;
	} *ptr;

	ptr = (struct locals *)pass;
	// deallocate dir results if any
	for(index = 0; index < ptr->count; index++)
		free(ptr->entList[index]);
	if(ptr->count > -1)
		free(ptr->entList);
}

int scandirSelect(const struct dirent *ent){
	// eliminate all entries that are not regular files
	if(ent->d_type == DT_REG)
		return 1;
	else
		return 0;
}

char *traverseFolderListing(char **dir, uint32_t modified, unsigned short none, 
				unsigned short seq, unsigned short rerun, unsigned short first, 
											uint32_t randlim, unsigned short date){
	struct stat statRec;
	dbi_conn conn;
	dbi_inst instance;
	struct locals{
		dbi_result dbResult;
		struct dirent **entList;
		int count;
	} locBlock;
	int size, index, i;
	int remove;
	time_t cutoff, last;
	char *prefix = NULL;
	char *include = NULL;
	char *result = NULL;
	char *path = NULL; 
	char *encStr = NULL;
	char *sub_name = NULL;
	char *tmp;
	const char *name; 
	struct dbErr errRec;
	
	str_setstr(&result, "");
	conn = NULL;
	instance = NULL;
	locBlock.count = -1;
	pthread_cleanup_push((void (*)(void *))folderPickCleanUp, (void *)&locBlock);
	tmp = *dir;
	index = strlen(tmp);
	if(index < 1)
		goto cleanup;
	if(tmp[index-1] != directoryToken)  // make sure there is a trailing slash in the path name. 
		str_appendstr(dir, directoryTokenStr);
	
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup; 
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);	
	include = GetMetaData(0, "db_include_loc", 0);
	if(strlen(include) > 0){
		str_insertstr(&include, ",", 0);
		str_insertstr(&include, (tmp = GetMetaData(0, "db_loc", 0)), 0);
		free(tmp);
	}else
		include = GetMetaData(0, "db_loc", 0);

	if(modified == 0)
		none = 1;
		
	while(modified || none){
		// deallocate previous dir result, if any
		for(index = 0; index < locBlock.count; index++)
			free(locBlock.entList[index]);
		if(locBlock.count > -1)
			free(locBlock.entList);
		// get dir listing
		if((locBlock.count = scandir(*dir, &locBlock.entList, scandirSelect, alphasort)) > 0){
			
			// if modified time is set, purge list of itmes older than modified time
			if(modified){
				cutoff = time(NULL) - (modified * 3600); // convert to now - hours in seconds
				index = 0;
				while(index < locBlock.count){
					str_setstr(&path, *dir);
					str_appendstr(&path, locBlock.entList[index]->d_name);
					remove = stat(path, &statRec);
					if(remove == 0){
						if(statRec.st_mtime < cutoff){
							// it's too old, delete it from the list
							remove = 1;
						}
					}
					if(remove){
						// shift the list down and try again
						i = index;
						free(locBlock.entList[i]);
						while(i < locBlock.count-1){
							locBlock.entList[i] = locBlock.entList[i+1];
							i++;
						}
						locBlock.count--;
					}else{
						index++;
					}
				}
				if(path){
					free(path);
					path = NULL;
				}
			}
	
			if(locBlock.count > 0){
				if(date){
					// try to find a file with a name of YYYY-MM-DD i.e. 2006-7-13 (no leading zeros)
					char dateStr[11];
					time_t now;
					struct tm loc_t;
					
					now = time(NULL);
					localtime_r(&now, &loc_t);
					// Use date/time for file name
					strftime(dateStr, sizeof dateStr, "%Y-%m-%d", &loc_t);
					str_setstr(&result, "");
					for(index = 0; index < locBlock.count; index++){
						if(strstr(locBlock.entList[index]->d_name, dateStr) == locBlock.entList[index]->d_name){
							// got the item... encode as uri
							tmp = uriEncodeKeepSlash(*dir);
							str_setstr(&result, "file://");
							str_appendstr(&result, tmp);
							free(tmp);
							tmp = uriEncodeKeepSlash(locBlock.entList[index]->d_name);
							str_appendstr(&result, tmp);
							free(tmp);
							// see if this is a file we can handle
							CheckFolderResultReadable(&result);
							if(strlen(result) > 0)
								break;
						}
					}
					if(strlen(result) > 0){
						// we have a winner!
						goto cleanup;
					}
				}

				if(rerun){     // re-run last played if true (non-zero)		
					errRec.message = "FindLastFilePlayedFromDir ";
					dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
					locBlock.dbResult = NULL;
					// set directory as URL
					tmp = uriEncodeKeepSlash(*dir);
					str_setstr(&encStr, "file://");
					str_appendstr(&encStr, tmp);
					free(tmp);
					size = strlen(encStr);
					
					// encode URL in db format
					dbi_conn_quote_string(conn, &encStr);

					// perform the sql query function: Get last ADDED file name in this directory (back eight hours)
					locBlock.dbResult = dbi_conn_queryf(conn, "SELECT Time, Source AS Name FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
									"AND (Added & 1) = 0 AND Time > UNIX_TIMESTAMP(NOW()) - 28800 AND LEFT(Source, %d) = %s AND SUBSTRING(Source, %d) NOT "
									"LIKE '%%/%%' ORDER BY Time DESC LIMIT 1", prefix, prefix, include, size, encStr, size+1);
					// get first record (should be the only record)
					name = "";
					if(locBlock.dbResult){
						if(dbi_result_has_next_row(locBlock.dbResult)){
							if(dbi_result_next_row(locBlock.dbResult)){
								name = (const char*)dbi_result_get_string(locBlock.dbResult, "Name");
								if(name == NULL)
									name = "";
							}
						}
					}
					if(strlen(name) == 0){
						if(locBlock.dbResult)
							dbi_result_free(locBlock.dbResult);
						// perform the sql query function: Get last PLAYED file name in this directory
						locBlock.dbResult = dbi_conn_queryf(conn, "SELECT Time, Source AS Name FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
										"AND (Added & 1) <> 1 AND LEFT(Source, %d) = %s AND SUBSTRING(Source, %d) NOT LIKE '%%/%%' "
										"ORDER BY Time DESC LIMIT 1", prefix, prefix, include, size, encStr, size+1);
						// get first record (should be the only record)
						if(locBlock.dbResult){
							if(dbi_result_has_next_row(locBlock.dbResult)){
								if(dbi_result_next_row(locBlock.dbResult)){
									name = (const char*)dbi_result_get_string(locBlock.dbResult, "Name");
									if(name == NULL)
										name = "";
								}
							}
						}
					}
					// free the string we allocated
					if(encStr){
						free(encStr);
						encStr = NULL;
					}
					
					if((int)strlen(name) > size){
						// decode url encoding of the last played/added file url
						tmp = uriDecode(name + size);
						if(tmp)
							sub_name = tmp;	
						else		
							str_setstr(&sub_name, "");			

						if(strlen(sub_name)){
							// check if this file is in the directory listing
							for(index = 0; index < locBlock.count; index++){
								if(strcmp(locBlock.entList[index]->d_name, sub_name) == 0)
									break;
							}
							if(index == locBlock.count)
								str_setstr(&sub_name, "");			
						}
					}else
						str_setstr(&sub_name, "");			

					if(locBlock.dbResult){
						dbi_result_free(locBlock.dbResult);
						locBlock.dbResult = NULL;
					}
					
					// check if item is readable
					if(strlen(sub_name)){
						// encode as uri
						tmp = uriEncodeKeepSlash(*dir);
						str_setstr(&result, "file://");
						str_appendstr(&result, tmp);
						free(tmp);
						tmp = uriEncodeKeepSlash(sub_name);
						str_appendstr(&result, tmp);
						free(tmp);
						// see if this is a file we can handle
						CheckFolderResultReadable(&result);	
					}
					// all done.
					if(sub_name){
						free(sub_name);
						sub_name = NULL;
					}
					if(conn && (conn == pthread_getspecific(gthread_dbi_conn)))
						dbi_conn_error_handler(conn, NULL, NULL);
					if(strlen(result) > 0)
						goto cleanup;
				}
				
				if(seq){     // sequencial first if true (non-zero)		
					errRec.message = "FindLastFilePlayedFromDir ";
					dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
					locBlock.dbResult = NULL;
					
					// set directory as URL
					tmp = uriEncodeKeepSlash(*dir);
					str_setstr(&encStr, "file://");
					str_appendstr(&encStr, tmp);
					free(tmp);
					size = strlen(encStr);
					
					// encode URL in db format
					dbi_conn_quote_string(conn, &encStr);

					// get end time of last item in list
					int listSize;
					listSize = queueCount();
					name = "";
					if(listSize > 0){
						// perform the sql query function: Get last ADDED file name in this directory 
						// looking back over the most recent items in the log no further than the current list length

						locBlock.dbResult = dbi_conn_queryf(conn, "SELECT Time, Name FROM (SELECT Time, Source AS Name FROM %slogs "
										"USE INDEX (%slogs_time) WHERE Location IN(%s) AND (Added & 1) = 1 ORDER BY Time DESC LIMIT %d) As inqueue "
										"WHERE LEFT(Name, %d) = %s AND SUBSTRING(Name, %d) NOT LIKE '%%/%%' ORDER BY Time DESC LIMIT 1", 
										prefix, prefix, include, listSize, size, encStr, size+1);

						// get first record (should be the only record)
						if(locBlock.dbResult){
							if(dbi_result_has_next_row(locBlock.dbResult)){
								if(dbi_result_next_row(locBlock.dbResult)){
									name = (const char*)dbi_result_get_string(locBlock.dbResult, "Name");
									if(name == NULL)
										name = "";
								}
							}
						}
					}
					if(strlen(name) == 0){
						if(locBlock.dbResult)
							dbi_result_free(locBlock.dbResult);
						// perform the sql query function: Get last PLAYED file name in this directory
						locBlock.dbResult = dbi_conn_queryf(conn, "SELECT Time, Source AS Name FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
										"AND (Added & 1) <> 1 AND LEFT(Source, %d) = %s AND SUBSTRING(Source, %d) NOT LIKE '%%/%%' "
										"ORDER BY Time DESC LIMIT 1", prefix, prefix, include, size, encStr, size+1);
						// get first record (should be the only record)
						if(locBlock.dbResult){
							if(dbi_result_has_next_row(locBlock.dbResult)){
								if(dbi_result_next_row(locBlock.dbResult)){
									name = (const char*)dbi_result_get_string(locBlock.dbResult, "Name");
									if(name == NULL)
										name = "";
								}
							}
						}
					}
					// free old stuff
					if(encStr){ 
						free(encStr);NULL;
						encStr = NULL;
					}
					if(locBlock.dbResult){
						dbi_result_free(locBlock.dbResult);
						locBlock.dbResult = NULL;
					}

					if((int)strlen(name) > size){
						// decode url encoding of the last played/added file url
						tmp = uriDecode(name + size);
						if(tmp)
							sub_name = tmp;	
						else		
							str_setstr(&sub_name, "");			

						if(strlen(sub_name)){
							// find next item, if any
							for(index = 0; index < locBlock.count; index++){
								if(strcmp(locBlock.entList[index]->d_name, sub_name) > 0){
									tmp = uriEncodeKeepSlash(*dir);
									str_setstr(&result, "file://");
									str_appendstr(&result, tmp);
									free(tmp);
									tmp = uriEncodeKeepSlash(locBlock.entList[index]->d_name);
									str_appendstr(&result, tmp);
									free(tmp);
									// see if this is a file we can handle
									CheckFolderResultReadable(&result);
									if(strlen(result) > 0)
										break;
								}
							}
						}
					}
					if(conn && (conn == pthread_getspecific(gthread_dbi_conn)))
						dbi_conn_error_handler(conn, NULL, NULL);
					if(sub_name){
						free(sub_name);
						sub_name = NULL;
					}
					if(strlen(result) > 0)
						goto cleanup;
				}
				
				if(first){
					// first readable alphebetical item, if any.
					for(index = 0; index < locBlock.count; index++){
						// got the next item in sequence
						tmp = uriEncodeKeepSlash(*dir);
						str_setstr(&result, "file://");
						str_appendstr(&result, tmp);
						free(tmp);
						tmp = uriEncodeKeepSlash(locBlock.entList[index]->d_name);
						str_appendstr(&result, tmp);
						free(tmp);
						// see if this is a file we can handle
						CheckFolderResultReadable(&result);
						if(strlen(result) > 0)
							break;
					}
					if(strlen(result) > 0)
						goto cleanup;
				}
				
				if(randlim){
					// random if sequencial has failed or was false and random is non-zero
					while(locBlock.count > 0){
						index = (int)((locBlock.count - 1) * RandomNumber());
						tmp = uriEncodeKeepSlash(*dir);
						str_setstr(&result, "file://");
						str_appendstr(&result, tmp);
						free(tmp);
						tmp = uriEncodeKeepSlash(locBlock.entList[index]->d_name);
						str_appendstr(&result, tmp);
						free(tmp);
						// see if this is a file we can handle
						CheckFolderResultReadable(&result);
						if(strlen(result) > 0){
							// check past play history, see if it qualifies

							// connection may have changed... get latest
							instance = get_thread_dbi(&conn);
							if(conn){
								errRec.message = "FindLastFilePlayedTime ";
								dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
								
								// encode URL in db format
								encStr = strdup(result);
								dbi_conn_quote_string(conn, &encStr);
								
								// perform the sql query function: Get Time this file was last played
								locBlock.dbResult = dbi_conn_queryf(conn, "SELECT Time AS Time FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
											"AND Source = %s ORDER BY Time DESC LIMIT 1", prefix, prefix, include, encStr);
								// free the c-string we allocated
								if(encStr) 
									free(encStr);
								if(locBlock.dbResult){
									// get first record (should be the only record)
									if(dbi_result_has_next_row(locBlock.dbResult)){
										if(dbi_result_next_row(locBlock.dbResult))
											last = dbi_result_get_longlong(locBlock.dbResult, "Time");
									}
									dbi_result_free(locBlock.dbResult);
									locBlock.dbResult = NULL;
								}
								dbi_conn_error_handler(conn, NULL, NULL);
							}

							if(((time(NULL) - last) / 3600) < (signed int)randlim){
								// played too recently
								str_setstr(&result, "");
							}
						}
							
						if(strlen(result) == 0){
							// not a valid pick... shift the list down and try again
							free(locBlock.entList[index]);
							while(index < locBlock.count-1){
								locBlock.entList[index] = locBlock.entList[index+1];
								index++;
							}
							locBlock.count--;
						}else{
							// we have a winner!
							goto cleanup;
						}
					}
				}
			}			
		}
		if(modified){
			// try again if none is true
			modified = 0;
		}else{
			// give up!
			none = 0;
		}
	}

cleanup:
	// all done... clean up!
	pthread_cleanup_pop(1);
	if(prefix)
		free(prefix);
	if(include)
		free(include);
	return result;
}

void folderPick(taskRecord *parent){
	unsigned short nomod, seq, first, rerun;
	long long date;
	uint32_t mod, rand, size;
	uint32_t newUID;
	float segout;
	char *tmp;
	char *pick, *encode;
	char *dir;
	char *target;
	char *priority;
	
	dir = GetMetaData(parent->UID, "Folder", 0);
	mod = GetMetaInt(parent->UID, "Modified", NULL);
	nomod = GetMetaInt(parent->UID, "NoModLimit", NULL);
	seq = GetMetaInt(parent->UID, "Sequencial", NULL);
	first = GetMetaInt(parent->UID, "First", NULL);
	rand = GetMetaInt(parent->UID, "Random", NULL);
	rerun = GetMetaInt(parent->UID, "Rerun", NULL);
	date = atoll(tmp = GetMetaData(parent->UID, "Date", 0));
	free(tmp);
	segout = GetMetaFloat(parent->UID, "def_segout", NULL);

	pick = traverseFolderListing(&dir, mod, nomod, seq, rerun, first, rand, date);
	free(dir);
	if(pick && strlen(pick)){
		
		// make file url from pick path
		encode = uriEncodeKeepSlash(pick);
		str_insertstr(&encode, "file://", 0);
		// add to playlist in pick placeholder position
		newUID = SplitItem(parent->UID, encode, 1);
		if(newUID){
			if(segout){
				float aFloat;
				aFloat = GetMetaFloat(newUID, "Duration", NULL);
				aFloat = aFloat - segout;
				if(aFloat < 0.0)
					aFloat = 0.0;
				SetMetaData(newUID, "SegOut", (tmp = fstr(aFloat, 2)));
				free(tmp);
			}
		}
		free(encode);
	}				
}

uint32_t dbGetFillID(time_t *when){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result;
	char *sql = NULL;
	char *prefix = NULL;
	char *loc = NULL;
	uint32_t result;
	int hr, min;
	struct tm tm_rec;
	struct dbErr errRec;
	
	result = 0;
	// fill time record
	localtime_r(when, &tm_rec);
	// set up database access
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);
	loc = GetMetaData(0, "db_loc", 0);

	errRec.message = "dbGetFillID ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	// first we set a mysql connection session variable to the priority of the highest over-riding fill item
	str_setstr(&sql,"SELECT @orPriority:= (((FLOOR([PFX]schedule.Fill / 60) + [PFX]hourmap.Map) > %d) "
						"OR (((FLOOR([PFX]schedule.Fill / 60) + [PFX]hourmap.Map) = %d) AND "
						"((([PFX]schedule.Fill %% 60) + [PFX]schedule.Minute) > %d))) * "
						"[PFX]schedule.Priority As override "
					"FROM ([PFX]schedule, [PFX]hourmap) "
					"LEFT JOIN [PFX]rest ON ([PFX]schedule.Item = [PFX]rest.Item AND [PFX]rest.Location = %s) "
					"WHERE [PFX]rest.Added IS NULL AND [PFX]schedule.Hour = [PFX]hourmap.Hour "
						"AND [PFX]schedule.Fill <> 0 AND ([PFX]schedule.Location IS NULL OR "
						"[PFX]schedule.Location = %s) AND [PFX]schedule.Priority > 0 "
						"AND ([PFX]schedule.Date = 0 OR [PFX]schedule.Date = %d) AND ([PFX]schedule.Day = 0 "
						"OR [PFX]schedule.Day = %d) AND ([PFX]schedule.Month = 0 OR [PFX]schedule.Month = %d) "
						"AND ([PFX]hourmap.Map < %d OR ([PFX]hourmap.Map = %d AND [PFX]schedule.Minute <= %d)) "
					"ORDER BY override DESC, [PFX]hourmap.Map DESC, [PFX]schedule.Minute DESC LIMIT 1");
					
	str_ReplaceAll(&sql, "[PFX]", prefix);

	// perform the sql query function, including only items with priority greater than or equal to the over-riding priority above
    db_result = dbi_conn_queryf(conn, sql, tm_rec.tm_hour, tm_rec.tm_hour, tm_rec.tm_min, loc, loc, 
								tm_rec.tm_mday, tm_rec.tm_wday+1, tm_rec.tm_mon+1, tm_rec.tm_hour, tm_rec.tm_hour, tm_rec.tm_min);
	if(db_result == NULL)
		goto cleanup;
	dbi_result_free(db_result);

	str_setstr(&sql,"SELECT [PFX]schedule.Item, [PFX]hourmap.Map AS Hour, "
						"[PFX]schedule.Minute, [PFX]schedule.Priority "
					"FROM ([PFX]schedule, [PFX]hourmap) "
					"LEFT JOIN [PFX]rest ON ([PFX]schedule.Item = [PFX]rest.Item "
						"AND [PFX]rest.Location = %s) "
					"WHERE [PFX]rest.Added IS NULL AND [PFX]schedule.Hour = [PFX]hourmap.Hour "
						"AND [PFX]schedule.Fill <> 0 AND ([PFX]schedule.Location IS NULL "
						"OR [PFX]schedule.Location = %s) AND [PFX]schedule.Priority >= @orPriority "
						"AND ([PFX]schedule.Date = 0 OR [PFX]schedule.Date = %d) AND ([PFX]schedule.Day = 0 "
						"OR [PFX]schedule.Day = %d) AND ([PFX]schedule.Month = 0 OR [PFX]schedule.Month = %d) "
						"AND ([PFX]hourmap.Map < %d OR ([PFX]hourmap.Map = %d AND [PFX]schedule.Minute <= %d)) "
					"ORDER BY Hour DESC, Minute DESC, Priority DESC LIMIT 1");
					
	str_ReplaceAll(&sql, "[PFX]", prefix);

	// perform the sql query function
	db_result = dbi_conn_queryf(conn, sql, loc, loc, tm_rec.tm_mday, tm_rec.tm_wday+1, 
									tm_rec.tm_mon+1, tm_rec.tm_hour, tm_rec.tm_hour, tm_rec.tm_min);
		
	if(db_result == NULL)
		goto cleanup;
	// get first record (should be the only record)
	if(!dbi_result_has_next_row(db_result)){
		dbi_result_free(db_result);
		goto cleanup;
	}
	if(dbi_result_next_row(db_result)){ 
		result = dbi_result_get_uint(db_result, "Item");
		hr = dbi_result_get_int(db_result, "Hour");
		min = dbi_result_get_int(db_result, "Minute");
		// update when pointer to the unix time when the fill item was schedule to start
		tm_rec.tm_hour = hr;
		tm_rec.tm_min = min;
		tm_rec.tm_sec = 0;
		*when = mktime(&tm_rec);		
	}
	dbi_result_free(db_result);

cleanup:
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	if(prefix)
		free(prefix);
	if(loc)
		free(loc);
	if(sql)
		free(sql);
	return result;
}

char *dbGetItemName(uint32_t ID){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result;
	const char *Str;
	char *prefix = NULL;
	char *result = NULL;
	struct dbErr errRec;
	
	str_setstr(&result, "");
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);

	errRec.message = "dbGetItemName ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function
	db_result = dbi_conn_queryf(conn, "SELECT Name FROM %stoc WHERE ID = %lu", prefix, ID);
	if(db_result == NULL)
		goto cleanup;
	// get first record (should be the only record)
	if(!dbi_result_has_next_row(db_result)){
		dbi_result_free(db_result);
		goto cleanup;
	}
	if(dbi_result_next_row(db_result)){ 
		Str = (const char*)dbi_result_get_string(db_result, "Name");
		if(Str)
			str_setstr(&result, Str);
	}
	dbi_result_free(db_result);

cleanup:
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	if(prefix)
		free(prefix);
	return result;
}

void dbMacroReplace(char **query){
	time_t endTime;
	char *tmp;
	char *include;
	uint32_t ID;
	int pos;

	endTime = 0;
	if((tmp = GetMetaData(0, "db_type", 0)) == "postgresql")
		// postgress: convert MySQL last_insert_ID function to postgress equivelent
		str_ReplaceAll(query, "LAST_INSERT_ID()", "CURRVAL('serial_variable')");
	free(tmp);
	
	include = GetMetaData(0, "db_include_loc", 0);
	if(strlen(include) > 0){
		str_insertstr(&include, ",", 0);
		str_insertstr(&include, (tmp = GetMetaData(0, "db_loc", 0)), 0);
		free(tmp);
	}else{
		free(include);
		include  = GetMetaData(0, "db_loc", 0);
	}
	
	str_ReplaceAll(query, "[loc-id]", (tmp = GetMetaData(0, "db_loc", 0)));
	free(tmp);
	str_ReplaceAll(query, "[loc-ids]", include);
	str_ReplaceAll(query, "[prefix]", (tmp = GetMetaData(0, "db_prefix", 0)));
	free(tmp);
	str_ReplaceAll(query, "[!loc-id]", "[loc-id]");
	str_ReplaceAll(query, "[!loc-ids]", "[loc-ids]");
	str_ReplaceAll(query, "[!prefix]", "[prefix]");
	str_ReplaceAll(query, "[!thisID]", "[thisID]");

	str_ReplaceAll(query, "\\n", "\n");
	
	if(strstr(*query, "[endtime]")){
		endTime = queueGetEndTime();
		str_ReplaceAll(query, "[endtime]", (tmp = fstr(endTime, 0)));
		free(tmp);	
	}
	
	if(strstr(*query, "[fill-id]")){
		if(endTime == 0)
			endTime = queueGetEndTime();
		ID = dbGetFillID(&endTime);
		str_ReplaceAll(query, "[fill-id]", (tmp = ustr(ID)));
		free(tmp);	
	}
	free(include);
}

uint32_t dbGetNextScheduledItem(dbi_result *db_result, time_t *targetTime, short *priority, time_t from_t, time_t to_t, unsigned char highOnly){
	char *sql = NULL;
	char *tmp;
	uint32_t result;
	struct tm from_rec, to_rec;
	short hr, min;
	struct dbErr errRec;
	dbi_conn conn = NULL;
	dbi_inst instance;
	
	if(difftime(to_t, from_t) > 10800)
		// greater than three hours bug trap???
		return 0;
	localtime_r(&from_t, &from_rec);
	localtime_r(&to_t, &to_rec);
	result = 0;
	if(db_result == NULL)
		return 0;
	if(*db_result == NULL){
		// new result requested
		// set up database access
		instance = get_thread_dbi(&conn);
		if(conn == NULL){
			// no connection yet for this thread... 
			if((conn = dbSetupConnection(instance, 1)) == NULL){
				goto cleanup;
			}
		}		
		
		errRec.message = "dbGetNextScheduledItem ";
		dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
		
		str_setstr(&sql, "SELECT [prefix]schedule.Item AS Item, [prefix]hourmap.Map AS Hour, MIN([prefix]schedule.Minute) AS Minute, ");
		str_appendstr(&sql, "MAX([prefix]schedule.Priority) AS Priority FROM ([prefix]schedule, [prefix]hourmap, [prefix]daymap) ");
		str_appendstr(&sql, "LEFT JOIN [prefix]rest ON ([prefix]schedule.Item = [prefix]rest.Item AND [prefix]rest.Location = ");
		str_appendstr(&sql, "[loc-id]) WHERE [prefix]rest.Added IS NULL AND ");
		if(highOnly)
			str_appendstr(&sql, "[prefix]schedule.Priority >= 8 AND ");
		str_appendstr(&sql, "[prefix]schedule.Day = [prefix]daymap.Day AND [prefix]schedule.Hour = [prefix]hourmap.Hour ");
		str_appendstr(&sql, "AND [prefix]schedule.Fill = 0 AND ([prefix]schedule.Location IS NULL OR ");
		str_appendstr(&sql, "[prefix]schedule.Location = [loc-id]) AND [prefix]schedule.Priority > 0 ");
	  
		if(from_rec.tm_mday == to_rec.tm_mday){
			// all in a single day
			str_appendstr(&sql, "AND ([prefix]schedule.Month = 0 OR [prefix]schedule.Month = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_mon + 1)));
			free(tmp);
			str_appendstr(&sql, " ) ");
			str_appendstr(&sql, "AND ([prefix]schedule.Date = 0 OR [prefix]schedule.Date = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_mday)));
			free(tmp);
			str_appendstr(&sql, " ) ");
			str_appendstr(&sql, "AND [prefix]daymap.Map = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_wday + 1)));
			free(tmp);
			str_appendstr(&sql, " ");
			if(from_rec.tm_hour == to_rec.tm_hour){
				// with in the same hour
				str_appendstr(&sql, "AND [prefix]hourmap.Map = ");
				str_appendstr(&sql, (tmp = istr(from_rec.tm_hour)));
				free(tmp);
				str_appendstr(&sql, " AND ([prefix]schedule.Minute > ");
				str_appendstr(&sql, (tmp = istr(from_rec.tm_min)));
				free(tmp);
				str_appendstr(&sql, " AND [prefix]schedule.Minute <= ");
				str_appendstr(&sql, (tmp = istr(to_rec.tm_min)));
				free(tmp);
				str_appendstr(&sql, " ) ");
			}else{
				// hour rolled over
				str_appendstr(&sql, "AND (([prefix]hourmap.Map > ");
				str_appendstr(&sql, (tmp = istr(from_rec.tm_hour)));
				free(tmp);
				str_appendstr(&sql, " AND [prefix]hourmap.Map < ");
				str_appendstr(&sql, (tmp = istr(to_rec.tm_hour)));
				free(tmp);
				str_appendstr(&sql, ") OR ([prefix]hourmap.Map = ");
				str_appendstr(&sql, (tmp = istr(to_rec.tm_hour)));
				free(tmp);
				str_appendstr(&sql, " AND [prefix]schedule.Minute <= ");
				str_appendstr(&sql, (tmp = istr(to_rec.tm_min)));
				free(tmp);
				str_appendstr(&sql, " ) OR ([prefix]hourmap.Map = ");
				str_appendstr(&sql, (tmp = istr(from_rec.tm_hour)));
				free(tmp);
				str_appendstr(&sql, " AND [prefix]schedule.Minute > ");
				str_appendstr(&sql, (tmp = istr(from_rec.tm_min)));
				free(tmp);
				str_appendstr(&sql, " )) ");
			}
		}else{
			// split across a day change
			// start day
			str_appendstr(&sql, "AND ((([prefix]schedule.Month = 0 OR [prefix]schedule.Month = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_mon + 1)));
			free(tmp);
			str_appendstr(&sql, ") AND ([prefix]schedule.Date = 0 OR [prefix]schedule.Date = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_mday)));
			free(tmp);
			str_appendstr(&sql, ") AND [prefix]daymap.Map = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_wday + 1)));
			free(tmp);
			str_appendstr(&sql, " AND ([prefix]hourmap.Map > ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_hour)));
			free(tmp);
			str_appendstr(&sql, " OR ([prefix]hourmap.Map = ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_hour)));
			free(tmp);
			str_appendstr(&sql, " AND [prefix]schedule.Minute > ");
			str_appendstr(&sql, (tmp = istr(from_rec.tm_min)));
			free(tmp);
			str_appendstr(&sql, "))) ");
			// stop day
			str_appendstr(&sql, "OR (([prefix]schedule.Month = 0 OR [prefix]schedule.Month = ");
			str_appendstr(&sql, (tmp = istr(to_rec.tm_mon + 1)));
			free(tmp);
			str_appendstr(&sql, ") AND ([prefix]schedule.Date = 0 OR [prefix]schedule.Date = ");
			str_appendstr(&sql, (tmp = istr(to_rec.tm_mday)));
			free(tmp);
			str_appendstr(&sql, ") AND [prefix]daymap.Map = ");
			str_appendstr(&sql, (tmp = istr(to_rec.tm_wday + 1)));
			free(tmp);
			str_appendstr(&sql, " AND ([prefix]hourmap.Map < ");
			str_appendstr(&sql, (tmp = istr(to_rec.tm_hour)));
			free(tmp);
			str_appendstr(&sql, " OR ([prefix]hourmap.Map = ");
			str_appendstr(&sql, (tmp = istr(to_rec.tm_hour)));
			free(tmp);
			str_appendstr(&sql, " AND [prefix]schedule.Minute <= ");
			str_appendstr(&sql, (tmp = istr(to_rec.tm_min)));
			free(tmp);
			str_appendstr(&sql, ")))) ");
		}	  
		str_appendstr(&sql, "GROUP BY [prefix]schedule.Item ORDER BY Priority DESC");
		dbMacroReplace(&sql);
		// perform the sql query function
		*db_result = dbi_conn_queryf(conn, sql);
		free(sql);
		if(*db_result == NULL){
			goto cleanup;
		}
	}
	
	// Return next row from query result
	if(dbi_result_has_next_row(*db_result)){
		if(dbi_result_next_row(*db_result)){
			*priority = dbi_result_get_int(*db_result, "Priority");
			hr = dbi_result_get_int(*db_result, "Hour");
			min = dbi_result_get_int(*db_result, "Minute");
			if(hr < from_rec.tm_hour){
				// must be after a day roll-over
				to_rec.tm_hour = hr;
				to_rec.tm_min = min;
				to_rec.tm_sec = 0;
				*targetTime = mktime(&to_rec);
			}else{
				// all in the same day
				from_rec.tm_hour = hr;
				from_rec.tm_min = min;
				from_rec.tm_sec = 0;
				*targetTime = mktime(&from_rec);
			}
			if(conn)
				dbi_conn_error_handler(conn, NULL, NULL);
			return dbi_result_get_uint(*db_result, "Item");
		}
	}
	
cleanup:
	// no more rows to return
	if(*db_result)
		dbi_result_free(*db_result);
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	return 0;
}

void dbSaveFilePos(uint32_t UID, float position){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result;
	uint32_t ID;
	char *prefix, *tmp;
	struct dbErr errRec;
	
	if(UID == 0)
		return;
	ID = GetMetaInt(UID, "ID", NULL);
	if(ID == 0)
		return;
		
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			return;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);
	errRec.message = "dbSaveFilePos ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function
    db_result = dbi_conn_queryf(conn, "UPDATE %sfile SET Memory = %f WHERE ID = %lu", prefix, position, ID);
	dbi_result_free(db_result);
	dbi_conn_error_handler(conn, NULL, NULL);
	free(prefix);
}

char *dbGetReqestComment(time_t theTime){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result;
	const char *Str;
	char *prefix = NULL;
	char *loc = NULL;
	char *result = NULL;
	uint32_t count;
	uint32_t row;
	struct dbErr errRec;
	
	str_setstr(&result, "");
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);
	loc = GetMetaData(0, "db_loc", 0);

	errRec.message = "dbGetReqestComment ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function -- first look for comments posted after lastTime
	db_result = dbi_conn_queryf(conn, "SELECT comment, ID FROM %srequest WHERE Item = 0 AND (Location = %s OR Location = 0) AND Time <= FROM_UNIXTIME(%ld) ORDER BY Time ASC LIMIT 1",
								prefix, loc, theTime);
	if(db_result != NULL){
		// get first record (should be the only record)
		if(dbi_result_has_next_row(db_result)){
			if(dbi_result_next_row(db_result)){ 
				Str = (const char*)dbi_result_get_string(db_result, "comment");
				if(Str){
					str_setstr(&result, Str);
					// and delete it from the database since we are about to post it.
					row = dbi_result_get_uint(db_result, "ID");
					dbi_result_free(db_result);
					db_result = dbi_conn_queryf(conn, "DELETE FROM %srequest WHERE ID = %lu", prefix, row);
				}
			}
		}
		dbi_result_free(db_result);
	}
	if(strlen(result))
		goto cleanup;
		
	// no comments... get one of the comments out of our pool
	db_result = dbi_conn_queryf(conn, "SELECT comment FROM %srequest WHERE Item = 0 AND (Location = %s OR Location = 0) AND Time > FROM_UNIXTIME(%lu)",
								prefix, loc, theTime);
	if(db_result != NULL){
		if(count = dbi_result_get_numrows(db_result)){
			row = (uint32_t)(count * RandomNumber());
			if(dbi_result_seek_row(db_result, row + 1)){ 
				Str = (const char*)dbi_result_get_string(db_result, "comment");
				if(Str)
					str_setstr(&result, Str);
			}
		}
		dbi_result_free(db_result);
	}

cleanup:
	if(prefix)
		free(prefix);
	if(loc)
		free(loc);

	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	return result;
}

char *dbGetCurrentMSG(void){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result;
	const char *Str;
	char *prefix = NULL;
	char *loc = NULL;
	char *result = NULL;
	struct dbErr errRec;
	
	str_setstr(&result, "");
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);
	loc = GetMetaData(0, "db_loc", 0);

	errRec.message = "dbGetCurrentMSG ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function -- first look for comments posted after lastTime
	db_result = dbi_conn_queryf(conn, "SELECT %smeta.Value AS MSG FROM %slogs, %smeta WHERE %slogs.Item = %smeta.ID AND %smeta.Parent = '%stoc' AND %smeta.Property = 'SCURL' AND %slogs.Location = %s AND (%slogs.Added & 1) <> 1 ORDER BY %slogs.Time DESC LIMIT 1",
								prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, loc, prefix, prefix);
	if(db_result != NULL){
		// get first record (should be the only record)
		if(dbi_result_has_next_row(db_result)){
			if(dbi_result_next_row(db_result)){ 
				Str = (const char*)dbi_result_get_string(db_result, "MSG");
				if(Str)
					str_setstr(&result, Str);
			}
		}
		dbi_result_free(db_result);
	}
	
cleanup:
	if(prefix)
		free(prefix);
	if(loc)
		free(loc);
		
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	return result;
}

uint32_t IDSearchArtistAlbumTitle(const char *Artist, const char *Album, const char *Title){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result;
	uint32_t recID;
	char *prefix = NULL;
	char *encArtist = NULL;
	char *encAlbum = NULL;
	char *encTitle = NULL;
	struct dbErr errRec;
	
	recID = 0;
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);

	errRec.message = "IDSearchArtistAlbumTitle ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	str_setstr(&encArtist, Artist);
	dbi_conn_quote_string(conn, &encArtist);

	str_setstr(&encAlbum, Album);
	dbi_conn_quote_string(conn, &encAlbum);

	str_setstr(&encTitle, Title);
	dbi_conn_quote_string(conn, &encTitle);

	// perform the sql query function
	result = dbi_conn_queryf(conn, "SELECT %stoc.ID FROM %stoc, %sfile, %sartist, %salbum WHERE %sArtist.ID = %sfile.artist "
			"AND %salbum.ID = %sfile.album AND %stoc.Name = %s AND %sartist.Name = %s AND %salbum.Name = %s LIMIT 1", 
			prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, encTitle, prefix, encArtist, prefix, encAlbum);
	if(result == NULL)
		goto cleanup;
	// get first record (should be the only record)
	if(!dbi_result_has_next_row(result)){
		dbi_result_free(result);
		goto cleanup;
	}
	if(dbi_result_next_row(result)){ 
		recID = dbi_result_get_uint(result, "ID");
	}
		
cleanup:
	// free char strings
	if(prefix)
		free(prefix);
	if(encArtist)
		free(encArtist);
	if(encAlbum)
		free(encAlbum);
	if(encTitle)
		free(encTitle);
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	return recID;
}

char *FindFromMeta(uint32_t UID){
	uint32_t id, localUID;
	char *tmp, *var1, *var2, *var3; 
	char *result = NULL;
	char *fname;
	char buf[32];
	
	str_setstr(&result ,"");
	id = GetMetaInt(UID, "ID", NULL);
	if(id){
		// we have a database ID... see if we can use that
		str_setstr(&result, "item:///");
		str_appendstr(&result, (tmp = ustr(id)));
		free(tmp);
		if(!strcmp((tmp = GetMetaData(UID, "Type", 0)), "file")){
			free(tmp);
			// check if the database entry maps back to valid file
			localUID = createMetaRecord(result, NULL);
			// fill the metadata record
			GetURLMetaData(localUID, result);
			if(GetMetaInt(localUID, "Missing", NULL) == 0){
				// valid file
				releaseMetaRecord(localUID);
				return result;
			}
			free(tmp);
			releaseMetaRecord(localUID);
		}else{
			free(tmp);
			// otherwise, return a URL for the database item for other item types
			return result;	
		}
	}
	
	// ID invalid or for missing item...  try the URL
	free(result);
	result = GetMetaData(UID, "URL", 0);
	if(strlen(result)){
		localUID = createMetaRecord(result, NULL);
		// fill the metadata record
		GetURLMetaData(localUID, result);
		if(GetMetaInt(localUID, "Missing", NULL) == 0){
			// all is well 
			releaseMetaRecord(localUID);
			return result;
		}
		free(tmp);
		releaseMetaRecord(localUID);
				
		// still no go... try a one of the relative paths if a FPL URL property is set
		free(result);
		result = GetMetaData(UID, "FPL", 0);	// this is a URL
		if(strlen(result)){
			// replace the ".fpl" with "_media/" for the URL
			// note URLs always use '/' for directory delimiters, regadless of platform
			str_ReplaceAll(&result, ".fpl", "_media");
			// and add the file name from the old URL
			fname = GetMetaData(UID, "URL", 0);
			if(tmp = strrchr(fname, '/')){
				str_appendstr(&result, tmp);
				// and try again
				localUID = createMetaRecord(result, NULL);
				// fill the metadata record
				GetURLMetaData(localUID, result);
				if(GetMetaInt(localUID, "Missing", NULL) == 0){
					// all is well
					free(fname);
					releaseMetaRecord(localUID);
					return result;
				}
				free(tmp);
				releaseMetaRecord(localUID);
			}
			free(fname);
		}
	}	
 
	// Try finding ID using Hash/Mount/Prefix data if available
	tmp = GetMetaData(UID, "Prefix", 0);
	if(strlen(tmp)){
		free(tmp);
		var1 = GetMetaData(UID, "Path", 0);
		if(strlen(var1)){
			var2 = GetMetaData(UID, "Hash", 0);
			if(strlen(var2)){
				// search the databse for a match
				id = IDSearchMarkedFile(var1, var2);
				if(id){
					// got a match... check to make sure it isn't missing
					snprintf(buf, sizeof buf, "item:///%u", (unsigned int)id);
					free(result);
					result = strdup(buf); 
					localUID = createMetaRecord(result, NULL);
					// fill the metadata record
					GetURLMetaData(localUID, result);
					if(GetMetaInt(localUID, "Missing", NULL) == 0){
						free(var2);
						free(var1);
						// all is well 
						releaseMetaRecord(localUID);
						return result;
					}
					releaseMetaRecord(localUID);
				}
			}
			free(var2);
		}
		free(var1);
	}else
		free(tmp);

	// still not going well.  Try searching on Artist/Album/Title in the database
	var1 = GetMetaData(UID, "Artist", 0);
	if(strlen(var1)){
		var2 = GetMetaData(UID, "Album", 0);
		if(strlen(var2)){
			var3 = GetMetaData(UID, "Name", 0);
			if(strlen(var3)){
				// search the databse for a match
				id = IDSearchArtistAlbumTitle(var1, var2, var3);
				if(id){
					// got a match... check to make sure it isn't missing
					snprintf(buf, sizeof buf, "item:///%u", (unsigned int)id);
					free(result);
					result = strdup(buf); 
					localUID = createMetaRecord(result, NULL);
					// fill the metadata record
					GetURLMetaData(localUID, result);
					if(GetMetaInt(localUID, "Missing", NULL) == 0){
						free(var3);
						free(var2);
						free(var1);
						// all is well 
						releaseMetaRecord(localUID);
						return result;
					}
					releaseMetaRecord(localUID);
				}
			}
			free(var3);
		}
		free(var2);
	}
	free(var1);
	// We are SOL! 
	str_setstr(&result ,"");
	return result;
}

void GetdbTaskMetaData(uint32_t UID, uint32_t recID, dbi_conn conn){
	dbi_result result;
	const char *Key, *Val;
	char *tmp;
	char *prefix;
	int size;
	struct dbErr errRec;
	
	errRec.message = "GetdbTaskMetaData ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
		
	prefix = GetMetaData(0, "db_prefix", 0);
			
	// perform the sql query function
	result = dbi_conn_queryf(conn, "SELECT * FROM %stask WHERE ID = %lu", prefix, recID);
	if(result == NULL){
		SetMetaData(UID, "Missing", "1");
		dbi_conn_error_handler(conn, NULL, NULL);
		free(prefix);
		return;
	}
	if(!dbi_result_has_next_row(result))
		SetMetaData(UID, "Missing", "1");
	while(dbi_result_has_next_row(result)){
		if(dbi_result_next_row(result)){ 
			Key = dbi_result_get_string(result, "Property");
			Val = dbi_result_get_string(result, "Value");
			if(Key && Val){
				if(!strcmp(Key,"Query")){
					// URL type escape (%nn) the query string.
					tmp = uriEncode(Val);
					SetMetaData(UID, Key, tmp);
					free(tmp);
				}else{
					SetMetaData(UID, Key, Val);
				}
			}
		}
	}
	free(prefix);
	dbi_result_free(result);
	dbi_conn_error_handler(conn, NULL, NULL);
}

int GetdbFileMetaData(uint32_t UID, uint32_t recID, dbi_conn conn, unsigned char markMissing){
	dbi_result result;
	char *prefix;
	char *pre;
	char *rem;
	char *newURL;
	char *tmp, *msg;
	const char *ctmp;
	char fVol[4096];
	char buf[32];
	uint32_t uInt, albID, artID;
	double Float;
	unsigned char missing;
	unsigned char changed;
	char *path = NULL;
	char *mountName = NULL;
	char *urlStr = NULL;
	char *pathStr = NULL;
	char *hash = NULL;
	char *prefixList, *newurl;
	uint32_t listSize, i, p;
	int stat = -1;
	size_t plen = 0;
	size_t pos;
	struct dbErr errRec; 
	glob_t globbuf;

	errRec.message = "GetdbFileMetaData ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// perform the sql query function
	result = dbi_conn_queryf(conn, "SELECT * FROM %sfile WHERE ID = %lu", prefix, recID);
	if(result == NULL){
		goto cleanup;
	}
	// get first record (should be the only record)
	if(!dbi_result_has_next_row(result)){
		dbi_result_free(result);
		goto cleanup;
	}
	if(dbi_result_next_row(result)) { 
		ctmp = dbi_result_get_string(result, "URL");
        if(ctmp) 
			SetMetaData(UID, "URL", ctmp);		
		
		ctmp = dbi_result_get_string(result, "Hash");
		if(ctmp){
			SetMetaData(UID, "Hash", ctmp);
			hash = strdup(ctmp);
		}
		
		artID = dbi_result_get_uint(result, "Artist");
		snprintf(buf, sizeof buf, "%u", (unsigned int)artID);
		SetMetaData(UID, "ArtistID", buf);
		
		albID = dbi_result_get_uint(result, "Album");
		snprintf(buf, sizeof buf, "%u", (unsigned int)albID);
		SetMetaData(UID, "AlbumID", buf);
		
		Float = dbi_result_get_float(result, "Volume");
		snprintf(buf, sizeof buf, "%0.2f", Float);
		SetMetaData(UID, "Volume", buf);
		
		Float = dbi_result_get_float(result, "SegIn");
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "SegIn", buf);
		
		Float = dbi_result_get_float(result, "SegOut");
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "SegOut", buf);
		
		Float = dbi_result_get_float(result, "FadeOut");
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "FadeOut", buf);
		
		Float = dbi_result_get_float(result, "Intro");
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "Intro", buf);
		
		Float = dbi_result_get_float(result, "Memory");
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "Memory", buf);
		
		ctmp = dbi_result_get_string(result, "OutCue");
		if(ctmp) 
			SetMetaData(UID, "OutCue", ctmp);
		
		uInt = dbi_result_get_uint(result, "Track");
		snprintf(buf, sizeof buf, "%u", (unsigned int)uInt);
		SetMetaData(UID, "Track", buf);

		missing = dbi_result_get_uchar(result, "Missing");
		snprintf(buf, sizeof buf, "%d", missing);
		SetMetaData(UID, "Missing", buf);

		ctmp = dbi_result_get_string(result, "Path");
		if(ctmp && strlen(ctmp)){
			SetMetaData(UID, "Path", ctmp);
			pathStr = strdup(ctmp);
		
			ctmp = dbi_result_get_string(result, "Prefix");
			if(ctmp) 
				SetMetaData(UID, "Prefix", ctmp);
			
			dbi_result_free(result);
		}else{
			// fall back to old rev <= 3 Mount list
			ctmp = dbi_result_get_string(result, "Mount");
			if(ctmp){ 
				SetMetaData(UID, "Mount", ctmp);
				dbi_result_free(result);

				tmp = GetMetaData(UID, "Mount", 0);
				mountName = str_NthField(tmp, "/", str_CountFields(tmp, "/"));
				free(tmp);
			}
		}
				
		// get Artist Name
		result = dbi_conn_queryf(conn, "SELECT Name FROM %sartist WHERE ID = %lu", prefix, artID);
		// get first record (should be the only record)
		if(result){
			if(dbi_result_has_next_row(result)){
				if(dbi_result_next_row(result)) { 
					ctmp = dbi_result_get_string(result, "Name");
					if(ctmp)
						SetMetaData(UID, "Artist", ctmp);
				}
			}
			dbi_result_free(result);		
		}
		// get Album Name
		result = dbi_conn_queryf(conn, "SELECT Name FROM %salbum WHERE ID = %lu", prefix, albID);
		if(result){
			// get first record (should be the only record)
			if(dbi_result_has_next_row(result)){
				if(dbi_result_next_row(result)) { 
					ctmp = dbi_result_get_string(result, "Name");
					if(ctmp)
						SetMetaData(UID, "Album", ctmp);
				}
			}
			dbi_result_free(result);
		}
	}else{
		dbi_result_free(result);
		goto cleanup;
	}
	
	// check file related properties in db against the file itself... find file if needed.
	// use file as final authority for properties.  Start with these 2 assumptions:
	changed = 0;
	missing = 1;
	prefixList = GetMetaData(0, "file_prefixes", 0);
	listSize = str_CountFields(prefixList, ",") + 1;
	if(pathStr){
		// try various prefixes to acomidate different disk mount locations
		// using new path & prefix method
		tmp = GetMetaData(UID, "Prefix", 0);
		plen = strlen(tmp);
		// start with the full path as found in the data base: Prefix + Path
		str_setstr(&path, tmp);
		str_appendstr(&path, pathStr);
		free(tmp);
	}else{
		// use old mount & URL method of searching for files. if sucessful,
		// set path & prefix properties to use new method next time.
		urlStr = GetMetaData(UID, "URL", 0);
		if((tmp = str_NthField(urlStr, "://", 1)) == NULL){
			// bad URL string
			changed = 1; // missing flag is already set
			str_setstr(&path, "");
			str_setstr(&pathStr, "");
			free(tmp);
		}else{
			// ignore host portion of URL, if any
			if(path = strchr(tmp, '/'))
				path = uriDecode(path);
			else
				path = uriDecode(tmp);
			free(tmp);

			if(mountName && (strlen(mountName) > 1)){
				if(tmp = strstr(path, mountName)){ 
					/* given path = /longer/path/to/mount/some/file
					 * and mountName = mount
					 * tmp = mount/some/file */
					pathStr = strdup(tmp);
					plen = 1;
				}else
					str_setstr(&pathStr, "");
			}else
				str_setstr(&pathStr, "");
		}
		free(urlStr);
		urlStr = NULL;
	}	

	/* escaping all *,?,[ chars found in pathStr to prevent paths
	 * with these chars from being interpereted by the glob function
	 * below as wild-card matches.*/
	str_ReplaceAll(&pathStr, "*", "\\*");
	str_ReplaceAll(&pathStr, "?", "\\?");
	str_ReplaceAll(&pathStr, "[", "\\[");
		
	// path is Full Path, pathStr is the relative path, if any		
	// i.e. path = /longer/path/to/mount/some/file
	// and  pathStr = mount/some/file
	globbuf.gl_offs = 0;
	globbuf.gl_pathc = 0;
	i = 0;
	p = 0;
	do{
		if(CheckFileHashMatch(path, hash)){
			// Hash code agrees with database, not missing
			missing = 0;
			break;
		}else{
			changed = 1;
		}
		// try to create another path with next prefix or next glob items
		do{	
			if(i < globbuf.gl_pathc){
				// next path in glob list
				str_setstr(&path, globbuf.gl_pathv[i]);
				i++;
				break;
			}else{
				tmp = str_NthField(prefixList, ",", p);
				if(tmp && strlen(pathStr)){
					str_setstr(&path, tmp);
					str_appendstr(&path, pathStr);

					i = 0;
					if(globbuf.gl_pathc){
						globfree(&globbuf);
						globbuf.gl_pathc = 0;
					}
					if(!glob(path, GLOB_NOSORT, NULL, &globbuf)){
						if(globbuf.gl_pathc){
							// found a path in new glob list
							str_setstr(&path, globbuf.gl_pathv[0]);
							i++;
							p++;
							free(tmp);
							break;
						}
					}
					free(tmp);
					p++;
				}else{
					// no more prefixes
					p = listSize+1;
					if(tmp)
						free(tmp);
				}
			}
		}while(p < listSize);
		
	}while(missing && plen && (p <= listSize));
	if(globbuf.gl_pathc){
		globfree(&globbuf);
	}
	free(prefixList);
	
	if(missing){
		// can't find it
		// set missing metadata flag
		SetMetaData(UID, "Missing", "1");	

		msg = NULL;
		str_setstr(&msg, "[database] GetdbFileMetaData-");
		str_appendstr(&msg, (tmp = GetMetaData(UID, "Name", 0)));
		free(tmp);
		str_appendstr(&msg, " ID#");
		str_appendstr(&msg, (tmp = ustr(recID)));
		free(tmp);
		str_appendstr(&msg, ": File is missing.");
		serverLogMakeEntry(msg);
		free(msg);
		stat = -2;
	}else{
		// found it... set URL if not set or if changed
		tmp = GetMetaData(UID, "URL", 0);
		if(changed || !strlen(tmp)){
			// re-encode URL
			free(tmp);
			newurl = NULL;
			tmp = uriEncodeKeepSlash(path);
			str_setstr(&newurl, "file://");
			str_appendstr(&newurl, tmp);
			free(tmp);
			SetMetaData(UID, "URL", newurl);
			free(newurl);
		}else
			free(tmp);
		
		tmp = path;
		if(pre = getFilePrefixPoint(&tmp)){
			SetMetaData(UID, "Prefix", pre);
			SetMetaData(UID, "Path", tmp);
			if(tmp != path)
				free(tmp);
			free(pre);
		}
		
		if(GetMetaInt(UID, "Missing", NULL))
		   // was flag as missing, but it's not really missing... nothing else is different
		   changed = 1;		
		
		// unset missing metadata flag
		SetMetaData(UID, "Missing", "0");	
		stat = 0;
	}
		
	if(markMissing){
		// if we are to manage missing files in the database
		if(changed){
			// status has changed... update database
			if(missing){
				// mark record as missing
				result = dbi_conn_queryf(conn, "UPDATE %sfile SET Missing = 1 WHERE ID = %lu", prefix, recID);
				if(result)
					dbi_result_free(result);
			}else{
				stat = 1;
				// unmark record as missing and update file location, etc.
				pre = GetMetaData(UID, "Prefix", 0);
				dbi_conn_quote_string(conn, &pre);
				rem = GetMetaData(UID, "Path", 0);
				dbi_conn_quote_string(conn, &rem);
				newURL = GetMetaData(UID, "URL", 0);
				dbi_conn_quote_string(conn, &newURL);
				// do the update
				result = dbi_conn_queryf(conn, 
					"UPDATE %sfile SET Missing = 0, Hash = '%s', Path = %s,  Prefix = %s, URL = %s WHERE ID = %lu", 
					 prefix, tmp=GetMetaData(UID, "Hash", 0), rem, pre, newURL, recID);
				free(tmp);
				if(result)
					dbi_result_free(result);
				free(rem);
				free(pre);
				free(newURL);
			}
		}
	}
cleanup:
	dbi_conn_error_handler(conn, NULL, NULL);
	free(prefix);
	if(mountName)
		free(mountName);
	if(pathStr)
		free(pathStr);
	if(urlStr)
		free(urlStr);
	if(path)
		free(path);
	if(hash)
		free(hash);
	return stat;
}

void GetItemMetaData(uint32_t UID, const char *url){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result;
	const char *Str;
	char *prefix = NULL;
	char *path;
	char *tmp;
	char *script;
	char Type[9];
	char buf[32];
	double Float;
	uint64_t verylong;
	unsigned char isAbsolutePath;
	uint32_t recID;
	int size;
	uint32_t idx;
	struct dbErr errRec;
	
	Type[0] = 0;
	
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL){
			goto error;
		}
	}		

	prefix = GetMetaData(0, "db_prefix", 0);
	errRec.message = "GetItemMetaData ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	

	// get db record id from URL
	if((tmp = str_NthField(url, "://", 1)) == NULL){
		goto error;
	}else{
		// ignore host portion of URL, if any
		if(path = strchr(tmp, '/'))
			path = uriDecode(path+1);
		else
			path = uriDecode(tmp);
		free(tmp);
	}		

	recID = atol(path);
	snprintf(buf, sizeof buf, "%u", (unsigned int)recID);
	free(path);
	if(recID){
		SetMetaData(UID, "ID", buf);
	}else{
		goto error;
	}
	
	// perform the sql query function
    result = dbi_conn_queryf(conn, "SELECT * FROM %stoc WHERE ID = %lu", prefix, recID);
	if(result == NULL)
		goto error;
	// get first record (should be the only record)
	if(!dbi_result_has_next_row(result)){
		dbi_result_free(result);
		goto error;
	}
	if(dbi_result_next_row(result)){ 
		Str = dbi_result_get_string(result, "Type");
		if(Str == NULL)
			Str = "";
		// make lowercase
		for(idx = 0; idx < strlen(Str); idx = idx + 1){
			if(idx >= 8)
				break;
			Type[idx] = tolower(Str[idx]);
		}
		Type[idx] = 0;
		SetMetaData(UID, "Type", Type);

		Str = dbi_result_get_string(result, "Name");
		if(Str == NULL)
			Str = "[Missing Name]";
		SetMetaData(UID, "Name", Str);
		
		Str = dbi_result_get_string(result, "Tag");
		if(Str)	
			SetMetaData(UID, "Tag", Str);
		
		Str = dbi_result_get_string(result, "Script");
		if(Str){
			//check if it's a file URI reference
			if(strncmp(Str, "file://", 7) == 0){
				if(tmp = str_NthField(Str, "://", 1)){
					// ignore host portion of URL, if any
					if(path = strchr(tmp, '/'))
						path = uriDecode(path);
					else
						path = uriDecode(tmp);
					free(tmp);
					tmp = getScriptFromFile(path, 0);
					free(path);
					// URL type escape (%nn) the string, except for " " chars.
					script = uriEncodeKeepSpace(tmp);
					free(tmp);
					SetMetaData(UID, "Script", script);
					free(script);
					Str = NULL;
				}
			}
		}
		if(Str){
			tmp = uriEncodeKeepSpace(Str);
			SetMetaData(UID, "Script", tmp);
			free(tmp);
		}
			
		Float = dbi_result_get_float(result, "Duration");
		snprintf(buf, sizeof buf, "%.2f", Float);
		SetMetaData(UID, "Duration", buf);
 
		verylong = dbi_result_get_longlong(result, "Added");
		snprintf(buf, sizeof buf, "%ld", verylong);
		SetMetaData(UID, "Added", buf);

	}else{
		dbi_result_free(result);
		goto error;
	}
	dbi_result_free(result);

	// get database fingerprint
	unsigned int fp = getFingerprint();
	if(fp){
		snprintf(buf, sizeof buf, "%d", fp);
		SetMetaData(UID, "Fingerprint", buf);
	}
	
	// perform the sql query function for custom properties
	result = dbi_conn_queryf(conn, "SELECT * FROM %smeta WHERE Parent = '%stoc' AND ID = %lu", prefix, prefix, recID);
	if(result){
		while(dbi_result_has_next_row(result)){
			if(dbi_result_next_row(result)){ 
				const char *prop, *value;
				prop = dbi_result_get_string(result, "Property");
				value = dbi_result_get_string(result, "Value");
				if(prop && strlen(prop) && value && strlen(value))
					SetMetaData(UID, prop, value);
			}else
				break;
		}
		dbi_result_free(result);		
	}
	dbi_conn_error_handler(conn, NULL, NULL);

	if(!strcmp(Type,"file")){
		// get additional meta data related to file references stored in the db
		GetdbFileMetaData(UID, recID, conn, GetMetaInt(0, "db_mark_missing", NULL));
		if(prefix)
			free(prefix);
		return;
	}
	if(!strcmp(Type,"task")){
		// get additional meta data related to task references stored in the db
		GetdbTaskMetaData(UID, recID, conn);
		if(prefix)
			free(prefix);
		return;
	}
	if(!strcmp(Type,"playlist")){
		// no addition meta data to get, and also, no error.
		if(prefix)
			free(prefix);
		return;
	}else{
		SetMetaData(UID, "Type", "");
	}
error:	
	if(conn && (conn == pthread_getspecific(gthread_dbi_conn)))
		dbi_conn_error_handler(conn, NULL, NULL);
	SetMetaData(UID, "Missing", "1");
	if(prefix)
		free(prefix);
}

uint32_t IDSearchMarkedFile(const char *path, const char *Hash){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result = NULL;
	uint32_t recID;
	char *prefix = NULL;
	char *encPath = NULL;
	struct dbErr errRec;
	
	recID = 0;
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			goto cleanup;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);	

	errRec.message = "IDSearchMarkedFile ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
		
	str_setstr(&encPath, path);
	dbi_conn_quote_string(conn, &encPath);

	// perform the sql query function
	result = dbi_conn_queryf(conn, "SELECT ID FROM %sfile WHERE Hash = '%s' AND SUBSTRING_INDEX(Path, '/', 1) = SUBSTRING_INDEX(%s, '/', 1)", prefix, Hash, encPath);
	free(encPath);
	if(result == NULL)
		goto cleanup;
	// get first record (**should** be the only record)
	if(!dbi_result_has_next_row(result)){
		dbi_result_free(result);
		goto cleanup;
	}
	if(dbi_result_next_row(result)){ 
		recID = dbi_result_get_uint(result, "ID");
	}
		
cleanup:
	if(conn)
		dbi_conn_error_handler(conn, NULL, NULL);
	if(prefix)
		free(prefix);
	return recID;
}

void dbFileSync(ctl_session *session, unsigned char silent){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result db_result;
	char *prefix;
	const char *URL, *Path;
	char *MSG, *tmp;
	char *Str = NULL;
	uint32_t count, missing, fixed, error;
	uint32_t recID;
	uint32_t localUID;
	char buf[4096]; // send data buffer 
	int sendCount;
	int tx_length;
	int	result;
	struct dbErr errRec;
	
	count = 0;
	missing = 0;
	fixed = 0;
	error = 0;
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL)	
			return;
	}		
	
	prefix = GetMetaData(0, "db_prefix", 0);

	errRec.message = "dbFileSync ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	// perform the sql query function
	db_result = dbi_conn_queryf(conn, "SELECT ID, URL, Path FROM %sfile ORDER BY URL", prefix);
	if(db_result){
		sendCount = 1;
		count = dbi_result_get_numrows(db_result);
		while(dbi_result_has_next_row(db_result) && (sendCount > 0)){ 
			if(dbi_result_next_row(db_result)){
				recID = dbi_result_get_uint(db_result, "ID");
				Path = dbi_result_get_string(db_result, "Path");
				URL = dbi_result_get_string(db_result, "URL");
				if(URL == NULL)
					URL = "[Missing URL]";
				result = -1;
				if(recID){
					// create an empty meta data record to hold results
					localUID = createMetaRecord("", NULL);
					// getting meta data with markMissing = true will update file info in the database
					result = GetdbFileMetaData(localUID, recID, conn, 1);
					// done with the metadata record... 
					releaseMetaRecord(localUID);
				}
				if(result == -2){
					MSG = "Missing";
					missing++;
				}
				if(result == -1){
					MSG = "Error";
					error++;
				}
				if(result == 0)
					MSG = "OK";
				if(result == 1){
					MSG = "Fixed";
					fixed++;
				}
			}else{
				Str = "[Database row error]";
				MSG = "Error";
				error++;
			}
			if(Path && strlen(Path)){
				str_setstr(&Str, Path);
			}else{
				str_setstr(&Str, "NOPATH, URL=");
				str_appendstr(&Str, URL);
			}
			tx_length = snprintf(buf, sizeof buf, "%u\t%s\t%s\n", (unsigned int)count, MSG, Str);
			sendCount = my_send(session, buf, tx_length, silent);
			count--;
		}
		count = dbi_result_get_numrows(db_result);
	}
	tx_length = snprintf(buf, sizeof buf, "\nChecked=%u\nMissing=%u\nError=%u\nFixed=%u\n", (unsigned int)count, (unsigned int)missing, (unsigned int)error, (unsigned int)fixed);
	my_send(session, buf, tx_length, silent);
	tx_length = snprintf(buf, sizeof buf, "Checked=%u, Missing=%u, Error=%u, Fixed=%u", (unsigned int)count, (unsigned int)missing, (unsigned int)error, (unsigned int)fixed);
	MSG = NULL;
	str_setstr(&MSG, "[database] dbSync-");
	str_appendstr(&MSG, (tmp = GetMetaData(0, "db_name", 0)));
	free(tmp);
	str_appendstr(&MSG, "': ");
	str_appendstr(&MSG, buf);
	serverLogMakeEntry(MSG);
	free(MSG);

	dbi_result_free(db_result);
	dbi_conn_error_handler(conn, NULL, NULL);
	free(prefix);
	if(Str)
		free(Str);
}

int dbHashSearchFileHeiarchy(ctl_session *session, unsigned char silent, const char *searchPath, dbi_conn conn, uint32_t mS_pace){
	char buf[4096]; // send data buffer 
	int tx_length;
	const char *pathArray[2] = {searchPath, 0};
	char *prefix = NULL;
	char *path;
	char *tmp, *enc;
	char *vol;
	char *hash;
	char *newurl = NULL;
	unsigned int recID;
	struct timespec timeout;
	FTS *fts_session;
	FTSENT *fts_entry;	
	dbi_result db_result, db_update;
	unsigned int localUID;
	char *newURL = NULL;
		
	vol = strdup(searchPath);
	if(tmp = getFilePrefixPoint(&vol)){
		// allocate, copy and encode the strings in the db's format
		str_setstr(&tmp, vol);
		free(vol);
		vol = str_NthField(tmp, "/", 0);
		free(tmp);
		dbi_conn_quote_string(conn, &vol);
		if(!strlen(vol)){
			tx_length = snprintf(buf, sizeof buf, " Path not in prefix list\n");
			my_send(session, buf, tx_length, silent);	
			free(vol);
			return 0;			
		}
	}else{
		tx_length = snprintf(buf, sizeof buf, " Path not in prefix list\n");
		my_send(session, buf, tx_length, silent);	
		free(vol);
		return 0;
	}
	
	timeout.tv_nsec= (mS_pace % 1000) * 1000 * 1000; // nS
	timeout.tv_sec= mS_pace / 1000;	// 0 seconds
	
	prefix = GetMetaData(0, "db_prefix", 0);
	
	if(fts_session = fts_open((char * const *)pathArray, FTS_NOCHDIR | FTS_XDEV, NULL)){
		tx_length = snprintf(buf, sizeof buf, " Searching...\n");
		my_send(session, buf, tx_length, silent);
		tmp = NULL;
		str_setstr(&tmp, "database] dbFileSearch-searching for moved files: ");
		str_appendstr(&tmp, searchPath);
		serverLogMakeEntry(tmp);
		free(tmp);
 
		while(fts_entry = fts_read(fts_session)){
			if(fts_entry->fts_name[0] == '.')
				continue;	// ignore . prefixed files
			tx_length = snprintf(buf, sizeof buf, "\t%s\n", fts_entry->fts_path);
			my_send(session, buf, tx_length, silent);
			
			if(fts_entry->fts_info == FTS_F){
				if(hash = GetFileHash(fts_entry->fts_path)){
					// perform the sql query function
					if(db_result = dbi_conn_queryf(conn, 
							"SELECT ID FROM %sfile WHERE Hash = '%s' AND SUBSTRING_INDEX(Path, '/', 1) = %s", prefix, hash, vol)){
						while(dbi_result_has_next_row(db_result)){ 
							if(dbi_result_next_row(db_result)){								
								recID = dbi_result_get_uint(db_result, "ID");
								tx_length = snprintf(buf, sizeof buf, "\t\t%u", recID);
								my_send(session, buf, tx_length, silent);
								// see if the record is for a missing file
								// create an empty meta data record to hold results
								localUID = createMetaRecord("", NULL);
								GetdbFileMetaData(localUID, recID, conn, 0);
								if(GetMetaInt(localUID, "Missing", NULL)){
									// update the URL, path, prefix and unmark as missing
									tmp = uriEncodeKeepSlash(fts_entry->fts_path);
									str_setstr(&newurl, "file://");
									str_appendstr(&newurl, tmp);
									free(tmp);
									dbi_conn_quote_string(conn, &newURL);
									path = NULL;
									str_setstr(&path, fts_entry->fts_path);
									if(tmp = getFilePrefixPoint(&path)){
										dbi_conn_quote_string(conn, &tmp);
										dbi_conn_quote_string(conn, &path);
										db_update = dbi_conn_queryf(conn, "UPDATE %sfile SET Missing = 0, URL = %s Path = %s, Prefix = %s WHERE ID = %lu", 
												prefix, newURL, path, tmp, recID);
										free(tmp);
										free(path);
										if(db_update){
											dbi_result_free(db_update);
											tx_length = snprintf(buf, sizeof buf, " Fixed\n");
											my_send(session, buf, tx_length, silent);
											tmp = NULL;
											str_setstr(&tmp, "[database] dbFileSearch-Found missing file: ");
											str_appendstr(&tmp, fts_entry->fts_path);
											serverLogMakeEntry(tmp);
											free(tmp);
										}
									}else{
										str_setstr(&tmp, "[database] dbFileSearch-Fix failed for found missing file: ");
										str_appendstr(&tmp, fts_entry->fts_path);
										serverLogMakeEntry(tmp);
										free(tmp);
									}
									free(newURL);
									newurl = NULL;
									
								}else{
									tx_length = snprintf(buf, sizeof buf, " OK\n");
									my_send(session, buf, tx_length, silent);	
								}
								// done with the metadata record... 
								releaseMetaRecord(localUID);
							}
						}
						dbi_result_free(db_result);
					}
					free(hash);
				}
			}
			
			// Wait for a time delay to check next
			nanosleep(&timeout, NULL);
		}
		free(vol);
		free(prefix);
		return 1;
	}
	tx_length = snprintf(buf, sizeof buf, " Invalid\n");
	my_send(session, buf, tx_length, silent);	
	free(vol);
	free(prefix);
	return 0;
}

void dbFileSearch(ctl_session *session, unsigned char silent, const char *Path, uint32_t pace){
	dbi_inst instance = NULL;
	dbi_conn conn = NULL;
	dbi_result result;
	char *prefix;
	char *tmp;
	char *path = NULL;
	char buf[4096]; // send data buffer
	int tx_length, i, p, listSize;
	char *prefixList;
	const char *mountName;
	struct dbErr errRec;
	glob_t globbuf;
	
	instance = get_thread_dbi(&conn);
	if(conn == NULL){
		// no connection yet for this thread... 
		if((conn = dbSetupConnection(instance, 1)) == NULL){
			return;
		}
	}		
		
	errRec.message = "dbFileSearch ";
	dbi_conn_error_handler(conn, HandleDBerror, (void *)(&errRec));	
	
	result = NULL;
	if((Path == NULL) || (strlen(Path) == 0)){	
		// no specific file path specified
		// get database default mount search path list
		prefix = GetMetaData(0, "db_prefix", 0);

		// get mount names
		result = dbi_conn_queryf(conn, "SELECT Distinct SUBSTRING_INDEX(Path, '/', 1) AS MountName FROM %sfile", prefix);
		free(prefix);
		if(result == NULL){
			return;
		}
		while(dbi_result_has_next_row(result)){ 
			if(dbi_result_next_row(result)){
				if((mountName = dbi_result_get_string(result, "MountName")) && (strlen(mountName) > 1)){
					// for each mount, pre-pend each of the search paths, and traverse all files
					// with in looking for files with Hash codes that match in the dtatabse and are missing.
					p = 0;
					i = 0;
					prefixList = GetMetaData(0, "file_prefixes", 0);
					listSize = str_CountFields(prefixList, ",") + 1;
					while(p < listSize){
						// Use glob function to find existing paths to the mount
						tmp = str_NthField(prefixList, ",", p);
						if(tmp){
							if(strlen(tmp)){
								str_setstr(&path, tmp);
								str_appendstr(&path, mountName);
								i = 0;
								if(!glob(path, GLOB_NOSORT, NULL, &globbuf)){
									while(i < globbuf.gl_pathc){
										// found a path in new glob list
										tx_length = snprintf(buf, sizeof buf, "Search Location %s: ", globbuf.gl_pathv[i]);
										my_send(session, buf, tx_length, silent);
										dbHashSearchFileHeiarchy(session, silent, globbuf.gl_pathv[i], conn, pace);
										i++;
									}
								}
								globfree(&globbuf);
							}
							free(tmp);
							p++;
						}else
							break;
					}
					free(prefixList);
				}
			}
		}
		if(result)
			dbi_result_free(result);
		if(path)
			free(path);
	}else{	
		// search path has been specified
		tx_length = snprintf(buf, sizeof buf, "Search Location %s: ", Path);
		my_send(session, buf, tx_length, silent);
		dbHashSearchFileHeiarchy(session, silent, Path, conn, pace);
	}
	dbi_conn_error_handler(conn, NULL, NULL);
}
