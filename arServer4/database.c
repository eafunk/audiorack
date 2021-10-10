/*
  Copyright (C) 2019-2020 Ethan Funk
  
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
#define _GNU_SOURCE		// needed for vasprintf() function use
#include <stdio.h>		// needed for vasprintf() function use

#include <stdarg.h>
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

unsigned int dbFPcache = 0;

/**************************************************************************
 * Thread Safe Database Abstraction Functions- only MySQL support for now *
 **************************************************************************/

pthread_key_t gthread_db_inst = 0;
pthread_once_t gthread_db_key_once = PTHREAD_ONCE_INIT;

volatile long instCnt = 0;

unsigned char db_preflight(void){
	// this MUST be called prior to any threads being created,
	// but after server logging has been configured.
	// returns 0 for no error.
	int result;
	
	if(result = mysql_library_init(0, NULL, NULL)){
		char *str = NULL;
		char *val;
		str_setstr(&str, "[mysqldb] Failed to initialize mysqlclient library - error ");
		val = istr(result);
		str_appendstr(&str, val);
		serverLogMakeEntry(str);
		free(val);
		free(str);
		return 1;
	}
	return 0;
}

void db_shutdown(void){
	// call at the end of main function, just befor shutting down, to clean up
	mysql_library_end();
}

void db_result_free(dbInstance *db){
	if(db){
		switch(db->type){
			case dbtype_mysql:
				if(db->result){
					db->fields = NULL;
					db->row = NULL;
					db->num_fields = 0;
					mysql_free_result((MYSQL_RES *)db->result);
					db->result = NULL;
				}
				break;
		}
	}
}

void db_instance_free(void *value){
	dbInstance *inst = (dbInstance*)value;
	
	if(value){
		switch(inst->type){
			case dbtype_mysql:
				db_result_free(inst);
				if(inst->instance){
					mysql_close((MYSQL*)inst->instance);
				}
				mysql_thread_end();
				break;
		}
		free(value);
	}
	pthread_setspecific(gthread_db_inst, NULL);
}

void db_make_threadspecific(void){
	pthread_key_create(&gthread_db_inst, db_instance_free);
	pthread_setspecific(gthread_db_inst, NULL);
}

unsigned char db_get_thread_instance(dbInstance **inst){
	// sets inst to a new or existing instance
	// returns true if the instance is connected (verified with ping).
	dbInstance *db;
	
	// create thread keys for a per-thread db instance and connections if they don't yet exist
	pthread_once(&gthread_db_key_once, db_make_threadspecific);
	
	// check to see if this thread has it's own dbi instance yet
	if((db = pthread_getspecific(gthread_db_inst)) == NULL) {
		// create instance
		db = calloc(1, sizeof(dbInstance));
		pthread_setspecific(gthread_db_inst, db);
	}
	if(*inst = db){
		// check connection status according to db type
		switch(db->type){
			case dbtype_mysql:
				if(!mysql_ping((MYSQL*)db->instance)){
					// Connected
					return 1;
				}
		}
	}
	// Not connected
	return 0;
}

static inline void db_set_errtag(dbInstance *db, const char *tag){
	if(db){
		db->errRec.tag = tag;
		db->errRec.flag = 0;
	}
}

void HandleDBerror(dbInstance *db){
	char *str = NULL;
	char *tmp;
	unsigned int err;

	if(db){
		switch(db->type){
			case dbtype_mysql:
				// MySQL type handling 
				if(err = mysql_errno((MYSQL *)db->instance)){
					db->errRec.flag = 1;
					
					switch(db->type){
						case dbtype_mysql:
							str_setstr(&str, "[mysqldb] ");
							break;
						case dbtype_postgresql:
							str_setstr(&str, "[postgresqldb] ");
							break;
						default:
							str_setstr(&str, "[uninitdb] ");
							break;
					}
					if(db->errRec.tag)
						str_appendstr(&str, db->errRec.tag);
					str_appendstr(&str, "-");
					tmp = ustr(err);
					str_appendstr(&str, tmp);
					free(tmp);
					str_appendstr(&str, ":");
					str_appendstr(&str, mysql_error((MYSQL *)db->instance));
					serverLogMakeEntry(str);
					free(str);
				}
				break;
		}
	}
}

unsigned char db_connection_setup(dbInstance *db, char useNamedDB){
	// returns true if connection was sucessful
	char *tmp;
	short port;
	char isValid;
	
	if(!db)
		return 0;
	db_set_errtag(db, "db_connection_setup");
	tmp = GetMetaData(0, "db_type", 0);
	if(db->type == dbtype_none){
			// brand new, uninitilized instance
			if(!strcmp(tmp, "mysql")){
				// setup for mysql type
				db->type = dbtype_mysql;
				db->instance = (void*)mysql_init(NULL);
			}
	}
	// note: tmp contains the db_type value string 
	switch(db->type){
		case dbtype_mysql:
			if(strcmp(tmp, "mysql")){
				// type mismatch with already initialized instance
				serverLogMakeEntry("[mysqldb] db_connection_setup-trying to change instance to a different type of database.");
				free(tmp);
				db_set_errtag(db, NULL);
				return 0;
			}
			// get properties from arServer settings
			char *host = GetMetaData(0, "db_server", 1);
			int port = GetMetaInt(0, "db_port", &isValid);
			char *user = GetMetaData(0, "db_user", 1);
			char *pw = GetMetaData(0, "db_pw", 1);
			char *name = NULL;
			if(useNamedDB)
				name = GetMetaData(0, "db_name", 1);
			// and try to connect
			MYSQL *res = mysql_real_connect((MYSQL*)db->instance, host, user, pw, name, port, NULL, 0);
			if(host)
				free(host);
			if(user)
				free(user);
			if(pw)
				free(pw);
			if(name)
				free(name);
			if(res){
				// connection sucess
				db->result = NULL;
				db->row = NULL;
				db->fields = NULL;
				db->num_fields = 0;
				db_set_errtag(db, NULL);
				free(tmp);
				return 1;
			}else{
				HandleDBerror(db);
			}
			break;
	}
	free(tmp);
	// if we get here, connection has failed
	db_set_errtag(db, NULL);
	return 0;
}

dbInstance *db_get_and_connect(void){
	dbInstance *instance = NULL;
	// handy function that aquires (or creates) a db instance for this thread and sets up the connection.
	// returns the instance pointer if all is well and connected, otherwise returns NULL
	if(db_get_thread_instance(&instance))
		return instance;
	else{
		// no connection yet for this thread... 
		if(db_connection_setup(instance, 1))
			return instance;
	}
	return NULL;
}

char db_quote_string(dbInstance *db, char **str){
	// returns 0 when quoating was sucessful, and string will have a new memory location
	unsigned long size, length;
	char *newstr;
	switch(db->type){
		case dbtype_mysql:
			size = 0;
			length = strlen(*str);
			newstr = malloc(length * 2 + 3);
			newstr[0] = '\'';
			if(length)
				size = mysql_real_escape_string((MYSQL *)db->instance, newstr+1, *str, length);
			newstr[++size] =  '\'';
			newstr[++size] = 0;
			free(*str);
			*str = newstr;
			return 0;
	}
	return 1;
}

unsigned char db_use_database(dbInstance *db, const char *name){
	switch(db->type){
		case dbtype_mysql:
			if(!mysql_select_db((MYSQL*)db->instance, name))
				// All is well
				return 0;
			break;
	}
	// invalid db type, or failed to select the database named
	return 1;
}

void *db_result_detach(dbInstance *db){
	// If a result is returned it will need to be reattached to be freed.
	// The result will no longer be available until re-attached
	void *res;
	res = db->result;
	db->result = NULL;
	db->fields = NULL;
	db->row = NULL;
	db->num_fields = 0;
	return res;
}

void db_result_attach(dbInstance *db, void *res){
	// An existing reult will be freed befor reattaching the passed reult
	if(db->result)
		db_result_free(db);
	if(db->result = res){
		switch(db->type){
			case dbtype_mysql:
				// if there is a non-null result, get associated field information
				db->num_fields = mysql_num_fields((MYSQL_RES *)res);
				db->fields = (void*)mysql_fetch_fields((MYSQL_RES *)res);
				break;
		}
	}
}

unsigned char db_query(dbInstance *db, const char *querryStr){
	// returns 0 if no error.
	switch(db->type){
		case dbtype_mysql:

			if(mysql_real_query((MYSQL*)db->instance, querryStr, strlen(querryStr))){
				HandleDBerror(db);
				return 1;
			}else{
				if(db->result)
					db_result_free(db);
				db->result = (void *)mysql_store_result((MYSQL*)db->instance);
				if(db->result){
					// if there is a result, get associated field information
					db->num_fields = mysql_num_fields((MYSQL_RES *)db->result);
					db->fields = (void*)mysql_fetch_fields((MYSQL_RES *)db->result);
				}
				return 0;
			}
			break;
	}
	return 1;
}

unsigned char db_queryf(dbInstance *db, const char *querryStr, ...){
	// returns 0 if no error.
	char *statement;
	va_list ap;
	unsigned char ret;

	va_start(ap, querryStr);
	vasprintf(&statement, querryStr, ap);
	va_end(ap);
	ret = db_query(db, statement);
	free(statement);
	
	return ret;
}

unsigned long long db_result_get_result_rows(dbInstance *db){
	switch(db->type){
		case dbtype_mysql:
			if(db->result)
				// rows in result
				return mysql_num_rows((MYSQL_RES *)db->result);
	}
	return 0;
}

unsigned long long db_result_get_rows_affected(dbInstance *db){
	switch(db->type){
		case dbtype_mysql:
			if(db->result)
				// rows in result
				return mysql_num_rows((MYSQL_RES *)db->result);
			else
				// no result, row inserted, updated, etc.
				return mysql_affected_rows((MYSQL*)db->instance);
	}
	return 0;
}

unsigned char db_result_next_row(dbInstance *db){
	// Call to select the first or next row of a query result.
	// Returns true if the next row was selected, 0 if no more rows in result
	switch(db->type){
		case dbtype_mysql:
			if(db->result){
				if(db->row = (void*)mysql_fetch_row((MYSQL_RES *)db->result))
					return 1;
			}
			break;
	}
	
	return 0;
}

unsigned char db_result_select_row(dbInstance *db, unsigned long long index){
	// Returns true if the next row was selected, 0 if index is invalid, or other error
	switch(db->type){
		case dbtype_mysql:
			if(db->result){
				mysql_data_seek((MYSQL_RES *)db->result, index);
				return db_result_next_row(db);
			}
			break;
	}
	return 0;
}

