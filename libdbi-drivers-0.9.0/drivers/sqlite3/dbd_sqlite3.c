/*
 * libdbi-drivers - database drivers for libdbi, a database independent
 * abstraction layer for C.
 * Copyright (C) 2002-2007, Markus Hoenicka
 * http://libdbi-drivers.sourceforge.net
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * dbd_sqlite3.c: SQLite3 database support (using libsqlite3)
 * Copyright (C) 2005-2007, Markus Hoenicka <mhoenicka@users.sourceforge.net>
 * http://libdbi-drivers.sourceforge.net
 * 
 * $Id: dbd_sqlite3.c,v 1.49 2013/01/08 23:55:54 mhoenicka Exp $
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE /* we need asprintf */

/* this is defined by the Makefile and passed via -D */
/* #define DBDIR /usr/local/var/lib/libdbi/sqlite3 */

#ifndef HAVE_ATOLL
long long atoll(const char *str);
#endif

#ifndef HAVE_STRTOLL
long long strtoll(const char *nptr, char **endptr, int base);
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h> /* defines _POSIX_PATH_MAX */
#include <dirent.h> /* directory listings */
#include <unistd.h> /* stat */
#include <sys/stat.h> /* S_ISXX macros */
#include <sys/types.h> /* directory listings */
#include <ctype.h> /* toupper, etc */

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

#include <sqlite3.h>
#include "dbd_sqlite3.h"

static const dbi_info_t driver_info = {
  "sqlite3",
  "SQLite3 database support (using libsqlite3)",
  "Markus Hoenicka <mhoenicka@users.sourceforge.net>",
  "http://libdbi-drivers.sourceforge.net",
  "dbd_sqlite3 v" VERSION,
  __DATE__
};

static const char *custom_functions[] = SQLITE3_CUSTOM_FUNCTIONS;
static const char *reserved_words[] = SQLITE3_RESERVED_WORDS;
static const char default_dbdir[] = DBDIR;

/* the encoding strings */
static const char sqlite3_encoding_UTF8[] = "UTF-8";
static const char sqlite3_encoding_UTF16[] = "UTF-16";

/* pointers to sqlite3 functions - avoids tons of if/elses */
/* int (*my_sqlite3_open)(const char *,sqlite3 **); */

/* forward declarations */
static int _real_dbd_connect(dbi_conn_t *conn, const char* database);
static void _translate_sqlite3_type(enum enum_field_types fieldtype, unsigned short *type, unsigned int *attribs);
static void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx);
static int find_result_field_types(char* field, dbi_conn_t *conn, const char* statement);
static int getTables(char** tables, int index, const char* statement, char* curr_table);
static void freeTables(char** tables, int table_count);
static char* get_field_type(char*** ptr_result_table, const char* curr_field_name, int numrows);
static size_t sqlite3_escape_string(char *to, const char *from, size_t length);
static int wild_case_compare(const char *str,const char *str_end,
		      const char *wildstr,const char *wildend,
		      char escape);
static const char* _conn_get_dbdir(dbi_conn_t *conn);


/* the real functions */
void dbd_register_driver(const dbi_info_t **_driver_info, const char ***_custom_functions, const char ***_reserved_words) {
  /* this is the first function called after the driver module is loaded into memory */
  *_driver_info = &driver_info;
  *_custom_functions = custom_functions;
  *_reserved_words = reserved_words;
}

int dbd_initialize(dbi_driver_t *driver) {
  /* perform any database-specific server initialization.
   * this is called right after dbd_register_driver().
   * return -1 on error, 0 on success. if -1 is returned, the driver will not
   * be added to the list of available drivers. */

  /* this indicates the driver can be safely unloaded when libdbi is
     shut down. Change the value to '0' (zero) if the driver, or a
     library it is linked against, installs exit handlers via
     atexit() */
  _dbd_register_driver_cap(driver, "safe_dlclose", 1);

  /* this indicates the database engine supports transactions */
  _dbd_register_driver_cap(driver, "transaction_support", 1);
  
  /* this indicates the database engine supports savepoints */
  _dbd_register_driver_cap(driver, "savepoint_support", 1);

  return 0;
}

int dbd_finalize(dbi_driver_t *driver) {
	/* perform any database-specific client library shutdown.
	 * this is called right before dlclose()ing the driver.
	 * return -1 on error, 0 on success. */

	return 0;
}

int dbd_connect(dbi_conn_t *conn) {
  /* connect using the database set with the "dbname" option */
  return _real_dbd_connect(conn, "");
}

static int _real_dbd_connect(dbi_conn_t *conn, const char* database) {
  /* connect using the database passed as an argument. If passed NULL
     or an empty string, this function tries to use the database set
     with the "dbname" option */
  sqlite3 *sqcon;
  int sqlite3_errcode;
  char* sq_errmsg = NULL;
  char* db_fullpath = NULL;

  /* ToDo: make OS-independent */
  const char dirsep[] = "/";

  const char *dbname;
  const char *dbdir;
  const char *encoding;

  int timeout;
  dbi_result dbi_result;

  /* initialize error stuff */
  conn->error_number = 0;
  conn->error_message = NULL;

  /* sqlite3 does not use hostname, username, password, port */
  if (database && *database) {
    dbname = database;
  }
  else {
    dbname = dbi_conn_get_option(conn, "dbname");
  }

  if (!dbname) {
    _dbd_internal_error_handler(conn, "no database specified", DBI_ERROR_CLIENT);
    return -1;
  }

  encoding = dbi_conn_get_option(conn, "encoding");

  if (!encoding) {
    /* use UTF-8 as default */
    encoding = sqlite3_encoding_UTF8;
  }

  dbdir = _conn_get_dbdir(conn);
	
  if (!dbdir) {
    _dbd_internal_error_handler(conn, "no database directory specified", DBI_ERROR_CLIENT);
    return -1;
  }

  /* the requested database is a file in the given directory. Assemble
     full path of database */
  db_fullpath = malloc(strlen(dbname)+strlen(dbdir)+2); /* leave room
							   for \0 and / */
  if (db_fullpath == NULL) {
    _dbd_internal_error_handler(conn, NULL, DBI_ERROR_NOMEM);
    return -1;
  }

  /* start with an empty string */
  db_fullpath[0] = '\0';

  if (strcmp(dbname, ":memory:")) {
    if (dbdir && *dbdir) {
      strcpy(db_fullpath, dbdir);
    }
    if (db_fullpath[strlen(db_fullpath)-1] != *dirsep) {
      /* db_fullpath length was checked above */
      strcat(db_fullpath, dirsep);
    }
  }
  /* else: open an in-memory database which does not require the path prefix */
  if (dbname && *dbname) {
    /* db_fullpath length was checked above */
    strcat(db_fullpath, dbname);
  }

  /*   fprintf(stderr, "try to open %s<<\n", db_fullpath); */
  if (!strcmp(encoding, sqlite3_encoding_UTF8)) {
    sqlite3_errcode = sqlite3_open(db_fullpath, &sqcon);
  }
  else {
    sqlite3_errcode = sqlite3_open16(db_fullpath, &sqcon);
  }

  free(db_fullpath);
	
  if (sqlite3_errcode) {

    /* todo: check the return code */

    /* sqlite3 creates a database the first time we try to access
       it. If this function fails, there's usually a problem with
       access rights or an existing database is corrupted or created
       with an incompatible version */
    if (sq_errmsg) {
      _dbd_internal_error_handler(conn, sq_errmsg, (const int) sqlite3_errcode);
      free(sq_errmsg);
    }
    else {
      _dbd_internal_error_handler(conn, "could not open database", (const int) sqlite3_errcode);
    }
    return -1;
  }
  else {
    conn->connection = (void *)sqcon;
    if (dbname) {
      conn->current_db = strdup(dbname);
    }
  }

  /* set the SQLite timeout to timeout milliseconds. The older
     SQLite3-specific setting takes precedence over the generic timeout
     option for backwards compatibility */
  timeout = dbi_conn_get_option_numeric(conn, "sqlite3_timeout");

  if (timeout == -1) {
    /* generic timeout is specified in seconds, not milliseconds */
    timeout = 1000*dbi_conn_get_option_numeric(conn, "timeout");
    if (timeout == -1) {
      timeout = 0;
    }
  }

  sqlite3_busy_timeout(sqcon, timeout);

  /* this is required to make SQLite work like other database engines
     in that it returns the column information even if there are no
     rows in a result set */
  dbi_result = dbd_query(conn, "PRAGMA empty_result_callbacks=1");
  
  if (dbi_result) {
    dbi_result_free(dbi_result);
  }

  return 0;
}