const char *db_result_get_field_by_index(dbInstance *db, unsigned long long index, int *type){
	// assumes a valid result from the last querry, and db_result_next_row()
	// has been called sucessfully (with a true result code) to selected a result row.
	// The return string will be null if there was an error, or the field is empty.
	// Returned string are valid only durring the lifetime of the current row and result.
	// db_result_next_row(), db_result_free(), or db_query() will invalidate the returned string.
	switch(db->type){
		case dbtype_mysql:
			if(db->result && db->row){
				if(index < db->num_fields){
					char **rowdata = (char**)db->row;
					MYSQL_FIELD *fields = (MYSQL_FIELD*)db->fields;
					if(type)
						*type = fields[index].type;
					return rowdata[index];
				}
			}
			break;
	}
	if(type)
		*type = -1;
	return NULL;
}

const char *db_result_get_field_by_name(dbInstance *db, const char *name, int *type){
	// assumes a valid result from the last querry, and db_result_next_row()
	// has been called sucessfully (with a true result code) to selected a result row.
	// The return string will be null if there was an error, or the field is empty.
	// Returned string are valid only durring the lifetime of the current row and result.
	// db_result_next_row(), db_result_free(), or db_query() will invalidate the returned string.
	switch(db->type){
		case dbtype_mysql:
			if(db->result && db->row){
				unsigned int idx = 0;
				char **rowdata = (char**)db->row;
				MYSQL_FIELD *fields = (MYSQL_FIELD*)db->fields;
				while(idx < db->num_fields){
					if(!strcmp(fields[idx].name, name)){
						if(type)
							*type = fields[idx].type;
						return rowdata[idx];
					}
					idx++;
				}
			}
			break;
	}
	if(type)
		*type = -1;
	return NULL;
}
/*************************************
 * end of database abstraction code 
 *************************************/

void DumpDBDriverList(ctl_session *session, char *buf, size_t size){
	const char *version;
	int tx_length;
	
	version = mysql_get_client_info();
	tx_length = snprintf(buf, size, "%s\t%s\n", "mysql", version);
	my_send(session, buf, tx_length, 0, 0);
	
}

unsigned char MakeLogEntry(ProgramLogRecord *rec){
	dbInstance *instance = NULL;
	char *Name = NULL;
	char *Artist = NULL;
	char *Album = NULL;
	char *Comment = NULL;
	char *Source = NULL;
	char *Owner = NULL;
	char *prefix = NULL;
	const char *tmp;
	unsigned char ret_val = 0;
	
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
		
	db_set_errtag(instance, "MakeLogEntry");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// make a new entry:
	// allocate, copy and encode the strings in the db's format
	Name = strdup(rec->name);
	db_quote_string(instance, &Name);
	
	Artist = strdup(rec->artist);
	db_quote_string(instance, &Artist);
	
	Album = strdup(rec->album);
	db_quote_string(instance, &Album);
	
	Source = strdup(rec->source);
	db_quote_string(instance, &Source);
	
	Comment = strdup(rec->comment);
	db_quote_string(instance, &Comment);

	Owner = strdup(rec->owner);
	db_quote_string(instance, &Owner);
			
	if(rec->logID && rec->played){
		if(!db_queryf(instance, "UPDATE %slogs SET Item = %lu, Time = %ld, Name = %s, Artist = %s, Album = %s, "
						"Added = %u, ArtistID = %lu, AlbumID = %lu, OwnerID = %lu, Comment = %s, Source = %s, Owner = %s WHERE ID = %lu", 
						prefix, (unsigned long)rec->ID, (long)rec->when, Name, Artist, Album, (unsigned int)rec->added, (unsigned long)rec->artistID, 
						(unsigned long)rec->albumID, (unsigned long)rec->ownerID, Comment, Source, Owner, (unsigned long)rec->logID)){
			if(db_result_get_rows_affected(instance)){
				ret_val = 1;
				goto cleanup;
			}
		}
	}
	
	// perform the sql insert function
	if(!db_queryf(instance, "INSERT INTO %slogs (Item, Location, Time, Name, Artist, Album, Added, ArtistID, AlbumID, OwnerID, "
				"Comment, Source, Owner) VALUES (%lu, %lu, %ld, %s, %s, %s, %u, %lu, %lu, %lu, %s, %s, %s)", 
				prefix, (unsigned long)rec->ID, (unsigned long)rec->location, (long)rec->when, Name, Artist, Album, (unsigned int)rec->added, 
				(unsigned long)rec->artistID, (unsigned long)rec->albumID, (unsigned long)rec->ownerID, Comment, Source, Owner)){
		ret_val = 1;
		// Get new log ID, if any
		if(rec->UID){
			if(!db_queryf(instance, "SELECT ID FROM %slogs WHERE Item = %lu AND Location = %lu AND Time = %ld AND Source = %s",
					prefix, (unsigned long)rec->ID, (unsigned long)rec->location, (long)rec->when, Source)){
				if(db_result_next_row(instance)){
					if(tmp = db_result_get_field_by_name(instance, "ID", NULL)){ 
						SetMetaData(rec->UID, "logID", tmp);
						rec->logID = atoll(tmp);
					}
				}
			}
		}
	}
	
cleanup:
	// clean up
	db_result_free(instance);
	db_set_errtag(instance, NULL);
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
	dbInstance *instance = NULL;
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
	
	if(recID = GetMetaInt(uid, "logID", NULL)){
		instance = db_get_and_connect();
		if(!instance)
			goto cleanup;
		db_set_errtag(instance, "updateLogMeta");
		prefix = GetMetaData(0, "db_prefix", 0);
		
		// make a new entry:
		Name = GetMetaData(uid, "Name", 0);
		Artist = GetMetaData(uid, "Artist", 0);
		Album = GetMetaData(uid, "Album", 0);
		Source = GetMetaData(uid, "URL", 0);
		Comment = GetMetaData(uid, "Tag", 0);
		Owner = GetMetaData(uid, "Owner", 0);

		// encode the strings in the db's format

		db_quote_string(instance, &Name);
		db_quote_string(instance, &Artist);
		db_quote_string(instance, &Album);
		db_quote_string(instance, &Source);
		db_quote_string(instance, &Comment);
		db_quote_string(instance, &Owner);
		
		// modify  entry
		if(!db_queryf(instance, "UPDATE %slogs SET Name = %s, Artist = %s, "
					"Album = %s, Comment = %s, Source = %s, Owner = %s WHERE ID = %lu AND (Added & 1) = 1", 
					prefix, Name, Artist, Album, Comment, Source, Owner, (unsigned long)recID))
			ret_val = 1;
	}

cleanup:
	if(instance){
		db_result_free(instance);
		db_set_errtag(instance, NULL);
	}
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
	dbInstance *instance = NULL;
	char *prefix = NULL;
	taskRecord *parent = (taskRecord *)inRef;
	
	if((logID = (uint32_t *)(parent->userData)) == 0)
		return;
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "DeleteLogEntry");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	db_queryf(instance, "DELETE FROM %slogs WHERE ID = %lu AND (Added & 1) = 1", prefix, *logID);

cleanup:
	if(prefix)
		free(prefix);
	db_set_errtag(instance, NULL);
	db_result_free(instance);
}

short dbPLGetNextMeta(uint32_t index, uint32_t ID, uint32_t UID){
	dbInstance *instance = NULL;
	short result;
	const char *valStr;
	const char *propStr;
	char *prefix = NULL;
	
	result = -1;
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "dbPLGetNextMeta");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// perform the sql query function
	if(db_queryf(instance, "SELECT * FROM %splaylist WHERE ID = %lu AND Position = %lu", prefix, ID, index))
		goto cleanup;

	while(db_result_next_row(instance)){
		if(valStr = db_result_get_field_by_name(instance, "Value", NULL)){
			if(propStr = db_result_get_field_by_name(instance, "Property", NULL)){
				SetMetaData(UID, propStr, valStr);
				result = 0;
			}
		}
	}
	
cleanup:
	db_set_errtag(instance, NULL);
	db_result_free(instance);
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

			value = GetMetaData(UID, "Path", 0);
			if(!strlen(value)){
				free(value);
				value = GetMetaData(UID, "Folder", 0);
			}
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
	dbInstance *instance = NULL;
	const char *Str;
	char *prefix = NULL;
	char *result;
	char *propCpy = NULL;
	
	result = strdup("");
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "dbGetInfo");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	propCpy = strdup(property);
	db_quote_string(instance, &propCpy);
	
	// perform the sql query function
	if(db_queryf(instance, "SELECT Value FROM %sinfo WHERE Property = %s", prefix, propCpy))
		goto cleanup;
	// get first record (should be the only record)
	if(db_result_next_row(instance)){ 
		if(Str = db_result_get_field_by_name(instance, "Value", NULL))
			str_setstr(&result, Str);
	}

cleanup:
	if(prefix)
		free(prefix);
	if(propCpy)
		free(propCpy);
	db_result_free(instance);
	db_set_errtag(instance, NULL);
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