int dbd_disconnect(dbi_conn_t *conn) {
  if (conn->connection) {
    sqlite3_close((sqlite3 *)conn->connection);
    if (conn->error_number) {
      conn->error_number = 0;
    }
    if (conn->error_message) {
      free(conn->error_message);
      conn->error_message = NULL;
    }
  }
  return 0;
}

int dbd_fetch_row(dbi_result_t *result, unsigned long long rowidx) {
  dbi_row_t *row = NULL;

  if (result->result_state == NOTHING_RETURNED) {
    return 0;
  }

  if (result->result_state == ROWS_RETURNED) {
    /* get row here */
    row = _dbd_row_allocate(result->numfields);
    _get_row_data(result, row, rowidx);
    _dbd_row_finalize(result, row, rowidx);
  }
	
  return 1; /* 0 on error, 1 on successful fetchrow */
}

int dbd_free_query(dbi_result_t *result) {
  if (result->result_handle) {
    sqlite3_free_table((char **)result->result_handle);
  }
  return 0;
}

int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) {
  result->currowidx = rowidx;
  return 1;
}

int dbd_get_socket(dbi_conn_t *conn){
  /* sqlite3 does not use sockets, so we'll always return 0 */
  return (int)0;
}

const char *dbd_get_encoding(dbi_conn_t *conn){
  const char* encoding;
  /* encoding is a compile-time option with the sqlite3
     library. Instead of using the sqlite3-provided string, we use the
     iana.org names */

  encoding = dbi_conn_get_option(conn, "encoding");

  if (!encoding) {
    /* use UTF-8 as default */
    encoding = sqlite3_encoding_UTF8;
  }
  /* todo: implement utf8 vs utf16 distinction */
  return encoding;
}

const char* dbd_encoding_to_iana(const char *db_encoding) {
  /* nothing to translate, return original encoding */
  return db_encoding;
}

const char* dbd_encoding_from_iana(const char *iana_encoding) {
  /* nothing to translate, return original encoding */
  return iana_encoding;
}

char *dbd_get_engine_version(dbi_conn_t *conn, char *versionstring) {
  dbi_result_t *dbi_result;
  const char *versioninfo = NULL;

  /* initialize return string */
  *versionstring = '\0';

  dbi_result = dbd_query(conn, "SELECT sqlite_version()");

  if (dbi_result) {
    if (dbi_result_next_row(dbi_result)) {
      versioninfo = dbi_result_get_string_idx(dbi_result, 1);
      strncpy(versionstring, versioninfo, VERSIONSTRING_LENGTH-1);
      versionstring[VERSIONSTRING_LENGTH-1] = '\0';
    }
    dbi_result_free(dbi_result);
  }

  return versionstring;
}

dbi_result_t *dbd_list_dbs(dbi_conn_t *conn, const char *pattern) {
  char *sq_errmsg = NULL;
  char old_cwd[_POSIX_PATH_MAX] = "";
  char sql_command[_POSIX_PATH_MAX+64];
  int retval;
  size_t entry_size;
  DIR *dp;
  struct dirent *entry;
  struct dirent *result;
  struct stat statbuf;
  dbi_result rs;

  /* sqlite3 has no builtin function to list databases. Databases are just
     files in the data directory. We search for matching files and fill a
     temporary table with what we've found. Then we query this table and
     pretend sqlite3 has done all the work */
  const char *sq_datadir = _conn_get_dbdir(conn);

  /* this is not nice but we have to drop the table even if it does not
     exist (sqlite3 has no way to list *temporary* tables so we can't check
     for it's existence). Then we start over with a fresh table lest we
     want duplicates.
     Update: Now apparently there is a system table that lists
     temporary tables, but the DROP TABLE error doesn't hurt and is
     most likely faster than checking for the existence of the table */
  rs = dbd_query(conn, "DROP TABLE libdbi_databases");
  dbi_result_free(rs);
  rs = dbd_query(conn, "CREATE TEMPORARY TABLE libdbi_databases (dbname VARCHAR(255))");
  dbi_result_free(rs);

  if (sq_datadir && (dp = opendir(sq_datadir)) == NULL) {
    _dbd_internal_error_handler(conn, "could not open data directory", DBI_ERROR_CLIENT);
    return NULL;
  }

  /* allocate memory for readdir_r(3) */
  entry_size = _dirent_buf_size(dp);
  if (entry_size == 0) {
    return NULL;
  }

  entry = (struct dirent *) malloc (entry_size);
  if (entry == NULL) {
    return NULL;
  }

  memset (entry, 0, entry_size);

  getcwd(old_cwd, _POSIX_PATH_MAX);
  chdir(sq_datadir);

  while (1) {
    result = NULL;
    retval = readdir_r(dp, entry, &result);
    if (retval != 0 || result == NULL) {
      break;
    }

    stat(entry->d_name, &statbuf);
    if (S_ISREG(statbuf.st_mode)) {

      /* todo: check this string */

      /* we do a magic number check here to make sure we
	 get only databases, not random files in the current directory.
	 SQLite3 databases start with the string:
	 
	 SQLite format 3

      */
      FILE* fp;

      if ((fp = fopen(entry->d_name, "r")) != NULL) {
	char magic_text[16] = "";

	if (fread(magic_text, 1, 15, fp) < 15) {
	  /* either we can't read at all, or the file is too small
	     for a sqlite3 database anyway */
	  fclose(fp);
	  continue;
	}

	/* terminate magic text */
	magic_text[15] = '\0';

	if (strcmp(magic_text, "SQLite format 3")) {
	  /* this file is not meant for us */
	  fclose(fp);
	  continue;
	}

	/* close file again, we're done reading */
	fclose(fp);

	/* match filename to a pattern, or use all found files */
	if (pattern) {
	  if (wild_case_compare(entry->d_name, &entry->d_name[strlen(entry->d_name)], pattern, &pattern[strlen(pattern)], '\\') == 0) {
	    snprintf(sql_command, _POSIX_PATH_MAX+64, "INSERT INTO libdbi_databases VALUES ('%s')", entry->d_name);
	    retval = sqlite3_exec((sqlite3*)(conn->connection), sql_command, NULL, NULL, &sq_errmsg);
	  }
	}
	else {
	  snprintf(sql_command, _POSIX_PATH_MAX+64, "INSERT INTO libdbi_databases VALUES ('%s')", entry->d_name);
	  retval = sqlite3_exec((sqlite3*)(conn->connection), sql_command, NULL, NULL, &sq_errmsg);
	}	  

	if (sq_errmsg) {
	  _dbd_internal_error_handler(conn, sq_errmsg, (const int) retval);
	  free(sq_errmsg);
	  break;
	}
      }
      /* else: we can't read it, so forget about it */
    }
  } /* end while */

  free(entry);
  closedir(dp);
  chdir(old_cwd);

  /* now query our temporary table */
  return dbd_query(conn, "SELECT dbname FROM libdbi_databases");
}

dbi_result_t *dbd_list_tables(dbi_conn_t *conn, const char *db, const char *pattern) {
  /* list tables in a database. The current implementation lists permanent
     tables only, as most applications know about the temporary tables
     they created anyway.
  */
  dbi_result_t *dbi_result;
  dbi_conn_t* tempconn;
  dbi_inst instance;
  int retval;
  char* sq_errmsg = NULL;
  char* sql_cmd;
  dbi_result_t *rs;

  /* this function tries to query a specific database, so we need a
     separate connection to that other database, retrieve the table names,
     and feed them to a temporary table in our main connection */
  instance = dbi_driver_get_instance(dbi_conn_get_driver(conn));
  tempconn = dbi_conn_new_r("sqlite3", instance);

  /* we explicitly cast to (char*) as we discard the "const" thing here */
  dbi_conn_set_option(tempconn, "dbname", (char*)db);
  dbi_conn_set_option(tempconn, "sqlite3_dbdir", (char*)_conn_get_dbdir(conn));

  if (dbi_conn_connect(tempconn) < 0) {
    _dbd_internal_error_handler(conn, NULL, DBI_ERROR_NOCONN);
    dbi_conn_close(tempconn);
    return NULL;
  }
  
  /* create temporary table for table names. The DROP command won't hurt
     if the table doesn't exist yet */
  rs = dbd_query(conn, "DROP TABLE libdbi_tablenames");
  dbi_result_free(rs);
  rs = dbd_query(conn, "CREATE TEMPORARY TABLE libdbi_tablenames (tablename VARCHAR(255))");
  dbi_result_free(rs);
  /*   fprintf(stderr, "created temporary table\n"); */

  /* sqlite3 does not support the SHOW command, so we have to extract the
     information from the accessory sqlite3_master table */
  if (pattern == NULL) {
    asprintf(&sql_cmd, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");
  }
  else {
    asprintf(&sql_cmd, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE '%s' ORDER BY name", pattern);
  }
  dbi_result = dbd_query(tempconn, sql_cmd);
  free(sql_cmd);
  /*   fprintf(stderr, "select from sqlite3_master has run\n"); */
  if (dbi_result) {
    while (dbi_result_next_row(dbi_result)) {
      asprintf(&sql_cmd, "INSERT INTO libdbi_tablenames VALUES ('%s')", dbi_result_get_string(dbi_result, "name"));
      retval = sqlite3_exec((sqlite3*)(conn->connection), sql_cmd, NULL, NULL, &sq_errmsg);
      free(sql_cmd);
      sqlite3_free(sq_errmsg);
    }
    dbi_result_free(dbi_result);
  }
  else {
    dbi_conn_error(tempconn, (const char**)&sq_errmsg);
  }

  //  sqlite3_close((sqlite3*)(tempconn->connection));
  dbi_conn_close(tempconn);

  return dbd_query(conn, "SELECT tablename FROM libdbi_tablenames ORDER BY tablename");
}

size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) {
  /* foo's -> 'foo\'s' */
  size_t len;
	
  strcpy(dest, "'");
  len = sqlite3_escape_string(dest+1, orig, strlen(orig));	
  strcat(dest, "'");
	
  return len+2;
}

size_t dbd_conn_quote_string(dbi_conn_t *conn, const char *orig, char *dest) {
  return dbd_quote_string(conn->driver, orig, dest);
}

size_t dbd_quote_binary(dbi_conn_t *conn, const unsigned char *orig, size_t from_length, unsigned char **ptr_dest) {
  unsigned char *temp;
  size_t len;

  if ((temp = malloc(from_length*2)) == NULL) {
    return 0;
  }

  strcpy((char *)temp, "\'");
  if (from_length) {
    len = _dbd_encode_binary(orig, from_length, temp+1);
  }
  else {
    len = 0;
  }
  strcat((char *)temp, "'");

  *ptr_dest = temp;

  return len+2;
}

dbi_result_t *dbd_query(dbi_conn_t *conn, const char *statement) {
  /* allocate a new dbi_result_t and fill its applicable members:
   * 
   * result_handle, numrows_matched, and numrows_changed.
   * everything else will be filled in by DBI */
	
  dbi_result_t *result;
  int query_res;
  int numrows;
  int numcols;
  char** result_table;
  char* errmsg;
  int idx = 0;
  unsigned short fieldtype;
  unsigned int fieldattribs;
  dbi_error_flag errflag = 0;

  query_res = sqlite3_get_table((sqlite3*)conn->connection,
			       statement,
			       &result_table,
			       &numrows,
			       &numcols,
			       &errmsg);

  if (query_res) {
    return NULL;
  }
	
  result = _dbd_result_create(conn, (void *)result_table, numrows, (unsigned long long)sqlite3_changes((sqlite3*)conn->connection));
/*    printf("numrows:%d, numcols:%d<<\n", numrows, numcols); */
  _dbd_result_set_numfields(result, numcols);

  /* assign types to result */
  while (idx < numcols) {
    int type;
    char *item;
    
    type = find_result_field_types(result_table[idx], conn, statement);
    /*     printf("type: %d<<\n", type); */
    _translate_sqlite3_type(type, &fieldtype, &fieldattribs);

    /* we need the field name without the table name here */
    item = strchr(result_table[idx], (int)'.');
    if (!item) {
      item = result_table[idx];
    }
    else {
      item++;
    }

    _dbd_result_add_field(result, idx, item, fieldtype, fieldattribs);
    idx++;
  }
  
  return result;
}

dbi_result_t *dbd_query_old(dbi_conn_t *conn, const char *statement) {
  /* allocate a new dbi_result_t and fill its applicable members:
   * 
   * result_handle, numrows_matched, and numrows_changed.
   * everything else will be filled in by DBI */
	
  dbi_result_t *result;
  int query_res;
  int numrows;
  int numcols;
  char** result_table;
  char* errmsg;
  int idx = 0;
  unsigned short fieldtype;
  unsigned int fieldattribs;
  dbi_error_flag errflag = 0;

  query_res = sqlite3_get_table((sqlite3*)conn->connection,
				statement,
				&result_table,
				&numrows,
				&numcols,
				&errmsg);

  if (query_res) {
    if(result_table != NULL) {
      sqlite3_free_table(result_table);
    }
    return NULL;
  }
	
  result = _dbd_result_create(conn, (void *)result_table, numrows, (unsigned long long)sqlite3_changes((sqlite3*)conn->connection));
  /*   printf("numrows:%d, numcols:%d<<\n", numrows, numcols); */
  _dbd_result_set_numfields(result, numcols);

  /* assign types to result */
  while (idx < numcols) {
    int type;
    char *item;
    
    type = find_result_field_types(result_table[idx], conn, statement);
    /*     printf("type: %d<<\n", type); */
    _translate_sqlite3_type(type, &fieldtype, &fieldattribs);

    /* we need the field name without the table name here */
    item = strchr(result_table[idx], (int)'.');
    if (!item) {
      item = result_table[idx];
    }
    else {
      item++;
    }

    _dbd_result_add_field(result, idx, item, fieldtype, fieldattribs);
    idx++;
  }
  
  return result;
}

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const unsigned char *statement, size_t st_length) {
  /* todo: implement using sqlite3_prepare and friends */
  return NULL;
}