unsigned char db_initialize(dbInstance *db){
	dbInstance *newinst;
	char *dbName;
	char *sqlstr = NULL;
	char *typeStr;
	char *versionStr = NULL;
	char *ini_file_path = NULL;
	char *tmp = NULL;
	FILE *fp;
	char line[4096];
	
	newinst = NULL; // used as a flag to clean up new db at the end ofthe  first call of the call recursive chain
	fp = NULL;
	typeStr = GetMetaData(0, "db_type", 0);
	dbName = GetMetaData(0, "db_name", 0);
	if(!strlen(typeStr) || !strlen(dbName))
		goto cleanup;

	if(!db){
		/* set up a db instance and connection, independent of the thread associated db instance,
		 * so we can start the connection off unassociated with a particular named database.  This
		 * is required so we can connect, and THEN see if the named database exists, creating it if 
		 * needed.  If we start off connecing with the named database, and the database doesn't exist
		 * yet, the entire connection will fail. */
		db = calloc(1, sizeof(dbInstance));
		db_set_errtag(db, "dbInitialize");
		newinst = db;  // this sets the flag that we are the first call of the call recursive chain
		if(!db_connection_setup(db, 0))
			goto cleanup;
	}
	
	if(!db_use_database(db, dbName))
		versionStr = dbGetInfo("Version");
	if(versionStr && strlen(versionStr)){
		// this is an upgrade to an existing database
		str_setstr(&ini_file_path, AppSupportDirectory);
		str_appendstr(&ini_file_path, typeStr);
		str_appendstr(&ini_file_path, versionStr);
		str_appendstr(&ini_file_path, ".dbi");
	}else{
		// create new database if one with the given name doesn't already exist
		str_setstr(&ini_file_path, AppSupportDirectory);
		str_appendstr(&ini_file_path, typeStr);
		str_appendstr(&ini_file_path, ".dbi");
		str_setstr(&sqlstr, "CREATE DATABASE IF NOT EXISTS ");
		str_appendstr(&sqlstr, dbName);
		
		if(db_query(db, sqlstr))
			goto cleanup;
		if(db_use_database(db, dbName))
			goto cleanup;
	}
	
	if((fp = fopen(ini_file_path, "r")) == NULL){
		if(!versionStr || !strlen(versionStr)){
			str_setstr(&tmp, "[database] dbInitialize- template file '");
			str_appendstr(&tmp, ini_file_path);
			str_appendstr(&tmp, "': Could not open file for reading");
			serverLogMakeEntry(tmp);
			free(tmp); 
		}else
			db_set_errtag(db, "dbInitialize");
		goto cleanup;
	}
	
	db_set_errtag(db, "dbInitialize");
	while(fgets(line, sizeof line, fp) != NULL){
		// each line in the .dbi file is an sql command to execute
		str_setstr(&sqlstr, line);
		dbMacroReplace(&sqlstr);
		if(!db_query(db, sqlstr))
			db_result_free(db);
	}
	if(versionStr && strlen(versionStr))
		// re-enter for another go-around so we upgrade
		// all they way to the latest version.
		db_initialize(db);
	
cleanup:
	free(dbName);
	free(typeStr);
	if(sqlstr)
		free(sqlstr);
	if(ini_file_path)
		free(ini_file_path);
	if(fp)
		fclose(fp);
	if(!db->errRec.flag){
		versionStr = dbGetInfo("Version");
		str_setstr(&tmp, " [database] dbInitialize-");
		str_appendstr(&tmp, dbName);
		str_appendstr(&tmp, ": initialized/updated to version ");
		str_appendstr(&tmp, versionStr);
		serverLogMakeEntry(tmp);
		free(dbName);
		free(versionStr);
		if(newinst)
			db_instance_free(newinst);
		return 1;
	}
	if(versionStr)
		free(versionStr);
	if(dbName)
		free(dbName);
	if(newinst)
		db_instance_free(newinst);
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
	void *result;
	void *lastResult = NULL;
	dbInstance *instance = NULL;
	unsigned int *id_array;
	unsigned int i;
	unsigned long field;
	unsigned long long Item;
	unsigned long long count;
	long row;
	unsigned char last;
	uint32_t newUID;
	int size;
	char *mode = NULL;
	char *query;
	char *qStr;
	char *tmp;
	const char *sval;
	char *name;
	char *single;
	double targetTime;

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

	instance = db_get_and_connect();
	if(!instance){
		free(qStr);
		return;
	}
	db_set_errtag(instance, "dbPick");

	field = 0;
	// parse query string for multiple queries, separated with ';' char
	single = str_NthField(qStr, ";", field);
	db_result_free(instance);
	while(!parent->cancelThread && single && strlen(single) && str_firstnonspace(single)){
		// perform the sql query function
		// replace any special function macros
		str_strip_lfcr(single); // replace LF and CR with spaces
		dbMacroReplace(&single);
		if(!db_query(instance, single)){
			count = db_result_get_result_rows(instance);
			if(count > 0){
				if(result = db_result_detach(instance)){
					if(lastResult){
						db_result_attach(instance, lastResult);
						db_result_free(instance);
					}
					lastResult = result;
				}
			}
		}
		field++;
		free(single);
		single = str_NthField(qStr, ";", field);
	}
	free(qStr);
	if(single)
		free(single);
	if(lastResult){
		db_result_attach(instance, lastResult);
		if(!parent->cancelThread){
			mode = GetMetaData(parent->UID, "Mode", 0);
			count = db_result_get_result_rows(instance);
			row = -1;
			if(!strcmp(mode,"random"))
				row = (unsigned long)((count-1) * RandomNumber());
			else if(!strcmp(mode,"weighted"))
				row = (unsigned long)((count-1) * GaussianNumber());
			else if(!strcmp(mode,"first"))
				row = 0;
				
			if(row >= 0){
				// a single row selected
				if(db_result_select_row(instance, row)){
					Item = 0;
					if(sval = db_result_get_field_by_name(instance, "ID", NULL)){
						Item = atoll(sval);
						if(Item){
							tmp = ustr(Item);
							str_insertstr(&tmp, "item:///", 0);
							newUID = SplitItem(parent->UID, tmp, 1);
							if(newUID == 0){
								free(tmp);
								tmp = ustr(Item);
								str_insertstr(&tmp, ": split failed on item:///", 0);
								str_insertstr(&tmp, (name = GetMetaData(parent->UID, "Name", 0)), 0);
								free(name);
								str_insertstr(&tmp, "[database] dbPick-", 0);
								serverLogMakeEntry(tmp);
							}
							free(tmp);
						}
					}
				}
			}else if(!strcmp(mode,"all")){ // no single row selected
				// get target-time of parent for inheritance by items, and increment by each previous item's duration.
				targetTime = GetMetaFloat(parent->UID, "TargetTime", NULL);
				
				// allocate id_array
				if(id_array = (unsigned int *)calloc(count, sizeof(unsigned int))){
					// fill id_array
					i=0;
					while(db_result_next_row(instance)){ 
						if(sval = db_result_get_field_by_name(instance, "ID", NULL)){
							id_array[i] = atoll(sval);
							if(id_array[i])
								i++;
						}
					}
					count = i;
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
			str_appendstr(&tmp, ": timeout.");
			serverLogMakeEntry(tmp);
			free(tmp);
		}
	}else{
		tmp = GetMetaData(parent->UID, "Name", 0);
		str_insertstr(&tmp, "[database] dbPick-", 0);
		str_appendstr(&tmp, ": No result.");
		serverLogMakeEntry(tmp);
		free(tmp);
	}
	// all done... clean up!
	db_result_free(instance);
	db_set_errtag(instance, NULL);
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
	localUID = createMetaRecord(*urlStr, NULL, 1);
	
	// fill the metadata record
	GetFileMetaData(localUID, *urlStr);
	if(GetMetaInt(localUID, "Missing", NULL)){
		// not a file we can read
		str_setstr(urlStr, "");
	}
	releaseMetaRecord(localUID);
}

int scandirSelect(const struct dirent *ent){
	// eliminate all entries that are not regular files
	if(ent->d_type == DT_REG)
		return 1;
	else
		return 0;
}

void folderPickCleanUp(void *pass){
	int index;
	struct locals{
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

char *folderPathLikeStringEncode(char **Str, char prefixWildcard, char matchToEnd, dbInstance *instance){
	// Str is assumed to already be URL encoded with the path or partial path.
	// returns notStr (must be freed) if matchToEnd is true, otherwise returns NULL
	char *notStr = NULL;
	str_ReplaceAll(Str, "\\", "\\\\");	// escape '\'
	str_ReplaceAll(Str, "%", "\\%");		// escape '%'
	str_ReplaceAll(Str, "_", "\\_");		// escape '_'
	if(prefixWildcard)
		str_insertstr(Str, "file://%", 0);				// prepend 'file://%' to match any file prefix
	else
		str_insertstr(Str, "file://", 0);				// prepend 'file://' to match full path
		
	if(!matchToEnd){
		str_appendstr(Str, "%");					// appaned '%' to match any suffix
		// also generate notMatchStr
		str_setstr(&notStr, *Str);
		str_appendstr(&notStr, "/%");			// appaned '/%' to NOT match any suffix with a / in it
		db_quote_string(instance, &notStr);
	}
	
	db_quote_string(instance, Str);
	return notStr;
}

char *traverseFolderListing(char **dir, char *pre, uint32_t modified,  
				unsigned short none, unsigned short seq, unsigned short rerun,  
					unsigned short first, uint32_t randlim, unsigned short date){
	struct stat statRec;
	dbInstance *instance = NULL;
	struct locals{
		struct dirent **entList;
		int count;
	} locBlock;
	int index, i, p, c;
	int remove;
	time_t cutoff, last;
	char *include = NULL;
	char *result = NULL;
	char *path = NULL; 
	char *ppre = NULL;
	char *prefix = NULL;
	char *encStr = NULL;
	char *sub_name = NULL;
	char *adjPathStr = NULL;
	char *likeStr = NULL;
	char *notLikeStr = NULL;
	char *tmp;
	const char *sval;
	const char *name; 
	struct dbErr errRec;
	
	str_setstr(&result, "");
	instance = NULL;
	locBlock.count = -1;
	pthread_cleanup_push((void (*)(void *))folderPickCleanUp, (void *)&locBlock);
	tmp = *dir;
	index = strlen(tmp);
	if(index < 1)
		goto cleanup;
	if(tmp[index-1] == directoryToken)  // make sure there is NOT a trailing slash in the path name. 
		tmp[index-1] = 0;
	
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	prefix = GetMetaData(0, "db_prefix", 0);
	include = GetMetaData(0, "db_include_loc", 0);
	
	if(strlen(include) > 0){
		str_insertstr(&include, ",", 0);
		str_insertstr(&include, (tmp = GetMetaData(0, "db_loc", 0)), 0);
		free(tmp);
	}else
		include = GetMetaData(0, "db_loc", 0);
	
	/* handle relative paths when there is a prefix specified, otherwise,
	 * dir is taken to be an absolute path, and should have a leading slash. */
	if(strlen(pre)){
		/* escaping all *,?,[ chars found in pathStr to prevent paths
		 * with these chars from being interpereted by the glob function
		 * below as wild-card matches.*/
		char *dirStr = NULL;
		char *prefixList;
		uint32_t listSize;
		unsigned char missing;
		struct stat path_stat;
		glob_t globbuf;
		
		prefixList = GetMetaData(0, "file_prefixes", 0);
		listSize = str_CountFields(prefixList, ",") + 1;
	
		str_setstr(&dirStr, *dir);
		str_setstr(&path, pre); // path = pre + dir strings
		str_appendstr(&path, *dir);
		str_ReplaceAll(&dirStr, "*", "\\*");
		str_ReplaceAll(&dirStr, "?", "\\?");
		str_ReplaceAll(&dirStr, "[", "\\[");
		
		// Copy the string and change the case of the first letter, if it's a letter
		// for an alternate comparison on systems that change the case of the mount
		str_setstr(&adjPathStr, dirStr);
		if(isupper(*adjPathStr))
			*adjPathStr = tolower(*adjPathStr);
		else if(islower(*adjPathStr))
			*adjPathStr = toupper(*adjPathStr);
		else{
			// not case changable
			free(adjPathStr);
			adjPathStr = NULL;
		}
		// path is Full Path. dirStr is the path sufix, without leading slash.
		// path = /longer/path/to/mount/some/dir,
		// dirStr = mount/some/dir
		globbuf.gl_offs = 0;
		globbuf.gl_pathc = 0;
		i = 0;
		p = 0;
		c = 0;
		missing = 1;
		do{
			/* see if path points to a valid directory */
			if(!stat(path, &path_stat) && S_ISDIR(path_stat.st_mode)){
				// yes! We are done.
				missing = 0;
				break;
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
					if(tmp && strlen(dirStr)){
						str_setstr(&path, tmp);
						if(adjPathStr && c){
							// trying the pathStr version with case adjustment
							str_appendstr(&path, adjPathStr);
							c = 0; // go back to original case next time
						}else{
							str_appendstr(&path, dirStr);
							if(adjPathStr)
								c = 1; // try case change next time
						}
						i = 0;
						if(globbuf.gl_pathc){
							globfree(&globbuf);
							globbuf.gl_pathc = 0;
						}
						if(!glob(path, (GLOB_NOSORT | GLOB_ONLYDIR), NULL, &globbuf)){
							if(globbuf.gl_pathc){
								// found a path in new glob list
								str_setstr(&path, globbuf.gl_pathv[0]);
								i++;
								if(!c)
									p++;
								free(tmp);
								break;
							}
						}
						free(tmp);
						if(!c)
							p++;
					}else{
						// no more prefixes
						p = listSize+1;
						if(tmp)
							free(tmp);
					}
				}
			}while(p < listSize);
			
		}while(missing && (p <= listSize));
		if(globbuf.gl_pathc)
			globfree(&globbuf);
		free(prefixList);
		free(dirStr);
		if(missing){
			// failed to find a directory 
			free(path);
			goto cleanup;
		}else{
			// found a directory
			if(*dir)
				free(*dir);
			*dir = path;
			str_appendstr(dir, directoryTokenStr);	// add trailing slash back in
			path = NULL;
		}
	}

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
			// If modified time is set, purge list of itmes older than modified time
			if(modified)
				cutoff = time(NULL) - (modified * 3600); // convert to now - hours in seconds
			// And always remove . files (hidden files) from the list
			index = 0;
			while(index < locBlock.count){
				remove = 0;
				if(locBlock.entList[index]->d_name[0] == '.')
					// hidden file
					remove = 1;
				else if(modified){
					str_setstr(&path, *dir);
					str_appendstr(&path, locBlock.entList[index]->d_name);
					remove = stat(path, &statRec);
					if(remove == 0){
						if(statRec.st_mtime < cutoff){
							// it's too old, delete it from the list
							remove = 1;
						}
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
			
			if(locBlock.count > 0){
				path = strdup(*dir);
				if((ppre = getFilePrefixPoint(&path)) && (strlen(path)>1)){
					// search for source matches ignoring the prefix, matching the remaining path only
					likeStr = uriEncodeKeepSlash(path+1);
					str_setstr(&path, *(dir+1));
					// We really want to match upper or lower case on the first char, but this approach above
					// of ignoring the first char is easier for the database to do and will work most of the time.
					// encode likeStr and create notLikeStr
					notLikeStr = folderPathLikeStringEncode(&likeStr, 1, 0, instance);	// prefix wild card, no match to end
				}else{
					// search for source matches using the full path
					likeStr = uriEncodeKeepSlash(*dir);
					notLikeStr = folderPathLikeStringEncode(&likeStr, 0, 0, instance);	// no prefix wild card, no match to end
				}
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

				if(rerun){		// re-run last played if true (non-zero)
					db_set_errtag(instance, "traverseFolderListingRerun");
					// perform the sql query function: Get last ADDED file name in this directory (back eight hours)
					name = "";
					if(!db_queryf(instance, "SELECT Time, Source AS Name FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
									"AND (Added & 1) = 0 AND Time > UNIX_TIMESTAMP(NOW()) - 28800 AND Source LIKE %s AND Source NOT LIKE %s "
									"ORDER BY Time DESC LIMIT 1", prefix, prefix, include, likeStr, notLikeStr)){
						// get first record (should be the only record)
						if(db_result_next_row(instance)){
							name = db_result_get_field_by_name(instance, "Name", NULL);
							if(name == NULL)
								name = "";
						}
					}
					if(strlen(name) == 0){
						// perform the sql query function: Get last PLAYED file name in this directory
						if(!db_queryf(instance, "SELECT Time, Source AS Name FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
										"AND (Added & 1) <> 1 AND Source LIKE %s AND Source NOT LIKE %s "
										"ORDER BY Time DESC LIMIT 1", prefix, prefix, include, likeStr, notLikeStr)){
							// get first record (should be the only record)
							if(db_result_next_row(instance)){
								name = db_result_get_field_by_name(instance, "Name", NULL);
								if(name == NULL)
									name = "";
							}
						}
					}
					
					if(strlen(name)){
						// decode url encoding of the last played/added file url
						str_setstr(&sub_name, "");
						tmp = uriDecode(name);
						if(tmp){
							char *sub;
							if(sub = strrchr(tmp, '/'))
								str_setstr(&sub_name, sub+1);
							free(tmp);
						}
						
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
					
					// check if item is readable
					if(strlen(sub_name)){
						// encode as uri
						str_setstr(&result, "file://");
						tmp = uriEncodeKeepSlash(*dir);
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
					if(strlen(result) > 0)
						goto cleanup;
				}
				
				if(seq){			// sequencial first if true (non-zero)
					db_set_errtag(instance, "traverseFolderListingSequencial");
					// get end time of last item in list
					int listSize;
					listSize = queueCount();
					name = "";
					if(listSize > 0){
						// perform the sql query function: Get last ADDED file name in this directory 
						// looking back over the most recent items in the log no further than the current list length
						if(!db_queryf(instance, "SELECT Time, Name FROM (SELECT Time, Source AS Name FROM %slogs "
										"USE INDEX (%slogs_time) WHERE Location IN(%s) AND (Added & 1) = 1 ORDER BY Time DESC LIMIT %d) "
										"As inqueue WHERE Name LIKE %s AND Name NOT LIKE %s ORDER BY Time DESC LIMIT 1", 
										prefix, prefix, include, listSize, likeStr, notLikeStr)){
													// get first record (should be the only record)
							if(db_result_next_row(instance)){
								name = db_result_get_field_by_name(instance, "Name", NULL);
								if(name == NULL)
									name = "";
							}
						}
					}
					if(strlen(name) == 0){
						// perform the sql query function: Get last PLAYED file name in this directory
						if(!db_queryf(instance,  "SELECT Time, Source AS Name FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
										"AND (Added & 1) <> 1 AND Source LIKE %s AND Source NOT LIKE %s "
										"ORDER BY Time DESC LIMIT 1", prefix, prefix, include, likeStr, notLikeStr)){
							// get first record (should be the only record)
							if(db_result_next_row(instance)){
								name = db_result_get_field_by_name(instance, "Name", NULL);
								if(name == NULL)
									name = "";
							}
						}
					}
					
					if(strlen(name)){
						// decode url encoding of the last played/added file url
						str_setstr(&sub_name, "");
						tmp = uriDecode(name);
						if(tmp){
							char *sub;
							if(sub = strrchr(tmp, '/'))
								str_setstr(&sub_name, sub+1);
							free(tmp);
						}
						
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
						// create db search string for this item
						if(ppre){
							// prefixable, wilcard prefix an use path.
							str_setstr(&encStr, path);
							str_appendstr(&encStr, locBlock.entList[index]->d_name);
							encStr = uriEncodeKeepSlash(encStr);
							folderPathLikeStringEncode(&encStr, 1, 1, instance);	// prefix wild card, match to end
						}else{
							// no prefix, full path
							str_setstr(&encStr, *dir);
							str_appendstr(&encStr, locBlock.entList[index]->d_name);
							encStr = uriEncodeKeepSlash(encStr);
							folderPathLikeStringEncode(&encStr, 0, 1, instance);	// no prefix wild card, no match to end
						}
						// create result for thsi selected item
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
							db_set_errtag(instance, "traverseFolderListingRandomLimit");
							// perform the sql query function: Get Time this file was last played
							last = 0;
							if(!db_queryf(instance,  "SELECT Time AS Time FROM %slogs USE INDEX (%slogs_time) WHERE Location IN(%s) "
										"AND Source LIKE %s ORDER BY Time DESC LIMIT 1", prefix, prefix, include, encStr)){
								// get first record (should be the only record)
								if(db_result_next_row(instance)){
									if(sval = db_result_get_field_by_name(instance, "Time", NULL))
										last = atoll(sval);
								}
							}
							if(((time(NULL) - last) / 3600) < (signed int)randlim){
								// played too recently
								str_setstr(&result, "");
							}
						}
						// free the c-string we allocated
						if(encStr){
							free(encStr);
							encStr = NULL;
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
	db_set_errtag(instance, NULL);
	db_result_free(instance);
	pthread_cleanup_pop(1);
	if(ppre)
		free(ppre);
	if(prefix)
		free(prefix);
	if(path)
		free(path);
	if(include)
		free(include);
	if(notLikeStr)
		free(notLikeStr);
	if(likeStr)
		free(likeStr);
	return result;
}

void folderPick(taskRecord *parent){
	unsigned short nomod, seq, first, rerun, date;
	uint32_t mod, rand, size;
	uint32_t newUID;
	float segout;
	char *tmp;
	char *pick;
	char *dir;
	char *prefix;
	char *target;
	char *priority;
	
	// new, relative prefix/path method, Prefix = "/some/path/prefix/", Path="trailing/directory/name"
	// or absolute Path = "/full/path/to/directory" if prefix is empty
	dir = GetMetaData(parent->UID, "Path", 0);
	prefix = GetMetaData(parent->UID, "Prefix", 0);
	if(strlen(dir) == 0){
		// Path is empty, use legacy Folder absolute path
		free(dir);
		dir = GetMetaData(parent->UID, "Folder", 0);
		// make sure prefix is empty... as it should be with the old method
		str_setstr(&prefix, "");
	}
	mod = GetMetaInt(parent->UID, "Modified", NULL);
	nomod = GetMetaInt(parent->UID, "NoModLimit", NULL);
	seq = GetMetaInt(parent->UID, "Sequencial", NULL);
	first = GetMetaInt(parent->UID, "First", NULL);
	rand = GetMetaInt(parent->UID, "Random", NULL);
	rerun = GetMetaInt(parent->UID, "Rerun", NULL);
	date = GetMetaInt(parent->UID, "Date", NULL);
	segout = GetMetaFloat(parent->UID, "def_segout", NULL);

	pick = traverseFolderListing(&dir, prefix, mod, nomod, seq, rerun, first, rand, date);
	free(dir);
	free(prefix);
	if(pick){
		if(strlen(pick)){
			// add to playlist in pick placeholder position
			newUID = SplitItem(parent->UID, pick, 1);
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
		}
		free(pick);
	}
}

uint32_t dbGetFillID(time_t *when){
	dbInstance *instance = NULL;
	char *sql = NULL;
	char *prefix = NULL;
	char *loc = NULL;
	const char *tmp;
	uint32_t result;
	int hr, min;
	struct tm tm_rec;
	struct dbErr errRec;
	int dbday, dbdaywk;
	
	result = 0;
	// fill time record
	localtime_r(when, &tm_rec);
	dbdaywk = ((tm_rec.tm_mday - 1) / 7) * 7 + tm_rec.tm_wday + 8;
	dbday = tm_rec.tm_wday + 1;
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "dbGetFillID");
	prefix = GetMetaData(0, "db_prefix", 0);
	loc = GetMetaData(0, "db_loc", 0);

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
						"OR [PFX]schedule.Day = %d OR [PFX]schedule.Day = %d) AND ([PFX]schedule.Month = 0 OR [PFX]schedule.Month = %d) "
						"AND ([PFX]hourmap.Map < %d OR ([PFX]hourmap.Map = %d AND [PFX]schedule.Minute <= %d)) "
					"ORDER BY override DESC, [PFX]hourmap.Map DESC, [PFX]schedule.Minute DESC LIMIT 1");
					
	str_ReplaceAll(&sql, "[PFX]", prefix);

	// perform the sql query function, including only items with priority greater than or equal to the over-riding priority above
	if(db_queryf(instance, sql, tm_rec.tm_hour, tm_rec.tm_hour, tm_rec.tm_min, loc, loc, 
								tm_rec.tm_mday, dbday, dbdaywk, tm_rec.tm_mon+1, tm_rec.tm_hour, tm_rec.tm_hour, tm_rec.tm_min))
		goto cleanup;

	str_setstr(&sql,"SELECT [PFX]schedule.Item, [PFX]hourmap.Map AS Hour, "
						"[PFX]schedule.Minute, [PFX]schedule.Priority "
					"FROM ([PFX]schedule, [PFX]hourmap) "
					"LEFT JOIN [PFX]rest ON ([PFX]schedule.Item = [PFX]rest.Item "
						"AND [PFX]rest.Location = %s) "
					"WHERE [PFX]rest.Added IS NULL AND [PFX]schedule.Hour = [PFX]hourmap.Hour "
						"AND [PFX]schedule.Fill <> 0 AND ([PFX]schedule.Location IS NULL "
						"OR [PFX]schedule.Location = %s) AND [PFX]schedule.Priority >= @orPriority "
						"AND ([PFX]schedule.Date = 0 OR [PFX]schedule.Date = %d) AND ([PFX]schedule.Day = 0 "
						"OR [PFX]schedule.Day = %d OR [PFX]schedule.Day = %d) AND ([PFX]schedule.Month = 0 OR [PFX]schedule.Month = %d) "
						"AND ([PFX]hourmap.Map < %d OR ([PFX]hourmap.Map = %d AND [PFX]schedule.Minute <= %d)) "
					"ORDER BY Hour DESC, Minute DESC, Priority DESC LIMIT 1");
					
	str_ReplaceAll(&sql, "[PFX]", prefix);

	// perform the sql query function
	if(db_queryf(instance, sql, loc, loc, tm_rec.tm_mday, dbday, dbdaywk,
									tm_rec.tm_mon+1, tm_rec.tm_hour, tm_rec.tm_hour, tm_rec.tm_min))
		goto cleanup;
	// get first record (should be the only record)
	if(db_result_next_row(instance)){ 
		result = 0;
		if(tmp = db_result_get_field_by_name(instance, "Item", NULL))
			result = atoll(tmp);
		hr = 0;
		if(tmp = db_result_get_field_by_name(instance, "Hour", NULL))
			hr = atoi(tmp);
		min = 0;
		if(tmp = db_result_get_field_by_name(instance, "Minute", NULL))
			min = atoi(tmp);
		// update when pointer to the unix time when the fill item was schedule to start
		tm_rec.tm_hour = hr;
		tm_rec.tm_min = min;
		tm_rec.tm_sec = 0;
		*when = mktime(&tm_rec);
	}

cleanup:
	db_result_free(instance);
	db_set_errtag(instance, NULL);
	if(prefix)
		free(prefix);
	if(loc)
		free(loc);
	if(sql)
		free(sql);
	return result;
}

char *dbGetItemName(uint32_t ID){
	dbInstance *instance = NULL;
	const char *Str;
	char *prefix = NULL;
	char *result = NULL;
	struct dbErr errRec;
	
	str_setstr(&result, "");
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "dbGetItemName");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// perform the sql query function
	if(db_queryf(instance, "SELECT Name FROM %stoc WHERE ID = %lu", prefix, ID))
		goto cleanup;
	// get first record (should be the only record)
	if(db_result_next_row(instance)){ 
		if(Str = db_result_get_field_by_name(instance, "Name", NULL))
			str_setstr(&result, Str);
	}

cleanup:
	db_set_errtag(instance, NULL);
	db_result_free(instance);
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

uint32_t dbGetNextScheduledItem(void **result, time_t *targetTime, short *priority, time_t from_t, time_t to_t, unsigned char highOnly){
	dbInstance *instance = NULL;
	char *sql = NULL;
	char *tmp;
	const char *sval;
	uint32_t ID;
	struct tm from_rec, to_rec;
	short min, hr;
	int fromwday, towday, fromwdaywk, towdaywk;
	
	if(!result)
		return 0;
	if(difftime(to_t, from_t) > 10800)
		// greater than three hours bug trap???
		return 0;
	localtime_r(&from_t, &from_rec);
	localtime_r(&to_t, &to_rec);
	ID = 0;
	fromwdaywk = ((from_rec.tm_mday - 1) / 7) * 7 + from_rec.tm_wday + 8;
	towdaywk = ((to_rec.tm_mday - 1) / 7) * 7 + to_rec.tm_wday + 8;
	fromwday = from_rec.tm_wday + 1;
	towday = to_rec.tm_wday + 1;
	// set up database access
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
		
	if(*result)
		// continuation of next row from existing querry
		db_result_attach(instance, *result);
	else{
		// new querry
		db_set_errtag(instance, "dbGetNextScheduledItem");
		// select all items scheduled for insert between the from and to times.  Group by Item ID, using the latest time
		// and highest priority of any duplicate items are scheduled in that time range.
		// NOTE (BUG): If from and to times are split across a day rollover, the latest item befor the roll over will be 
		// selected for the case where multiple duplicate items are scheduled.
		str_setstr(&sql, "SELECT [prefix]schedule.Item AS Item, MAX(([prefix]hourmap.Map * 60) + [prefix]schedule.Minute) AS Minutes, ");
		str_appendstr(&sql, "MAX([prefix]schedule.Priority) AS Priority FROM ([prefix]schedule, [prefix]hourmap) ");
		str_appendstr(&sql, "LEFT JOIN [prefix]rest ON ([prefix]schedule.Item = [prefix]rest.Item AND [prefix]rest.Location = ");
		str_appendstr(&sql, "[loc-id]) WHERE [prefix]rest.Added IS NULL AND ");
		if(highOnly)
			str_appendstr(&sql, "[prefix]schedule.Priority >= 8 AND ");
		str_appendstr(&sql, "[prefix]schedule.Hour = [prefix]hourmap.Hour ");
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
			str_appendstr(&sql, "AND ([prefix]schedule.Day = 0 OR [prefix]schedule.Day = ");
			str_appendstr(&sql, (tmp = istr(fromwday)));
			free(tmp);
			str_appendstr(&sql, " OR [prefix]schedule.Day = ");
			str_appendstr(&sql, (tmp = istr(fromwdaywk)));
			free(tmp);
			str_appendstr(&sql, ") ");
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
			str_appendstr(&sql, ") AND ([prefix]schedule.Day = 0 OR [prefix]schedule.Day = ");
			str_appendstr(&sql, (tmp = istr(fromwday)));
			free(tmp);
			str_appendstr(&sql, " OR [prefix]schedule.Day = ");
			str_appendstr(&sql, (tmp = istr(fromwdaywk)));
			free(tmp);
			str_appendstr(&sql, ") ");
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
			str_appendstr(&sql, ") AND ([prefix]schedule.Day = 0 OR [prefix]schedule.Day = ");
			str_appendstr(&sql, (tmp = istr(towday)));
			free(tmp);
			str_appendstr(&sql, " OR [prefix]schedule.Day = ");
			str_appendstr(&sql, (tmp = istr(towdaywk)));
			free(tmp);
			str_appendstr(&sql, ") ");
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
		str_appendstr(&sql, "GROUP BY [prefix]schedule.Item ORDER BY Priority DESC, Minutes ASC");
		dbMacroReplace(&sql);
		// perform the sql query function
		if(db_query(instance, sql)){
			goto cleanup;
		}
	}
	// get next row from query result
	if(db_result_next_row(instance)){
		if(sval = db_result_get_field_by_name(instance, "Item", NULL))
			ID = atol(sval);
		if(sval = db_result_get_field_by_name(instance, "Priority", NULL))
			*priority = atoi(sval);
		else
			*priority = 0;
		min = 0;
		if(sval = db_result_get_field_by_name(instance, "Minutes", NULL))
			min = atoi(sval);
		hr = min / 60;
		if(hr < from_rec.tm_hour){
			// must be after a day roll-over
			to_rec.tm_hour = hr;
			to_rec.tm_min = min % 60;
			to_rec.tm_sec = 0;
			*targetTime = mktime(&to_rec);
		}else{
			// all in the same day
			from_rec.tm_hour = hr;
			from_rec.tm_min = min % 60;
			from_rec.tm_sec = 0;
			*targetTime = mktime(&from_rec);
		}
	}

cleanup:
	if(sql)
		free(sql);
	if(!ID){
		db_set_errtag(instance, NULL);
		db_result_free(instance);
		*result = NULL;
	}else
		*result = db_result_detach(instance);
	return ID;
}

void dbSaveFilePos(uint32_t UID, float position){
	dbInstance *instance = NULL;
	uint32_t ID;
	char *prefix, *tmp;
	
	if(UID == 0)
		return;
	ID = GetMetaInt(UID, "ID", NULL);
	if(ID == 0)
		return;
		
	instance = db_get_and_connect();
	if(!instance)
		return;
	db_set_errtag(instance, "dbSaveFilePos");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// perform the sql query function
	db_queryf(instance,  "UPDATE %sfile SET Memory = %f WHERE ID = %lu", prefix, position, ID);
	db_set_errtag(instance, NULL);
	db_result_free(instance);
	free(prefix);
}

char *dbGetReqestComment(time_t theTime){
	dbInstance *instance = NULL;
	const char *Str;
	char *prefix = NULL;
	char *loc = NULL;
	char *result = NULL;
	uint32_t count;
	uint32_t row;
	
	str_setstr(&result, "");
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "dbGetReqestComment");
	prefix = GetMetaData(0, "db_prefix", 0);
	loc = GetMetaData(0, "db_loc", 0);
	
	// perform the sql query function -- first look for comments posted after lastTime
	if(!db_queryf(instance, "SELECT comment, ID FROM %srequest WHERE Item = 0 AND (Location = %s OR Location = 0) AND Time <= FROM_UNIXTIME(%ld) ORDER BY Time ASC LIMIT 1",
								prefix, loc, theTime)){
		// get first record (should be the only record)
		if(db_result_next_row(instance)){ 
			if(Str = db_result_get_field_by_name(instance, "comment", NULL)){
				str_setstr(&result, Str);
				// and delete it from the database since we are about to post it.
				if(Str = db_result_get_field_by_name(instance, "ID", NULL)){
					row = atoll(Str);
					db_queryf(instance, "DELETE FROM %srequest WHERE ID = %lu", prefix, row);
				}
			}
		}
		db_result_free(instance);
	}
	if(strlen(result))
		goto cleanup;
		
	// no comments... get one of the comments out of our pool
	if(!db_queryf(instance, "SELECT comment FROM %srequest WHERE Item = 0 AND (Location = %s OR Location = 0) AND Time > FROM_UNIXTIME(%lu)",
								prefix, loc, theTime)){
		if(count = db_result_get_result_rows(instance)){
			count--;
			row = (uint32_t)(count * RandomNumber());
			if(db_result_select_row(instance, row)){ 
				if(Str = db_result_get_field_by_name(instance, "comment", NULL))
					str_setstr(&result, Str);
			}
		}
		db_result_free(instance);
	}

cleanup:
	if(prefix)
		free(prefix);
	if(loc)
		free(loc);
	db_set_errtag(instance, NULL);

	return result;
}

char *dbGetCurrentMSG(void){
	dbInstance *instance = NULL;
	const char *Str;
	char *prefix = NULL;
	char *loc = NULL;
	char *result = NULL;
	
	str_setstr(&result, "");
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "dbGetCurrentMSG");
	prefix = GetMetaData(0, "db_prefix", 0);
	loc = GetMetaData(0, "db_loc", 0);
	
	// perform the sql query function -- first look for comments posted after lastTime
	if(!db_queryf(instance, "SELECT %smeta.Value AS MSG FROM %slogs, %smeta WHERE %slogs.Item = %smeta.ID AND %smeta.Parent = '%stoc' AND %smeta.Property = 'SCURL' AND %slogs.Location = %s AND (%slogs.Added & 1) <> 1 ORDER BY %slogs.Time DESC LIMIT 1",
								prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, loc, prefix, prefix)){
		// get first record (should be the only record)
		if(db_result_next_row(instance)){ 
			if(Str = db_result_get_field_by_name(instance, "MSG", NULL))
				str_setstr(&result, Str);
		}
	}
	
cleanup:
	if(prefix)
		free(prefix);
	if(loc)
		free(loc);
	db_result_free(instance);
	db_set_errtag(instance, NULL);
	return result;
}

uint32_t IDSearchArtistAlbumTitle(const char *Artist, const char *Album, const char *Title){
	dbInstance *instance = NULL;
	uint32_t recID;
	char *prefix = NULL;
	char *encArtist = NULL;
	char *encAlbum = NULL;
	char *encTitle = NULL;
	const char *sval;
	
	recID = 0;
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "IDSearchArtistAlbumTitle");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	str_setstr(&encArtist, Artist);
	db_quote_string(instance, &encArtist);

	str_setstr(&encAlbum, Album);
	db_quote_string(instance, &encAlbum);

	str_setstr(&encTitle, Title);
	db_quote_string(instance, &encTitle);

	// perform the sql query function
	if(db_queryf(instance, "SELECT %stoc.ID FROM %stoc, %sfile, %sartist, %salbum WHERE %sArtist.ID = %sfile.artist "
			"AND %salbum.ID = %sfile.album AND %stoc.Name = %s AND %sartist.Name = %s AND %salbum.Name = %s LIMIT 1", 
			prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, prefix, encTitle, prefix, encArtist, prefix, encAlbum))
		goto cleanup;
	// get first record (should be the only record)
	if(db_result_next_row(instance)){ 
		if(sval =  db_result_get_field_by_name(instance, "ID", NULL))
		recID = atoll(sval);
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
	db_result_free(instance);
	db_set_errtag(instance, NULL);
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
			localUID = createMetaRecord(result, NULL, 1);
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
		localUID = createMetaRecord(result, NULL, 1);
		// fill the metadata record
		GetURLMetaData(localUID, result);
		if(GetMetaInt(localUID, "Missing", NULL) == 0){
			// all is well 
			releaseMetaRecord(localUID);
			return result;
		}
		free(tmp);
		releaseMetaRecord(localUID);
				
		// still no go... try one of the relative paths if a FPL URL property is set
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
				localUID = createMetaRecord(result, NULL, 1);
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
					localUID = createMetaRecord(result, NULL, 1);
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
					localUID = createMetaRecord(result, NULL, 1);
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

void GetdbTaskMetaData(uint32_t UID, uint32_t recID){
	dbInstance *instance = NULL;
	const char *Key, *Val;
	char *tmp;
	char *prefix;
	int size;
	struct dbErr errRec;
	
	instance = db_get_and_connect();
	if(!instance){
		SetMetaData(UID, "Missing", "1");
		return;
	}
	db_set_errtag(instance, "GetdbTaskMetaData");
	prefix = GetMetaData(0, "db_prefix", 0);

	// perform the sql query function
	if(db_queryf(instance, "SELECT * FROM %stask WHERE ID = %lu", prefix, recID)){
		SetMetaData(UID, "Missing", "1");
		db_set_errtag(instance, NULL);
		free(prefix);
		return;
	}
	if(!db_result_get_result_rows(instance))
		SetMetaData(UID, "Missing", "1");
	while(db_result_next_row(instance)){
		Key = db_result_get_field_by_name(instance, "Property", NULL);
		Val = db_result_get_field_by_name(instance, "Value", NULL);
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
	free(prefix);
	db_result_free(instance);
	db_set_errtag(instance, NULL);
}

int GetdbFileMetaData(uint32_t UID, uint32_t recID, unsigned char markMissing){
	dbInstance *instance = NULL;
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
	char *adjPathStr = NULL;
	char *hash = NULL;
	char *prefixList, *newurl;
	uint32_t listSize, i, p;
	unsigned char c;	// true for case modified
	int stat = -1;
	size_t plen = 0;
	size_t pos;
	glob_t globbuf;

	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "GetdbFileMetaData");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// perform the sql query function
	if(db_queryf(instance, "SELECT * FROM %sfile WHERE ID = %lu", prefix, recID))
		goto cleanup;
	// get first record (should be the only record)
	if(!db_result_next_row(instance))
		goto cleanup;
	else{ 
		if(ctmp = db_result_get_field_by_name(instance, "URL", NULL))
			SetMetaData(UID, "URL", ctmp);
		
		if(ctmp = db_result_get_field_by_name(instance, "Hash", NULL)){
			SetMetaData(UID, "Hash", ctmp);
			hash = strdup(ctmp);
		}
		
		artID = 0;
		if(ctmp = db_result_get_field_by_name(instance, "Artist", NULL)){
			if(artID = atoll(ctmp))
				SetMetaData(UID, "ArtistID", ctmp);
		}
		
		albID = 0;
		if(ctmp = db_result_get_field_by_name(instance, "Album", NULL)){
			if(albID = atol(ctmp))
				SetMetaData(UID, "AlbumID", ctmp);
		}
		
		Float = 0.0;
		if(ctmp = db_result_get_field_by_name(instance, "Volume", NULL))
			Float = atof(ctmp);
		snprintf(buf, sizeof buf, "%0.2f", Float);
		SetMetaData(UID, "Volume", buf);
		
		Float = 0.0;
		if(ctmp = db_result_get_field_by_name(instance, "SegIn", NULL))
			Float = atof(ctmp);
		snprintf(buf, sizeof buf, "%0.2f", Float);
		SetMetaData(UID, "SegIn", buf);
		
		Float = 0.0;
		if(ctmp = db_result_get_field_by_name(instance, "SegOut", NULL))
			Float = atof(ctmp);
		snprintf(buf, sizeof buf, "%0.2f", Float);
		SetMetaData(UID, "SegOut", buf);
		
		Float = 0.0;
		if(ctmp = db_result_get_field_by_name(instance, "FadeOut", NULL))
			Float = atof(ctmp);
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "FadeOut", buf);
		
		Float = 0.0;
		if(ctmp = db_result_get_field_by_name(instance, "Intro", NULL))
			Float = atof(ctmp);
		snprintf(buf, sizeof buf, "%0.1f", Float);
		SetMetaData(UID, "Intro", buf);
		
		if(ctmp = db_result_get_field_by_name(instance, "Memory", NULL)){
			if(atof(ctmp))
				SetMetaData(UID, "Memory", ctmp);
		}
		
		if(ctmp = db_result_get_field_by_name(instance, "OutCue", NULL))
			SetMetaData(UID, "OutCue", ctmp);
		
		uInt = 0;
		if(ctmp = db_result_get_field_by_name(instance, "Track", NULL))
			uInt = atoi(ctmp);
		if(uInt){
			snprintf(buf, sizeof buf, "%u", (unsigned int)uInt);
			SetMetaData(UID, "Track", buf);
		}
		
		missing = 0;
		if(ctmp = db_result_get_field_by_name(instance, "Missing", NULL))
			missing = atof(ctmp);
		snprintf(buf, sizeof buf, "%d", missing);
		SetMetaData(UID, "Missing", buf);
		
		ctmp = db_result_get_field_by_name(instance, "Path", NULL);
		if(ctmp && strlen(ctmp)){
			SetMetaData(UID, "Path", ctmp);
			pathStr = strdup(ctmp);
			if(ctmp = db_result_get_field_by_name(instance, "Prefix", NULL))
				SetMetaData(UID, "Prefix", ctmp);
		}else{
			// fall back to old rev <= 3 Mount list
			if(ctmp = db_result_get_field_by_name(instance, "Mount", NULL)){
				SetMetaData(UID, "Mount", ctmp);

				tmp = GetMetaData(UID, "Mount", 0);
				mountName = str_NthField(tmp, "/", str_CountFields(tmp, "/"));
				free(tmp);
			}
		}

		// get Artist Name
		if(!db_queryf(instance, "SELECT Name FROM %sartist WHERE ID = %lu", prefix, artID)){
			// get first record (should be the only record)
			if(db_result_next_row(instance)){
				if(ctmp = db_result_get_field_by_name(instance, "Name", NULL))
					SetMetaData(UID, "Artist", ctmp);
			}
		}
		// get Album Name
		if(!db_queryf(instance, "SELECT Name FROM %salbum WHERE ID = %lu", prefix, albID)){
			// get first record (should be the only record)
			if(db_result_next_row(instance)){
				if(ctmp = db_result_get_field_by_name(instance, "Name", NULL))
					SetMetaData(UID, "Album", ctmp);
			}
		}
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
	
	// Copy the string and change the case of the first letter, if it's a letter
	// for an alternate comparison on systems that change the case of the mount
	str_setstr(&adjPathStr, pathStr);
	if(isupper(*adjPathStr))
		*adjPathStr = tolower(*adjPathStr);
	else if(islower(*adjPathStr))
		*adjPathStr = toupper(*adjPathStr);
	else{
		// not case changable
		free(adjPathStr);
		adjPathStr = NULL;
	}
	
	// path is Full Path, pathStr is the relative path, if any
	// i.e. path = /longer/path/to/mount/some/file
	// and  pathStr = mount/some/file
	globbuf.gl_offs = 0;
	globbuf.gl_pathc = 0;
	i = 0;
	p = 0;
	c = 0;
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
					if(adjPathStr && c){
						// trying the pathStr version with case adjustment
						str_appendstr(&path, adjPathStr);
						c = 0; // go back to original case next time
					}else{
						str_appendstr(&path, pathStr);
						if(adjPathStr)
							c = 1; // try case change next time
					}
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
							if(!c)
								p++;
							free(tmp);
							break;
						}
					}
					free(tmp);
					if(!c)
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
		}else{
			SetMetaData(UID, "Prefix", "");
			SetMetaData(UID, "Path", path);
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
				db_queryf(instance, "UPDATE %sfile SET Missing = 1 WHERE ID = %lu", prefix, recID);
			}else{
				stat = 1;
				// unmark record as missing and update file location, etc.
				pre = GetMetaData(UID, "Prefix", 0);
				db_quote_string(instance, &pre);
				rem = GetMetaData(UID, "Path", 0);
				db_quote_string(instance, &rem);
				newURL = GetMetaData(UID, "URL", 0);
				db_quote_string(instance, &newURL);
				tmp = GetMetaData(UID, "Hash", 0);
				db_quote_string(instance, &tmp);
				// do the update
				
				// Old OSX: Mount -> Prefix + mountName (first dir in path) 
				// if path=/some/path, mountName = /
				// if path=some/path, mountName = Some/
				// mount = prefix + mountName
				str_setstr(&mountName, "/");
				if(strlen(pre)){
					if((tmp = strstr(rem, "/")) && (tmp != rem)){
						if(tmp = str_substring(rem, 0, (tmp - rem))){
							*tmp = toupper(*tmp);
							str_setstr(&mountName, pre);
							str_appendstr(&mountName, tmp);
							free(tmp);
						}
					}
				}
				db_queryf(instance, 
					"UPDATE %sfile SET Missing = 0, Hash = %s, Mount = %s, Path = %s, Prefix = %s, URL = %s WHERE ID = %lu", 
					 prefix, tmp, rem, pre, mountName, newURL, recID);

				// for now, don't modify old v4 URL and Mount properties, due to v4 lacking Mount name case sensitivity
//				db_queryf(instance, 
//					"UPDATE %sfile SET Missing = 0, Hash = %s, Path = %s,  Prefix = %s WHERE ID = %lu", 
//					 prefix, tmp, rem, pre, recID);
				free(tmp);
				free(rem);
				free(pre);
				free(newURL);
			}
		}
	}
cleanup:
	db_set_errtag(instance, NULL);
	db_result_free(instance);
	free(prefix);
	if(mountName)
		free(mountName);
	if(adjPathStr)
		free(adjPathStr);
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
	dbInstance *instance = NULL;
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
	
	Type[0] = 0;
	
	instance = db_get_and_connect();
	if(!instance)
		goto error;
	db_set_errtag(instance, "GetItemMetaData");
	prefix = GetMetaData(0, "db_prefix", 0);
	
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
	if(db_queryf(instance, "SELECT * FROM %stoc WHERE ID = %lu", prefix, recID))
		goto error;
	// get first record (should be the only record)
	if(db_result_next_row(instance)){ 
		Str = db_result_get_field_by_name(instance, "Type", NULL);
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
		
		Str = db_result_get_field_by_name(instance, "Name", NULL);
		if(Str == NULL)
			Str = "[Missing Name]";
		SetMetaData(UID, "Name", Str);
		
		if(Str = db_result_get_field_by_name(instance, "Tag", NULL))
			SetMetaData(UID, "Tag", Str);
		
		if(Str = db_result_get_field_by_name(instance, "Script", NULL)){
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
		Float = 0.0;
		if(Str = db_result_get_field_by_name(instance, "Duration", NULL))
			Float = atof(Str);
		snprintf(buf, sizeof buf, "%.2f", Float);
		SetMetaData(UID, "Duration", buf);
		
		if(Str = db_result_get_field_by_name(instance, "Added", NULL)){
			verylong = atoll(Str);
			snprintf(buf, sizeof buf, "%ld", verylong);
			SetMetaData(UID, "Added", buf);
		}
	}else
		goto error;
	
	// get database fingerprint
	unsigned int fp = getFingerprint();
	if(fp){
		snprintf(buf, sizeof buf, "%d", fp);
		SetMetaData(UID, "Fingerprint", buf);
	}
	
	// perform the sql query function for custom properties
	if(!db_queryf(instance, "SELECT * FROM %smeta WHERE Parent = '%stoc' AND ID = %lu", prefix, prefix, recID)){
		while(db_result_next_row(instance)){
			const char *prop, *value;
			prop = db_result_get_field_by_name(instance, "Property", NULL);
			value = db_result_get_field_by_name(instance, "Value", NULL);
			if(prop && strlen(prop) && value && strlen(value))
				SetMetaData(UID, prop, value);
		}
	}
	db_set_errtag(instance, NULL);
	db_result_free(instance);
	
	if(!strcmp(Type,"file")){
		// get additional meta data related to file references stored in the db
		GetdbFileMetaData(UID, recID, GetMetaInt(0, "db_mark_missing", NULL));
		if(prefix)
			free(prefix);
		tmp = GetMetaData(UID, "Hash", 0);
		if(strlen(tmp) == 0){
			// change-able file - fall back to file's actual meta data
			free(tmp);
			tmp = GetMetaData(UID, "URL", 0);
			GetGstDiscoverMetaData(UID, tmp);
		}
		free(tmp);
		return;
		
	}
	if(!strcmp(Type,"task")){
		// get additional meta data related to task references stored in the db
		GetdbTaskMetaData(UID, recID);
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
	db_set_errtag(instance, NULL);
	db_result_free(instance);
	SetMetaData(UID, "Missing", "1");
	if(prefix)
		free(prefix);
}

uint32_t IDSearchMarkedFile(const char *path, const char *Hash){
	dbInstance *instance = NULL;
	uint32_t recID;
	char *prefix = NULL;
	char *encPath = NULL;
	const char *str;
	
	recID = 0;
	instance = db_get_and_connect();
	if(!instance)
		goto cleanup;
	db_set_errtag(instance, "IDSearchMarkedFile");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	str_setstr(&encPath, path);
	db_quote_string(instance, &encPath);

	// perform the sql query function
	if(db_queryf(instance, "SELECT ID FROM %sfile WHERE Hash = '%s' AND SUBSTRING_INDEX(Path, '/', 1) = SUBSTRING_INDEX(%s, '/', 1)", prefix, Hash, encPath))
		goto cleanup;
	// get first record (**should** be the only record)
	if(db_result_next_row(instance)){ 
		if(str = db_result_get_field_by_name(instance, "ID", NULL))
			recID = atol(str);
	}
	
cleanup:
	if(encPath);
		free(encPath);
	db_set_errtag(instance, NULL);
	if(prefix)
		free(prefix);
	return recID;
}

void dbFileSync(ctl_session *session, unsigned char silent){
	dbInstance *instance = NULL;
	void *curres;
	char *prefix;
	const char *URL, *Path, *cstr;
	char *MSG, *tmp;
	char *Str = NULL;
	uint32_t count, missing, fixed, error;
	uint32_t recID;
	uint32_t localUID;
	char buf[4096]; // send data buffer 
	int sendCount;
	int tx_length;
	int result;
	
	count = 0;
	missing = 0;
	fixed = 0;
	error = 0;
	instance = db_get_and_connect();
	if(!instance)
		return;
	db_set_errtag(instance, "dbFileSync");
	prefix = GetMetaData(0, "db_prefix", 0);
	
	// perform the sql query function
	if(!db_queryf(instance, "SELECT ID, URL, Path FROM %sfile ORDER BY ID", prefix)){
		sendCount = 1;
		count = db_result_get_result_rows(instance);
		while(db_result_next_row(instance) && (sendCount > 0)){ 
			recID = 0;
			if(cstr = db_result_get_field_by_name(instance, "ID", NULL))
				recID = atol(cstr);
			Path = db_result_get_field_by_name(instance, "Path", NULL);
			URL = db_result_get_field_by_name(instance, "URL", NULL);
			if(URL == NULL)
				URL = "[Missing URL]";
			result = -1;
			if(recID){
				// create an empty meta data record to hold results
				localUID = createMetaRecord("", NULL, 1);
				curres = db_result_detach(instance);
				// getting meta data with markMissing = true will update file info in the database
				result = GetdbFileMetaData(localUID, recID, 1);
				// done with the metadata record...
				releaseMetaRecord(localUID);
				db_result_attach(instance, curres);
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
			if(Path && strlen(Path)){
				str_setstr(&Str, Path);
			}else{
				str_setstr(&Str, "NOPATH, URL=");
				str_appendstr(&Str, URL);
			}
			tx_length = snprintf(buf, sizeof buf, "%u\t%s\t%s\n", (unsigned int)count, MSG, Str);
			sendCount = my_send(session, buf, tx_length, silent, 0);
			count--;
		}
		count = db_result_get_result_rows(instance);
	}
	tx_length = snprintf(buf, sizeof buf, "\nChecked=%u\nMissing=%u\nError=%u\nFixed=%u\n", (unsigned int)count, (unsigned int)missing, (unsigned int)error, (unsigned int)fixed);
	my_send(session, buf, tx_length, silent, 0);
	tx_length = snprintf(buf, sizeof buf, "Checked=%u, Missing=%u, Error=%u, Fixed=%u", (unsigned int)count, (unsigned int)missing, (unsigned int)error, (unsigned int)fixed);
	MSG = NULL;
	str_setstr(&MSG, "[database] dbSync-'");
	str_appendstr(&MSG, (tmp = GetMetaData(0, "db_name", 0)));
	free(tmp);
	str_appendstr(&MSG, "': ");
	str_appendstr(&MSG, buf);
	serverLogMakeEntry(MSG);
	free(MSG);

	db_result_free(instance);
	db_set_errtag(instance, NULL);
	free(prefix);
	if(Str)
		free(Str);
}

int dbHashSearchFileHeiarchy(ctl_session *session, unsigned char silent, const char *searchPath, uint32_t mS_pace){
	dbInstance *instance = NULL;
	void *dbresult;
	char buf[4096]; // send data buffer 
	int tx_length;
	const char *pathArray[2] = {searchPath, 0};
	char *prefix = NULL;
	char *path;
	char *tmp, *enc;
	char *vol;
	char *hash;
	const char *sval;
	char *newurl = NULL;
	unsigned int recID;
	struct timespec timeout;
	FTS *fts_session;
	FTSENT *fts_entry;	
	unsigned int localUID;
	
	vol = strdup(searchPath);
	if(tmp = getFilePrefixPoint(&vol)){
		instance = db_get_and_connect();
		if(!instance){
			free(vol);
			return 0;
		}
		db_set_errtag(instance, "dbFileSearch");
		
		// allocate, copy and encode the strings in the db's format
		str_setstr(&tmp, vol);
		free(vol);
		vol = str_NthField(tmp, "/", 0);
		free(tmp);
		db_quote_string(instance, &vol);
		if(!strlen(vol)){
			tx_length = snprintf(buf, sizeof buf, " Path not in prefix list\n");
			my_send(session, buf, tx_length, silent, 0);	
			free(vol);
			db_set_errtag(instance, NULL);
			return 0;
		}
	}else{
		tx_length = snprintf(buf, sizeof buf, " Path not in prefix list\n");
		my_send(session, buf, tx_length, silent, 0);	
		free(vol);
		return 0;
	}
	
	timeout.tv_nsec= (mS_pace % 1000) * 1000 * 1000; // nS
	timeout.tv_sec= mS_pace / 1000;	// 0 seconds
	
	prefix = GetMetaData(0, "db_prefix", 0);
	
	if(fts_session = fts_open((char * const *)pathArray, FTS_NOCHDIR | FTS_XDEV, NULL)){
		tx_length = snprintf(buf, sizeof buf, " Searching...\n");
		my_send(session, buf, tx_length, silent, 0);
		tmp = NULL;
		str_setstr(&tmp, "[database] dbFileSearch-searching for moved files: ");
		str_appendstr(&tmp, searchPath);
		serverLogMakeEntry(tmp);
		free(tmp);
 
		while(fts_entry = fts_read(fts_session)){
			if(fts_entry->fts_name[0] == '.')
				continue;	// ignore . prefixed files
			tx_length = snprintf(buf, sizeof buf, "\t%s\n", fts_entry->fts_path);
			my_send(session, buf, tx_length, silent, 0);
			
			if(fts_entry->fts_info == FTS_F){
				if(hash = GetFileHash(fts_entry->fts_path)){
					// perform the sql query function
					if(!db_queryf(instance, 
							"SELECT ID FROM %sfile WHERE Hash = '%s' AND SUBSTRING_INDEX(Path, '/', 1) = %s", prefix, hash, vol)){
						while(db_result_next_row(instance)){ 
							recID = 0;
							if(sval = db_result_get_field_by_name(instance, "ID", NULL))
								recID = atol(sval);
							if(recID){
								tx_length = snprintf(buf, sizeof buf, "\t\t%u", recID);
								my_send(session, buf, tx_length, silent, 0);
								// see if the record is for a missing file
								// create an empty meta data record to hold results
								dbresult = db_result_detach(instance);
								localUID = createMetaRecord("", NULL, 1);
								GetdbFileMetaData(localUID, recID, 0);
								db_result_attach(instance, dbresult);
								if(GetMetaInt(localUID, "Missing", NULL)){
									// update the URL, path, prefix and unmark as missing
									tmp = uriEncodeKeepSlash(fts_entry->fts_path);
									str_setstr(&newurl, "file://");
									str_appendstr(&newurl, tmp);
									free(tmp);
									db_quote_string(instance, &newurl);
									path = NULL;
									str_setstr(&path, fts_entry->fts_path);
									if(tmp = getFilePrefixPoint(&path)){
										db_quote_string(instance, &tmp);
										db_quote_string(instance, &path);
										dbresult = db_result_detach(instance);
										db_queryf(instance, "UPDATE %sfile SET Missing = 0, URL = %s, Path = %s, Prefix = %s WHERE ID = %lu", 
												prefix, newurl, path, tmp, recID);
										free(tmp);
										free(path);
										if(db_result_get_rows_affected(instance)){
											tx_length = snprintf(buf, sizeof buf, " Fixed\n");
											my_send(session, buf, tx_length, silent, 0);
											tmp = NULL;
											str_setstr(&tmp, "[database] dbFileSearch-Found missing file: ");
											str_appendstr(&tmp, fts_entry->fts_path);
											serverLogMakeEntry(tmp);
											free(tmp);
										}
										db_result_attach(instance, dbresult);
									}else{
										str_setstr(&tmp, "[database] dbFileSearch-Fix failed for found missing file: ");
										str_appendstr(&tmp, fts_entry->fts_path);
										serverLogMakeEntry(tmp);
										free(tmp);
									}
									free(newurl);
									newurl = NULL;
									
								}else{
									tx_length = snprintf(buf, sizeof buf, " OK\n");
									my_send(session, buf, tx_length, silent, 0);	
								}
								// done with the metadata record... 
								releaseMetaRecord(localUID);
							}
						}
					}
					free(hash);
				}
			}
			
			// Wait for a time delay to check next
			nanosleep(&timeout, NULL);
		}
		free(vol);
		free(prefix);
		db_set_errtag(instance, NULL);
		db_result_free(instance);
		return 1;
	}
	tx_length = snprintf(buf, sizeof buf, " Invalid\n");
	my_send(session, buf, tx_length, silent, 0);	
	free(vol);
	free(prefix);
	db_set_errtag(instance, NULL);
	db_result_free(instance);
	return 0;
}

void dbFileSearch(ctl_session *session, unsigned char silent, const char *Path, uint32_t pace){
	dbInstance *instance = NULL;
	void *dbresult;
	char *prefix;
	char *tmp;
	char *path = NULL;
	char *adjmountName = NULL;
	char buf[4096]; // send data buffer
	int tx_length, i, p, c, listSize;
	char *prefixList;
	const char *mountName;
	glob_t globbuf;
	
	instance = db_get_and_connect();
	if(!instance)
		return;
	db_set_errtag(instance, "dbFileSearch");
	
	if((Path == NULL) || (strlen(Path) == 0)){	
		// no specific file path specified
		// get database default mount search path list
		prefix = GetMetaData(0, "db_prefix", 0);

		// get mount names
		if(db_queryf(instance, "SELECT Distinct SUBSTRING_INDEX(Path, '/', 1) AS MountName FROM %sfile", prefix)){
			free(prefix);
			db_set_errtag(instance, NULL);
			return;
		}
		free(prefix);
		while(db_result_next_row(instance)){ 
			if((mountName = db_result_get_field_by_name(instance, "MountName", NULL)) && (strlen(mountName) > 1)){
				// for each mount, pre-pend each of the search paths, and traverse all files
				// with in looking for files with Hash codes that match in the dtatabse and are missing.
				p = 0;
				i = 0;
				c = 0;
				// Copy the string and change the case of the first letter, if it's a letter
				// for an alternate comparison on systems that change the case of the mount
				str_setstr(&adjmountName, mountName);
				if(isupper(*adjmountName))
					*adjmountName = tolower(*adjmountName);
				else if(islower(*adjmountName))
					*adjmountName = toupper(*adjmountName);
				else{
					// not case changable
					free(adjmountName);
					adjmountName = NULL;
				}
				prefixList = GetMetaData(0, "file_prefixes", 0);
				listSize = str_CountFields(prefixList, ",") + 1;
				while(p < listSize){
					// Use glob function to find existing paths to the mount
					tmp = str_NthField(prefixList, ",", p);
					if(tmp){
						if(strlen(tmp)){
							str_setstr(&path, tmp);
							if(adjmountName && c){
								// trying the version with case adjustment
								str_appendstr(&path, adjmountName);
								c = 0; // go back to original case next time
							}else{
								str_appendstr(&path, mountName);
								if(adjmountName)
									c = 1; // try case change next time
							}
							i = 0;
							if(!glob(path, GLOB_NOSORT, NULL, &globbuf)){
								while(i < globbuf.gl_pathc){
									// found a path in new glob list
									tx_length = snprintf(buf, sizeof buf, "Search Location %s: ", globbuf.gl_pathv[i]);
									my_send(session, buf, tx_length, silent, 0);
									dbresult = db_result_detach(instance);
									dbHashSearchFileHeiarchy(session, silent, globbuf.gl_pathv[i], pace);
									db_result_attach(instance, dbresult);
									i++;
								}
							}
							globfree(&globbuf);
						}
						free(tmp);
						if(!c)
							p++;
					}else
						break;
				}
				free(prefixList);
			}
		}
		if(path)
			free(path);
	}else{	
		// search path has been specified
		tx_length = snprintf(buf, sizeof buf, "Search Location %s: ", Path);
		my_send(session, buf, tx_length, silent, 0);
		dbHashSearchFileHeiarchy(session, silent, Path, pace);
	}
	db_set_errtag(instance, NULL);
	db_result_free(instance);
}