int dbd_transaction_begin(dbi_conn_t *conn) {
  if (dbd_query(conn, "BEGIN") == NULL) {
    return 1;
  }
  else {
    return 0;
  }
}

int dbd_transaction_commit(dbi_conn_t *conn) {
  if (dbd_query(conn, "COMMIT") == NULL) {
    return 1;
  }
  else {
    return 0;
  }
}

int dbd_transaction_rollback(dbi_conn_t *conn) {
  if (dbd_query(conn, "ROLLBACK") == NULL) {
    return 1;
  }
  else {
    return 0;
  }
}

int dbd_savepoint(dbi_conn_t *conn, const char *savepoint) {
  char* query;

  if (!savepoint) {
    return 1;
  }

  asprintf(&query, "SAVEPOINT %s", savepoint);

  if (dbd_query(conn, query) == NULL) {
    free(query);
    return 1;
  }
  else {
    free(query);
    return 0;
  }
}

int dbd_rollback_to_savepoint(dbi_conn_t *conn, const char *savepoint) {
  char* query;

  if (!savepoint) {
    return 1;
  }

  asprintf(&query, "ROLLBACK TO SAVEPOINT %s", savepoint);

  if (dbd_query(conn, query) == NULL) {
    free(query);
    return 1;
  }
  else {
    free(query);
    return 0;
  }
}

int dbd_release_savepoint(dbi_conn_t *conn, const char *savepoint) {
  char* query;

  if (!savepoint) {
    return 1;
  }

  asprintf(&query, "RELEASE SAVEPOINT %s", savepoint);

  if (dbd_query(conn, query) == NULL) {
    free(query);
    return 1;
  }
  else {
    free(query);
    return 0;
  }
}

static int find_result_field_types(char* field, dbi_conn_t *conn, const char* statement) {

  /*
    field is the name of the field which we want to know the type of
    conn is the connection
    statement is the query string

    returns the type as a FIELD_TYPE_XXX value

    sqlite3 uses a type system insufficient for libdbi. You don't
    even have to declare the column types if you don't want to.

    However, sqlite3 stores the types as used in the CREATE TABLE
    commands and makes them available through the table_info
    pragma. It is a VERY GOOD idea to declare the types if we want
    the following to work

    The code assumes that table and field names do not exceed a given
    length limit. PostgreSQL uses 32 which is a bit low. Sqlite3 does
    not seem to have fixed limits. We use a default limit of 128 here
    which can be increased in dbd_sqlite3.h if need arises.
  */

  int curr_table_index;
  char* statement_copy = strdup(statement);
  char* item;
  char curr_field[MAX_IDENT_LENGTH];
  char curr_field_lower[MAX_IDENT_LENGTH];
  char curr_table[MAX_IDENT_LENGTH];
  char* tables[MAX_TABLES_IN_QUERY];
  int table_count = 0;
  int type;
  int counter;

  item = strchr(field, (int)'.');
  if ( !item ) {
    strcpy(curr_field, field);
    strcpy(curr_table, "");
  }
  else {
    strcpy(curr_field, item+1);
    strncpy(curr_table, field, item-field);
    curr_table[item-field] = '\0';
  }
  //printf("table = %s\ncolumn = %s\n",curr_table,curr_field);

  /* If curr_table is empty, this means we have to get the
     select tables from the statement (it is possible there is more than one),
     otherwise we have the table for this field.
     It would seem that even if the table is aliased in the statement,
     we still have the original table name.
     sqlite3_get_table returns the tablename and not the alias when returning table.column.
     It probably isn't a good idea to rely on this, but we will.

     I knew it wasn't a good idea :( Select using distinct breaks the above assumptions.
     now we have to resolve the table name as well (if it is given). */

  /* We have to assume that curr_table is an alias.
     This call will resolve the curr_table if given
     */
  //printf("curr_table before getTables = %s\n",curr_table);
  table_count = getTables(tables,0,statement_copy,curr_table);
  //printf("curr_table after getTables = %s\n",curr_table);
  //printf("%s\n",statement_copy);
  //printf("*********TABLELIST************\n");
  //for ( counter = 0 ; counter < table_count ; counter++) {
  //   printf("[%i] %s\n",counter,tables[counter]);
  //}

  // resolve our curr_field to a real column
  char* token;
  char* saveptr;
  char* itemstore;
  int as_flag = 0;
  int function_flag = 0;
  int expression_flag = 0;
  int from_flag = 0;

  token = strtok_r(statement_copy, " ,;", &saveptr);
  while( token != NULL ) {
    //printf("checking %s\n",token);
    // check to see if there is a tablename on this field
    item = strchr(token, (int)'.');
    if ( item != NULL ) {
      // discard the tablename
      token = item+1;
      //printf("checking %s\n",token);
    }
    /* if the from flag is set, we're not interested in any tokens
     * until we hit another select.
     */
    if ( from_flag == 1 ) {
      if( strcmp(token,"(select") == 0 ||
	  strcmp(token,"(SELECT" ) == 0 ||
	  strcmp(token,"select") == 0 ||
	  strcmp(token,"SELECT" ) == 0
	  ) {
	from_flag = 0;
      }
    }
    else {
      if ( as_flag == 0 ) {
	if( strcmp(token,"as") == 0 || strcmp(token,"AS" ) == 0 ) {
	  as_flag = 1;
	}
	else {
	  // reset function and expression flags
	  function_flag = 0;
	  expression_flag = 0;
	  itemstore = token;
	  // check if this is a function
	  item = strchr(itemstore,'(');
	  if ( item != NULL ) {
	    if ( item == itemstore ) {
	      /* I started to try to parse for this scenario, and
		 realized that I was descending into a pit of parsing
		 that would never end.  if it turns out that our
		 curr_field aliases something that comes after this
		 case it will more than likely be wrong and the end
		 result is the field type will resolve to string.

		 so we have to create a rule for creating sql
		 statements with this case and document it so users
		 know how to form their statements and know how to
		 receive their results

		 The rule here is that if you want to enclose your
		 result field in brackets, you must have an alias for
		 it, and the 'as' must be spaced from the closing
		 bracket.  e.g. (<some expression that could be
		 anything really>) as <alias>

		 The result will be obtainable as string, period.

		 if this is the case then this field is enclosed in brackets
		 check for the closing bracket in this token */

	      expression_flag = 1;
	      int opens = 1;
	      while ( opens > 0 && *item != '\0' ) {
		item++;
		if ( *item == '(' )
		  opens++;
		if ( *item == ')' )
		  opens--;
	      }
	      if ( opens > 0 ) {
		// this token doesn't have the complete field
		// get the next token etc...
		int field_complete = 0;
		while ( field_complete == 0 ) {
		  token = strtok_r(NULL, " ,;", &saveptr);
		  item = token;
		  while ( opens > 0 && *item != '\0' ) {
		    if ( *item == '(' )
		      opens++;
		    if ( *item == ')' )
		      opens--;
		    item++;
		  }
		  if ( opens == 0 ) {
		    field_complete = 1;
		  }
		}
	      }
	    } // if item == itemstore

	    else {
	      // we have a function here
	      /*
	       * As for expressions we need a documented rule for
	       * having a function as a result column.  The opening
	       * bracket should be attached to the function name, e.g
	       * count(, not count ( the function must be aliased and
	       * the 'as' must be spaced from the closing bracket. e.g
	       * <function>( <function parameters> ) as <alias>
	       */
	      function_flag = 1;
	      int opens = 1;
	      while ( opens > 0 && *item != '\0' ) {
		item++;
		if ( *item == '(' )
		  opens++;
		if ( *item == ')' )
		  opens--;
	      }
	      if ( opens > 0 ) {
		// this token doesn't have the complete function
		// get the next token etc...
		int function_complete = 0;
		while ( function_complete == 0 ) {
		  token = strtok_r(NULL, " ,;", &saveptr);
		  item = token;
		  while ( opens > 0 && *item != '\0' ) {
		    if ( *item == '(' )
		      opens++;
		    if ( *item == ')' )
		      opens--;
		    item++;
		  }
		  if ( opens == 0 ) {
		    function_complete = 1;
		  }
		}
	      }
	    }
	  }
	  if ( strcmp(token, "from") == 0 || strcmp(token, "FROM") == 0 ) {
	    from_flag = 1;
	  }
	}
      }
      else {
	if ( strcmp(token,curr_field) == 0 ) {
	  // our curr_field is an alias for the field in itemstore
	  // if the expresion flag is set we know that the field type is string
	  if ( expression_flag == 1 ) {
	    free(statement_copy);
	    return FIELD_TYPE_STRING;
	  }
	  if ( function_flag == 1 ) {
	    // itemstore has at least the functionname( in it
	    strcpy(curr_field,itemstore);
	    strcpy(curr_field_lower, curr_field);
	    item = curr_field_lower;
	    while (*item) {
	      *item = (char)tolower((int)*item);
	      item++;
	    }
            //printf("Field is a function - %s\n",curr_field_lower);
	    free(statement_copy);
	    if ( strstr(curr_field_lower,"avg(") ||
		 strstr(curr_field_lower,"sum(") ||
		 strstr(curr_field_lower,"total(") ||
		 strstr(curr_field_lower,"abs(") ||
		 strstr(curr_field_lower,"round(") ) {
	      return FIELD_TYPE_FLOAT;
	    }
	    if ( strstr(curr_field_lower,"julianday(") ||
		 strstr(curr_field_lower,"count(") ||
		 strstr(curr_field_lower,"max(") ||
		 strstr(curr_field_lower,"min(") ||
		 strstr(curr_field_lower,"last_insert_rowid(") ||
		 strstr(curr_field_lower,"length(") ) {
	      return FIELD_TYPE_LONG;
	    }
	    if ( strstr(curr_field_lower,"random(") ) {
	      return FIELD_TYPE_LONGLONG;
	    }
	    if ( strstr(curr_field_lower,"randomblob(") ||
		 strstr(curr_field_lower,"zeroblob(") ||
		 strstr(curr_field_lower,"total(") ||
		 strstr(curr_field_lower,"abs(") ||
		 strstr(curr_field_lower,"round(") ) {
	      return FIELD_TYPE_BLOB;
	    }
	    if ( strstr(curr_field_lower,"date(") ||
		 strstr(curr_field_lower,"time(") ||
		 strstr(curr_field_lower,"datetime(") ||
		 strstr(curr_field_lower,"strftime(") ||
		 strstr(curr_field_lower,"group_concat(") ||
		 strstr(curr_field_lower,"coalesce(") ||
		 strstr(curr_field_lower,"glob(") ||
		 strstr(curr_field_lower,"ifnull(") ||
		 strstr(curr_field_lower,"hex(") ||
		 strstr(curr_field_lower,"like(") ||
		 strstr(curr_field_lower,"lower(") ||
		 strstr(curr_field_lower,"ltrim(") ||
		 strstr(curr_field_lower,"nullif(") ||
		 strstr(curr_field_lower,"quote(") ||
		 strstr(curr_field_lower,"replace(") ||
		 strstr(curr_field_lower,"rtrim(") ||
		 strstr(curr_field_lower,"sqlite_version(") ||
		 strstr(curr_field_lower,"substr(") ||
		 strstr(curr_field_lower,"trim(") ||
		 strstr(curr_field_lower,"typeof(") ||
		 strstr(curr_field_lower,"upper(") ) {
	      return FIELD_TYPE_STRING;
	    }
	    // if we get here we have a function we don't know
	    free(statement_copy);
	    return FIELD_TYPE_STRING;
	  }
	  item = strchr(itemstore,'.');
	  if ( item != NULL ) {
	    strcpy(curr_field,item+1);
	  }
	  else {
	    strcpy(curr_field,itemstore);
	  }
	}
	as_flag = 0;
      }
    }
    token = strtok_r(NULL, " ,;", &saveptr);
  }
  //printf("table = %s\ncolumn = %s\n",curr_table,curr_field);
  free(statement_copy);

  /* now we have to look for the field type in the curr_table
   * If curr_table is empty, we have to search through the table list
   */
  char sql_command[MAX_IDENT_LENGTH+80];
  char **table_result_table;
  char *curr_type = NULL;
  char* errmsg;
  int query_res;
  int table_numrows = 0;
  int table_numcols = 0;
  dbi_error_flag errflag = 0;

  if ( strlen(curr_table) > 0 ) {
    snprintf(sql_command, MAX_IDENT_LENGTH+80, "PRAGMA table_info(%s)", curr_table);
    query_res = sqlite3_get_table((sqlite3*)conn->connection,
				  sql_command,
				  &table_result_table,
				  &table_numrows,
				  &table_numcols,
				  &errmsg);

    if (query_res || !table_numrows) {
      /* The table we have doesn't seem to exist in the database!
       * fallback to to string
       */
      //printf("singletable unknown !\n");
      return FIELD_TYPE_STRING;
    }
    curr_type = get_field_type(&table_result_table, curr_field, table_numrows);
    sqlite3_free_table(table_result_table);
    if (!curr_type) {
      /* the field was not found in the table!
       * fallback to string
       */
      //printf("field not in singletable !\n");
      return FIELD_TYPE_STRING;
    }
  }
  else {
    /* process the table list
     * It should be noted here that we stop searching the tables on the
     * first match of the curr_field from the list of tables
     * The reasoning here is that fields with the same name will
     * probably be the same type.  Obviously, this is a hole.
     */
    if ( table_count > 0 ) {

      for ( counter = 0 ; counter < table_count ; counter++ ) {
/* 	printf("searching table %s\n",tables[counter]); */
	snprintf(sql_command, MAX_IDENT_LENGTH+80, "PRAGMA table_info(%s)", tables[counter]);
	query_res = sqlite3_get_table((sqlite3*)conn->connection,
				      sql_command,
				      &table_result_table,
				      &table_numrows,
				      &table_numcols,
				      &errmsg);

	if (query_res || !table_numrows) {
	  /* This table doesn't seem to exist in the database!
	   * fallback to to string
	   */
/* 	  printf("table not found\n"); */
	  // continue processing
	}
	else {
	  curr_type = get_field_type(&table_result_table, curr_field, table_numrows);
	  sqlite3_free_table(table_result_table);
	  if (!curr_type) {
	    /* the field was not found in this table!
	     * fallback to string
	     */
	    // continue processing
	  }
	}
	if ( curr_type )
	  break;
      }
      if (!curr_type) {
	/* the field was not found in any of the tables!
	 * fallback to string
	 */
/* 	printf("field not in any table !\n"); */
	return FIELD_TYPE_STRING;
      }
    }
    else {
      /* no tables in the statement ?!
       * fallback to string
       */
      printf("no tables in statement !\n");
      return FIELD_TYPE_STRING;
    }
  }

  freeTables(tables, table_count);

  /* convert type to uppercase, reuse item */
  item = curr_type;
  while (*item) {
    *item = (char)toupper((int)*item);
    item++;
  }

  /* the following code tries to support as many of the SQL types as
     possible, including those extensions supported by MySQL and
     PostgreSQL. Some conflicts remain, like the REAL type which is a
     different thing in MySQL and PostgreSQL */

/*      printf("field type before type assignment: %s<<\n", curr_type);  */
  if (strstr(curr_type, "CHAR(") /* note the opening bracket */
      || strstr(curr_type, "CLOB")
      || strstr(curr_type, "TEXT") /* also catches TINYTEXT */
      || strstr(curr_type, "VARCHAR")
      || strstr(curr_type, "ENUM")
      || strstr(curr_type, "SET")
      || strstr(curr_type, "YEAR")) { /* MySQL 2 or 4 digit year (string) */
    type = FIELD_TYPE_STRING;
  }
  else if (strstr(curr_type, "BLOB")
	   || strstr(curr_type, "BYTEA")) {
    type = FIELD_TYPE_BLOB;
  }
  else if (strstr(curr_type, "CHAR") /* this is a 1-byte value */
	   || strstr(curr_type, "TINYINT")
	   || strstr(curr_type, "INT1")) {
    type = FIELD_TYPE_TINY;
  }
  else if (strstr(curr_type, "SMALLINT")
	   || strstr(curr_type, "INT2")) {
    type = FIELD_TYPE_SHORT;
  }
  else if (strstr(curr_type, "MEDIUMINT")) {
    type = FIELD_TYPE_INT24;
  }
  else if (strstr(curr_type, "BIGINT")
	   || strstr(curr_type, "INTEGER PRIMARY KEY") /* BAD BAD HACK */
	   || strstr(curr_type, "INT8")) {
    type = FIELD_TYPE_LONGLONG;
  }
  else if (strstr(curr_type, "INTEGER")
	   || strstr(curr_type, "INT")
	   || strstr(curr_type, "INT4")) {
    type = FIELD_TYPE_LONG;
  }
  else if (strstr(curr_type, "DECIMAL") ||
	   strstr(curr_type, "NUMERIC")) {
    type = FIELD_TYPE_DECIMAL;
  }
  else if (strstr(curr_type, "TIMESTAMP")
	   || strstr(curr_type, "DATETIME")) {
    type = FIELD_TYPE_TIMESTAMP;
  }
  else if (strstr(curr_type, "DATE")) {
    type = FIELD_TYPE_DATE;
  }
  else if (strstr(curr_type, "TIME")) {
    type = FIELD_TYPE_TIME;
  }
  else if (strstr(curr_type, "DOUBLE") /* also catches "double precision" */
	   || strstr(curr_type, "FLOAT8")) {
    type = FIELD_TYPE_DOUBLE;
  }
  else if (strstr(curr_type, "REAL") /* this is PostgreSQL "real", not
					MySQL "real" which is a
					synonym of "double" */
	   || strstr(curr_type, "FLOAT")
	   || strstr(curr_type, "FLOAT4")) {
    type = FIELD_TYPE_FLOAT;
  }
  else {
    type = FIELD_TYPE_STRING; /* most reasonable default */
  }

  free(curr_type);
/*   printf("GET FIELD TYPE RETURNS %d !\n",type); */
  return type;
}

/* Similar to C99 strstr() except ensures that there is
 * white space around needle. */
char *strstr_ws(const char *haystack, const char *needle){
   const char *c = NULL;
   int len;

   len = strlen(needle);

   c = haystack;

   while( (c = strstr(c,needle)) != NULL){
      if(c == haystack) return NULL;
      if( ( *(c-1)  == ' ' || *(c-1) == '\t' || *(c-1) == '\n')
         &&
          ( c[len] == ' '  || c[len] == '\t' || c[len] == '\n'))
           return (char*) c;
   }

   return NULL;
}

static int getTables(char** tables, int index, const char* statement, char* curr_table) {
  //printf("getTables\n");
/*   printf("processing %s\n",statement); */
  char* item;
  char* start;
  int join_flag = 0;
  int as_flag = 0;
  int not_word_flag = 0;

  // the table list will start after 'from' and finish at the last occurrence of
  // 'where' | 'group' | 'having' | 'union' | 'intersect' | 'except' | 'order' | 'limit'

  char* endwords[] = {"where","group","having","union","intersect","except","order","limit"};
  char* nottables[] = {"natural","left","right","full","outer","inner","cross","join","as","on"};

  if ( !(item = strstr_ws(statement, "from")) ) {
    if ( !(item = strstr_ws(statement, "FROM")) )
/*       printf("no from clause\n"); */
      return index;
  }
  item += strlen("from");

  while ( *item != '\0' ) {
/*     printf("begin parsing at %s\n", item); */
    if ( *item == ' '|| *item == '\t' || *item == ',' ) {
      item++;
    }
    else {
/*       printf("word start at %s\n", item); */
      start = item; // mark the start of the word
      if ( *item == '(' ) {
	//printf("sub select\n");
	int opens = 1;
	while ( opens > 0 ) {
	  item++;
	  if ( *item == '(' )
	    opens++;
	  if ( *item == ')' )
	    opens--;
	}
	char substatement[item-start];
	strncpy(substatement,start+1,item-(start+1));
	substatement[item-(start+1)] = '\0';
	index = getTables(tables,index,substatement,curr_table);
	//printf("index is at %d\n",index);
	item ++;
      }
      else {
/* 	printf("actual word starting here: %s\n", item); */
	while ( *item && *item != ',' && *item != ' ' && *item != '\t'
                                      && *item != ')' && *item != ';' ) {
	  item++;
	}
	char word[item-start+1];
	char word_lower[item-start+1];
	strncpy(word,start,item-start);
	word[item-start] = '\0';
	strncpy(word_lower,start,item-start);
	word_lower[item-start] = '\0';
	int i = 0;
	while (word_lower[i]) {
	  word_lower[i] = tolower(word_lower[i]);
	  i++;
	}
	// if word is an end word we can return
	for ( i = 0 ; i < (sizeof(endwords)/sizeof *(endwords)) ; i++ ) {
	  if ( strcmp(endwords[i],word_lower) == 0 ) {
/* 	    printf("end word!\n"); */
	    return index;
	  }
	}
	// if word is not a table we ignore it and continue
	for ( i = 0 ; i < (sizeof(nottables)/sizeof *(nottables)) ; i++ ) {
	  if ( strcmp(nottables[i],word_lower) == 0 ) {
/* 	    printf("not a table: %s\n", word_lower); */
	    // if we encounter join or as we set
	    // a flag because we know what to do next
	    if ( strcmp("join",word_lower) == 0 ) {
	      //printf("join found\n");
	      join_flag = 1;
	    }
	    if ( strcmp("as",word_lower) == 0 ) {
	      //printf("as found\n");
	      as_flag = 1;
	    }
	    not_word_flag = 1;
	    break;
	  }
	}
	if ( not_word_flag == 1) {
/* 	  printf("skipping word\n"); */
	  not_word_flag = 0;
	}
	else {
	  if ( as_flag == 1) {
            // if this word matches what is currently in curr_table
	    // then curr_table is an alias for the last found table
	    //printf("++++ AS FLAG ++++ curr_table = %s , word = %s\n",curr_table,word);
	    if ( strcmp(curr_table,word) == 0 ) {
/* 	      printf("Setting curr_table\n",curr_table,word); */
	      strcpy(curr_table,tables[index - 1]);
            }
	    as_flag = 0;
	    //printf("++++ AS FLAG ++++ curr_table set to %s\n",curr_table);
	  }
	  else {
/* 	    printf("found table!\n"); */
	    // if we get here the word is a table name
	    tables[index] = strdup(word);
/* 	    printf("table index %d = %s\n",index,tables[index]); */
	    index++;
	    if ( join_flag == 1) {
	      //printf("skipping after joined table\n");
	      // we can ignore everything until the next ',' or 'join'
	      join_flag = 0;
	      int skip_flag = 1;
	      while ( skip_flag == 1 ) {
		if ( *item == ' '|| *item == '\t' ) {
		  item++;
		}
		else {
		  start = item; // mark the start of the word
		  // this will skip over the using (id-list)
		  if ( *item == '(' ) {
		    //printf("skip over the using (id-list)\n");
		    int opens = 1;
		    while ( opens > 0 ) {
		      item++;
		      if ( *item == '(' )
			opens++;
		      if ( *item == ')' )
			opens--;
		    }
		  }
		  while ( *item && *item != ','  && *item != ' '
                                && *item != '\t' && *item != '(') {
		    item++;
		  }
		  if ( *item == '\0' ) {
/* 		    printf("returning %d, *item went to NULL1\n",index); */
		    return index;
		  }
		  if ( *item == ',') {
		    //printf("stop skip after comma\n");
		    // we have come to a comma, so we can stop skipping
		    skip_flag = 0;
		    break;
		  }

		  word_lower[item-start+1];
		  strncpy(word_lower,start,item-start);
		  word_lower[item-start] = '\0';
		  int i = 0;
		  while (word_lower[i]) {
		    word_lower[i] = tolower(word_lower[i]);
		    i++;
		  }
		  if ( strcmp("join",word_lower) == 0 ) {
		    //printf("stop skip after join found\n");
		    // we have found the next join, stop skipping
		    join_flag = 1;
		    skip_flag = 0;
		    break;
		  }
                  for ( i = 0 ; i < (sizeof(endwords)/sizeof *(endwords)) ; i++ ) {
                     if ( strcmp(endwords[i],word_lower) == 0 ) {
                        /* printf("end word!\n"); */
                        return index;
                    }
                  }
		}
	      }
	    } // if join_flag
	  } // if as_flag else
	} // if not_word_flag else
	if ( *item == '\0' ) {
/* 	  printf("returning %d",index); */
	  return index;
	}
        item++;
      } // if *item == '(' else
    } // if *item == ' '|| *item == '\t' || *item == ',' else
  } // while
/*   printf("returning %d\n",index); */
  return index;
}

static void freeTables(char** tables, int table_count) {
  int counter;

  for ( counter = 0 ; counter < table_count ; counter++ ) {
    if (*(tables[counter])) {
      free(tables[counter]);
    }
  }
}

static char* get_field_type(char*** ptr_result_table, const char* curr_field_name, int numrows) {
  /*
    ptr_table is a ptr to a string array as returned by the
    sqlite3_get_table() function called with the table_info pragma.
    The array is 6 cols wide and contains one row for each field in
    the table. The first row contains the column names, but it is not
    included in numrows, so the real data start at [6].  The columns
    are: Number|column_name|type|may_be_null|default_value|pk?  Thus,
    the column names are in [6+6*i], and the corresponding type of the
    result is in [7+6*i]. pk is set to 1 if the column is part of a
    primary key. An autoincrementing column is identified by type
    INTEGER and by being the only column with pk set to 1 in this
    table
    curr_field_name is a ptr to a string holding the field name
    numrows is the number of rows in the string array

    returns the field type as an allocated string or NULL
    if an error occurred
  */
  char* curr_type = NULL;
  int i;
  int pk_count = 0;

  for (i=6;i<=numrows*6;i+=6) {
    if (!strcmp((*ptr_result_table)[i+1], curr_field_name)) {
      curr_type = strdup((*ptr_result_table)[i+2]);
    }
    if (strcmp((*ptr_result_table)[i+5], "1") == 0) {
      pk_count++;
    }
  }
/*   printf("curr_type of %s went to %s<<\n", curr_field_name, curr_type); */

  /* sqlite has the bad habit to turn INTEGER PRIMARY KEY columns into
     INTEGER columns when using the table_info pragma. If a column is
     an INTEGER and the only column with pk set to "1", turn it back
     to INTEGER PRIMARY KEY */

  if (curr_type) {
    if (pk_count == 1
	&& (strcmp(curr_type, "INTEGER") == 0
	    || strcmp(curr_type, "integer") == 0)) {
      free(curr_type);
      curr_type = strdup("INTEGER PRIMARY KEY");
    }
  }
  return curr_type;
}

const char *dbd_select_db(dbi_conn_t *conn, const char *db) {
  /*
    sqlite3 does not separate connecting to a database server and using
    or opening a database. If we want to switch to a different database,
    we have to drop the current connection and create a new one
    instead, using the new database.
  */

  if (!db || !*db) {
    return NULL;
  }

  if (conn->connection) {
    sqlite3_close((sqlite3 *)conn->connection);
  }

  if (_real_dbd_connect(conn, db)) {
    return NULL;
  }

  return db;
}

int dbd_geterror(dbi_conn_t *conn, int *err_no, char **errstr) {
  /* put error number into err_no, error string into errstr
   * return 0 if error, 1 if err_no filled, 2 if errstr filled, 3 if both err_no and errstr filled */

  *err_no = sqlite3_errcode((sqlite3 *)conn->connection);
  *errstr = strdup((char*)sqlite3_errmsg((sqlite3 *)conn->connection));
  return 3;
}


unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) {
  return (unsigned long long)sqlite3_last_insert_rowid((sqlite3*)conn->connection);
}

unsigned long long dbd_get_seq_next(dbi_conn_t *conn, const char *sequence) {
  _dbd_internal_error_handler(conn, NULL, DBI_ERROR_UNSUPPORTED);
  return 0;
}

int dbd_ping(dbi_conn_t *conn) {

  if (dbd_query(conn, "SELECT 1") == NULL) {
    return 0;
  }
  else {
    return 1;
  }
}

/* CORE SQLITE3 DATA FETCHING STUFF */

static void _translate_sqlite3_type(enum enum_field_types fieldtype, unsigned short *type, unsigned int *attribs) {
  unsigned int _type = 0;
  unsigned int _attribs = 0;
/*   printf("fieldtype:%d<<\n", fieldtype); */
  switch (fieldtype) {
  case FIELD_TYPE_TINY:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE1;
    break;
  case FIELD_TYPE_YEAR:
    _attribs |= DBI_INTEGER_UNSIGNED;
  case FIELD_TYPE_SHORT:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE2;
    break;
  case FIELD_TYPE_INT24:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE3;
    break;
  case FIELD_TYPE_LONG:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE4;
    break;
  case FIELD_TYPE_LONGLONG:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE8;
    break;
			
  case FIELD_TYPE_FLOAT:
    _type = DBI_TYPE_DECIMAL;
    _attribs |= DBI_DECIMAL_SIZE4;
    break;
  case FIELD_TYPE_DOUBLE:
    _type = DBI_TYPE_DECIMAL;
    _attribs |= DBI_DECIMAL_SIZE8;
    break;
			
  case FIELD_TYPE_DATE: /* TODO parse n stuph to native DBI unixtime type. for now, string */
    _type = DBI_TYPE_DATETIME;
    _attribs |= DBI_DATETIME_DATE;
    break;
  case FIELD_TYPE_TIME:
    _type = DBI_TYPE_DATETIME;
    _attribs |= DBI_DATETIME_TIME;
    break;
  case FIELD_TYPE_DATETIME:
  case FIELD_TYPE_TIMESTAMP:
    _type = DBI_TYPE_DATETIME;
    _attribs |= DBI_DATETIME_DATE;
    _attribs |= DBI_DATETIME_TIME;
    break;
			
  case FIELD_TYPE_DECIMAL: /* decimal is actually a string, has arbitrary precision, no floating point rounding */
  case FIELD_TYPE_ENUM:
  case FIELD_TYPE_SET:
  case FIELD_TYPE_VAR_STRING:
  case FIELD_TYPE_STRING:
    _type = DBI_TYPE_STRING;
    break;
			
  case FIELD_TYPE_TINY_BLOB:
  case FIELD_TYPE_MEDIUM_BLOB:
  case FIELD_TYPE_LONG_BLOB:
  case FIELD_TYPE_BLOB:
    _type = DBI_TYPE_BINARY;
    break;
			
  default:
    _type = DBI_TYPE_STRING;
    break;
  }
	
  *type = _type;
  *attribs = _attribs;
}


static void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx) {
  char **result_table = result->result_handle;
  
  unsigned int curfield = 0;
  char *raw = NULL;
  unsigned int sizeattrib;
  dbi_data_t *data;

  while (curfield < result->numfields) {
    /* rowidx appears to be 0-based, but the first row always contains
       the column names */
    raw = result_table[curfield + ((rowidx+1)*result->numfields)];
    data = &row->field_values[curfield];
    
    row->field_sizes[curfield] = 0;
    /* this will be set to the string size later on if the field is indeed a string */

    if (raw == NULL) { /* no data available */
      _set_field_flag( row, curfield, DBI_VALUE_NULL, 1);
      curfield++;
      continue;
    }
    
    switch (result->field_types[curfield]) {
    case DBI_TYPE_INTEGER:
      sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_INTEGER_SIZE1, DBI_INTEGER_SIZE8);
      switch (sizeattrib) {
      case DBI_INTEGER_SIZE1:
	data->d_char = (char) atol(raw); break;
      case DBI_INTEGER_SIZE2:
	data->d_short = (short) atol(raw); break;
      case DBI_INTEGER_SIZE3:
      case DBI_INTEGER_SIZE4:
/* 	printf("returning a long via data->d_long\n"); */
	data->d_long = (int) atol(raw); break;
      case DBI_INTEGER_SIZE8:
/* 	printf("returning a long long via data->d_longlong\n"); */
	data->d_longlong = (long long) atoll(raw); break; /* hah, wonder if that'll work */
      default:
	break;
      }
      break;
    case DBI_TYPE_DECIMAL:
      sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_DECIMAL_SIZE4, DBI_DECIMAL_SIZE8);
      switch (sizeattrib) {
      case DBI_DECIMAL_SIZE4:
	data->d_float = (float) strtod(raw, NULL); break;
      case DBI_DECIMAL_SIZE8:
	data->d_double = (double) strtod(raw, NULL); break;
      default:
	break;
      }
      break;
    case DBI_TYPE_STRING:
      data->d_string = strdup(raw);
      row->field_sizes[curfield] = strlen(raw);
      break;
    case DBI_TYPE_BINARY:
      data->d_string = strdup(raw);
      row->field_sizes[curfield] = _dbd_decode_binary(data->d_string, data->d_string);
      break;
    case DBI_TYPE_DATETIME:
      sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_DATETIME_DATE, DBI_DATETIME_TIME);
      data->d_datetime = _dbd_parse_datetime(raw, sizeattrib);
      break;
      
    default:
      data->d_string = strdup(raw);
      row->field_sizes[curfield] = strlen(raw);
      break;
    }
    
    curfield++;
  }
}

/* this function is stolen from MySQL and somewhat simplified for our
   needs */
/* it appears to return 0 on a match, 1 if no match is found, and -1
   in some odd cases */

#define wild_many (char)'%'
#define wild_one (char)'_'
#define INC_PTR(A,B) A++

static int wild_case_compare(const char *str,const char *str_end,
		      const char *wildstr,const char *wildend,
		      char escape) {
  int result= -1;				// Not found, using wildcards
  unsigned char cmp;

  while (wildstr != wildend) {
    while (*wildstr != wild_many && *wildstr != wild_one) {
      if (*wildstr == escape && wildstr+1 != wildend) {
	wildstr++;
      }
      if (str == str_end || *wildstr++ != *str++) {
	return(1);				// No match
      }
      if (wildstr == wildend) {
	return (str != str_end);		// Match if both are at end
      }
      result=1;					// Found an anchor char
    }
    if (*wildstr == wild_one)	{
      do {
	if (str == str_end) {			// Skip one char if possible
	  return (result);
	}
	INC_PTR(str,str_end);
      } while (++wildstr < wildend && *wildstr == wild_one);
      if (wildstr == wildend) {
	break;
      }
    }

    if (*wildstr == wild_many) {		// Found wild_many
      wildstr++;
      /* Remove any '%' and '_' from the wild search string */
      for ( ; wildstr != wildend ; wildstr++) {
	if (*wildstr == wild_many) {
	  continue;
	}
	if (*wildstr == wild_one) {
	  if (str == str_end) {
	    return (-1);
	  }
	  INC_PTR(str,str_end);
	  continue;
	}
	break;					// Not a wild character
      }
      if (wildstr == wildend) {
	return(0);				// Ok if wild_many is last
      }
      if (str == str_end) {
	return -1;
      }

      if ((cmp= *wildstr) == escape && wildstr+1 != wildend) {
	cmp= *++wildstr;
      }
      INC_PTR(wildstr,wildend);			// This is compared trough cmp
      /*        cmp=likeconv(cmp);    */
      do {
	while (str != str_end && *str != cmp) {
	  str++;
	}
	if (str++ == str_end) {
	  return (-1);
	}

	{
	  int tmp=wild_case_compare(str,str_end,wildstr,wildend,escape);
	  if (tmp <= 0) {
	    return (tmp);
	  }
	}
      } while (str != str_end && wildstr[0] != wild_many);
      return(-1);
    }
  }
  return (str != str_end ? 1 : 0);
}

/* this function is stolen from MySQL. The quoting was changed to the
   SQL standard, i.e. single and double quotes are escaped by doubling,
   not by a backslash. Newlines and carriage returns are left alone */
static size_t sqlite3_escape_string(char *to, const char *from, size_t length)
{
  const char *to_start=to;
  const char *end;

  for (end=from+length; from != end ; from++)
    {
      switch (*from) {
      case 0:				/* Must be escaped for 'mysql' */
	*to++= '\\';
	*to++= '0';
	break;
      case '\'':
	*to++= '\''; /* double single quote */
	*to++= '\'';
	break;
      case '\032':			/* This gives problems on Win32 */
	*to++= '\\';
	*to++= 'Z';
	break;
      default:
	*to++= *from;
      }
    }
  *to=0;
  return (size_t) (to-to_start);
}

/* this is a convenience function to retrieve the database directory */
static const char* _conn_get_dbdir(dbi_conn_t *conn) {
  const char* dbdir;

  dbdir = dbi_conn_get_option(conn, "sqlite3_dbdir");
	
  if (!dbdir) {
    /* use default directory instead */
    dbdir = default_dbdir;
  }

  return dbdir;
}
