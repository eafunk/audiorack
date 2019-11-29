/*
 * libdbi-drivers - 3rd-party drivers for libdbi, a database independent
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
 * dbd_sqlite.c: SQLite database support (using libsqlite)
 * Copyright (C) 2002-2007, Markus Hoenicka <mhoenicka@users.sourceforge.net>
 * http://libdbi-drivers.sourceforge.net
 * 
 * $Id: dbd_sqlite.c,v 1.55 2013/01/09 21:30:20 mhoenicka Exp $
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE /* we need asprintf */

/* this is defined by the Makefile and passed via -D */
/* #define DBDIR /usr/local/var/lib/libdbi/sqlite */

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

#include <sqlite.h>
#include "dbd_sqlite.h"

static const dbi_info_t driver_info = {
  "sqlite",
  "SQLite database support (using libsqlite)",
  "Markus Hoenicka <mhoenicka@users.sourceforge.net>",
  "http://libdbi-drivers.sourceforge.net",
  "dbd_sqlite v" VERSION,
  __DATE__
};

static const char *custom_functions[] = SQLITE_CUSTOM_FUNCTIONS;
static const char *reserved_words[] = SQLITE_RESERVED_WORDS;
static const char default_dbdir[] = DBDIR;

/* the encoding strings */
static const char sqlite_encoding_UTF8[] = "UTF-8";
/* the following is an assumption that is most likely correct */
static const char sqlite_encoding_ISO8859[] = "ISO-8859-1";

/* forward declarations */
int _real_dbd_connect(dbi_conn_t *conn, const char* database);
void _translate_sqlite_type(enum enum_field_types fieldtype, unsigned short *type, unsigned int *attribs);
void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned int rowidx);
int find_result_field_types(char* field, dbi_conn_t *conn, const char* statement);
char* get_field_type(const char* statement, const char* curr_field_name);
static size_t sqlite_escape_string(char *to, const char *from, size_t length);
int wild_case_compare(const char *str,const char *str_end,
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

int _real_dbd_connect(dbi_conn_t *conn, const char* database) {
  /* connect using the database passed as an argument. If passed NULL
     or an empty string, this function tries to use the database set
     with the "dbname" option */
  sqlite *sqcon;
  char* sq_errmsg = NULL;
  char* db_fullpath = NULL;

/* ToDo: make OS-independent */
  const char dirsep[] = "/";

  const char *dbname;
  const char *dbdir;

  int timeout;

  /* initialize error stuff */
  conn->error_number = 0;
  conn->error_message = NULL;

  /* sqlite does not use hostname, username, password, port */
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

  /* sqlite specific options */
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
	
  if (dbdir && *dbdir) {
    strcpy(db_fullpath, dbdir);
  }
  if (db_fullpath[strlen(db_fullpath)-1] != *dirsep) {
    strcat(db_fullpath, dirsep);
  }
  if (dbname && *dbname) {
    strcat(db_fullpath, dbname);
  }

/*   fprintf(stderr, "try to open %s<<\n", db_fullpath); */
  sqcon = sqlite_open(db_fullpath, 0 /* param not used */, &sq_errmsg);
  free(db_fullpath);
	
  if (!sqcon) {
    /* sqlite creates a database the first time we try to access
       it. If this function fails, there's usually a problem with
       access rights or an existing database is corrupted or created
       with an incompatible version */
    if (sq_errmsg) {
      _dbd_internal_error_handler(conn, sq_errmsg, DBI_ERROR_CLIENT);
      free(sq_errmsg);
    }
    else {
      _dbd_internal_error_handler(conn, "could not open database", 0);
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
     SQLite-specific setting takes precedence over the generic timeout
     option for backwards compatibility */
  timeout = dbi_conn_get_option_numeric(conn, "sqlite_timeout");

  if (timeout == -1) {
    /* generic timeout is specified in seconds, not milliseconds */
    timeout = 1000*dbi_conn_get_option_numeric(conn, "timeout");
    if (timeout == -1) {
      timeout = 0;
    }
  }

  sqlite_busy_timeout(sqcon, timeout);	
  
  return 0;
}

int dbd_disconnect(dbi_conn_t *conn) {
  if (conn->connection) {
    sqlite_close((sqlite *)conn->connection);
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

  if (result->result_state == NOTHING_RETURNED) return 0;
	
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
    sqlite_free_table((char **)result->result_handle);
  }
  return 0;
}

int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) {
  result->currowidx = rowidx;
  return 1;
}

int dbd_get_socket(dbi_conn_t *conn){
  /* sqlite does not use sockets, so we'll always return 0 */
  return (int)0;
}

const char *dbd_get_encoding(dbi_conn_t *conn){
  /* encoding is a compile-time option with the sqlite
     library. Instead of using the sqlite-provided string, we use the
     iana.org names */
  if (!strcmp(sqlite_encoding, "UTF-8")) {
    return sqlite_encoding_UTF8;
  }
  else {
    return sqlite_encoding_ISO8859;
  }
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
  int retval;
  size_t entry_size;
  DIR *dp;
  struct dirent *entry;
  struct dirent *result;
  struct stat statbuf;
  dbi_result rs;

  /* sqlite has no builtin function to list databases. Databases are just
     files in the data directory. We search for matching files and fill a
     temporary table with what we've found. Then we query this table and
     pretend sqlite has done all the work */
  const char *sq_datadir = _conn_get_dbdir(conn);

  /* this is not nice but we have to drop the table even if it does not
   exist (sqlite has no way to list *temporary* tables so we can't check
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
      /* we do a magic number check here to make sure we
	 get only databases, not random files in the current directory.
	 SQLite databases start with the string:
	 
	 ** This file contains an SQLite 2.1 database **

      */
      FILE* fp;

      if ((fp = fopen(entry->d_name, "r")) != NULL) {
	char magic_text[48] = "";

	if (fread(magic_text, 1, 47, fp) < 47) {
	  /* either we can't read at all, or the file is too small
	     for a sqlite database anyway */
	  fclose(fp);
	  continue;
	}

	/* terminate magic text */
	magic_text[47] = '\0';

	if (strcmp(magic_text, "** This file contains an SQLite 2.1 database **")) {
	  /* this file is not meant for us */
	  fclose(fp);
	  continue;
	}

	/* close file again, we're done reading */
	fclose(fp);

	/* match filename to a pattern, or use all found files */
	if (pattern) {
	  if (wild_case_compare(entry->d_name, &entry->d_name[strlen(entry->d_name)], pattern, &pattern[strlen(pattern)], '\\') == 0) {
	    retval = sqlite_exec_printf((sqlite*)(conn->connection), "INSERT INTO libdbi_databases VALUES ('%s')", NULL, NULL, &sq_errmsg, entry->d_name);
	    if (sq_errmsg) {
	      _dbd_internal_error_handler(conn, sq_errmsg, retval);
	      free(sq_errmsg);
	      break;
	    }
	  }
	}
	else {
	  retval = sqlite_exec_printf((sqlite*)(conn->connection), "INSERT INTO libdbi_databases  VALUES ('%s')", NULL, NULL, &sq_errmsg, entry->d_name);
	  
	  if (sq_errmsg) {
	    _dbd_internal_error_handler(conn, sq_errmsg, retval);
	    free(sq_errmsg);
	    break;
	  }
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
  tempconn = dbi_conn_new_r("sqlite", instance);

  /* we explicitly cast to (char*) as we discard the "const" thing here */
  dbi_conn_set_option(tempconn, "dbname", (char*)db);
  dbi_conn_set_option(tempconn, "sqlite_dbdir", (char*)_conn_get_dbdir(conn));

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

  /* sqlite does not support the SHOW command, so we have to extract the
     information from the accessory sqlite_master table */
  if (pattern == NULL) {
    asprintf(&sql_cmd, "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name");
  }
  else {
    asprintf(&sql_cmd, "SELECT name FROM sqlite_master WHERE type='table' AND name LIKE '%s' ORDER BY name", pattern);
  }
  dbi_result = dbd_query(tempconn, sql_cmd);
  free(sql_cmd);
/*   fprintf(stderr, "select from sqlite_master has run\n"); */
  if (dbi_result) {
    while (dbi_result_next_row(dbi_result)) {
      retval = sqlite_exec_printf((sqlite*)(conn->connection), "INSERT INTO libdbi_tablenames VALUES ('%s')", NULL, NULL, NULL /* no error messages please */, dbi_result_get_string(dbi_result, "name"));
    }
    dbi_result_free(dbi_result);
  }
  else {
    dbi_conn_error(tempconn, (const char**)&sq_errmsg);
  }

  /* sqlite_close((sqlite*)(tempconn->connection)); */
  dbi_conn_close(tempconn);

  return dbd_query(conn, "SELECT tablename FROM libdbi_tablenames ORDER BY tablename");
}

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const unsigned char *statement, size_t st_length) {
	return NULL;
}

size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) {
  /* foo's -> 'foo\'s' */
  size_t len;
	
  strcpy(dest, "'");
  len = sqlite_escape_string(dest+1, orig, strlen(orig));	
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

  query_res = sqlite_get_table((sqlite*)conn->connection,
			       statement,
			       &result_table,
			       &numrows,
			       &numcols,
			       &errmsg);

  if (query_res) {
    _dbd_internal_error_handler(conn, errmsg, query_res);
    if (result_table != NULL) {
      sqlite_free_table(result_table);
    }
    return NULL;
  }
	
  result = _dbd_result_create(conn, (void *)result_table, numrows, (unsigned long long)sqlite_changes((sqlite*)conn->connection));
/*   printf("numrows:%d, numcols:%d<<\n", numrows, numcols); */
  _dbd_result_set_numfields(result, numcols);

  /* assign types to result */
  while (idx < numcols) {
/*     printf("idx: %d<< numcols:%d\n", idx, numcols); */
    int type;
    char *item;
    
    type = find_result_field_types(result_table[idx], conn, statement);
/*     printf("type: %d<<\n", type); */
    _translate_sqlite_type(type, &fieldtype, &fieldattribs);

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

int find_result_field_types(char* field, dbi_conn_t *conn, const char* statement) {
  /* 
     field is the name of the field which we want to know the type of
     conn is the connection
     statement is the query string

     returns the type as a FIELD_TYPE_XXX value

     sqlite is typeless (with one exception) on purpose. You can
     declare types with the CREATE TABLE statement, but they serve
     descriptive purposes only. Therefore libsqlite does not provide
     any helper function to find out about the type of a given field.

     However, sqlite stores the CREATE TABLE commands as a string in
     an internal table, so we can try to look up the types in these
     strings. It is a VERY GOOD idea to declare the types if we want
     the following to work

     The code assumes that table and field names do not exceed a given
     length limit. PostgreSQL uses 32 which is a bit low. SQLite does
     not seem to have fixed limits. We use a default limit of 128 here
     which can be increased in dbd_sqlite.h if need arises.
   */

  char* item;
  char* table;
  char* my_statement = NULL;
  char curr_table[MAX_IDENT_LENGTH] = "";
  char curr_field_name[MAX_IDENT_LENGTH];
  char curr_field_name_up[MAX_IDENT_LENGTH];
  char **table_result_table;
  char *curr_type;
  char* errmsg;
  int query_res;
  int table_numrows = 0; /* int seems ok as sqlite does not use longlongs */
  int table_numcols = 0;
  int type;
  dbi_error_flag errflag = 0;

  /* check whether field contains the table info. It does if the
   notation "table.field" is used */
  item = strchr(field, (int)'.');
  if (!item) {
    /* the field does not contain the table info. However, the latter
     may be available in the original statement, so let's look
     there first*/
    my_statement = strdup(statement);
    if (!my_statement) {
      return 0;
    }

    if (!(table = strstr(my_statement, " from "))) {
      table = strstr(my_statement, " FROM ");
    }

    if (!table) {
/*       fprintf(stderr, "no from keyword found\n"); */
      return 0;
    }

    *table = '\0'; /* terminate string, leaves only field names */

    if ((table = strstr(my_statement, field)) != NULL
	&& table != my_statement
	&& *(table-1) == '.') {
      /* the field name is there, isolate preceding table */
      *(table-1) = '\0';

      while (table > my_statement
	     && *table != ' '
	     && *table != ',') {
	table--;
      }

      if (*table == ' '
	  || *table == ',') {
	table++;
      }

      /* table should now point to the table name */
      strcpy(curr_table, table);
    }
    else {
      /* as a last resort assume that all fields are from the same table
	 which we have to extract from the statement that created the
	 result */

      /* To get started, we use the first item after 'from' or 'FROM'
	 as the table name (we currently ignore pathologic cases like
	 'FroM' or 'froM'. We could uppercase a copy but we need the
	 table name as is, so it is going to get complex) */
      if (!(table = strstr(statement, " from "))) {
	table = strstr(statement, " FROM ");
      }
      
      if (!table) {
	/*       fprintf(stderr, "no from keyword found\n"); */
	return 0;
      }
      
      /* set ptr to possible start of item after 'from' */
      table += 6;
      
      /* skip spaces */
      while (*table == ' ') {
	table++;
      }

      /* table now points to the table name; find the end of table */
      item = table;
      while (*item && *item != ' ' && *item != ',' && *item != ';') {
	item++;
      }
      strncpy(curr_table, table, item-table);
      curr_table[item-table] = '\0'; /* terminate just in case */

      /* for obvious reasons, the internal tables do not contain the
	 commands how they were created themselves. We have to use known
	 values for the field types */
      if (!strcmp(curr_table, "sqlite_master") ||
	  !strcmp(curr_table, "sqlite_temp_master")) {
	if (!strcmp(field, "rootpage")) {
	  return FIELD_TYPE_LONG;
	}
	else {
	  return FIELD_TYPE_STRING;
	}
      }
    }
    free(my_statement);
    strcpy(curr_field_name, field);
  }
  else {   /* each field contains table info */
    strncpy(curr_table, field, item-field);
    curr_table[item-field] = '\0';
    strcpy(curr_field_name, item+1);
  }

/*   printf("curr_table went to %s<<\ncurr_field_name went to %s<<\n", curr_table, curr_field_name); */
  /* check for known functions which may appear here instead
     of field names. There is some overlap, i.e. some function work
     both on strings and numbers. These cases would have to be
     analyzed by checking the arguments */
  /* ToDo: find the matching closing bracket and submit this function
     call to the builtin typeof() SQL function. This should return a
     distinction between text and numeric types. However, the size and
     subtype of a numeric column can't be deduced as easily */
  strcpy(curr_field_name_up, curr_field_name);

  /* uppercase string, reuse item */
  item = curr_field_name_up;
  while (*item) {
    *item = (char)toupper((int)*item);
    item++;
  }

  if (strstr(curr_field_name_up, "ABS(")
      || strstr(curr_field_name_up, "LAST_INSERT_ROWID(")
      || strstr(curr_field_name_up, "LENGTH(")
      || strstr(curr_field_name_up, "MAX(")
      || strstr(curr_field_name_up, "MIN(")
      || strstr(curr_field_name_up, "RANDOM(*)")
      || strstr(curr_field_name_up, "ROUND(")
      || strstr(curr_field_name_up, "AVG(")
      || strstr(curr_field_name_up, "COUNT(")
      || strstr(curr_field_name_up, "SUM(")) {
    return FIELD_TYPE_LONG;
  }
  else if (strstr(curr_field_name_up, "COALESCE(")
	   || strstr(curr_field_name_up, "GLOB(")
	   || strstr(curr_field_name_up, "LIKE(")
	   || strstr(curr_field_name_up, "LOWER(")
	   || strstr(curr_field_name_up, "SUBSTR(")
	   || strstr(curr_field_name_up, "UPPER(")) {
    return FIELD_TYPE_STRING;
  }
      

  /* curr_table now contains the name of the table that the field
     belongs to. curr_field_name contains the name of the field.
     Look up the field type in the sqlite_master table */

  /* first try in the table containing permanent tables */
  query_res = sqlite_get_table_printf((sqlite*)conn->connection,
				      "SELECT tbl_name, sql FROM sqlite_master where tbl_name='%s'",
				      &table_result_table,
				      &table_numrows,
				      &table_numcols,
				      &errmsg,
				      curr_table);

  if (query_res || !table_numrows) {
	if(table_result_table != NULL) {
	  sqlite_free_table(table_result_table);
	}
    /* now try in the table ocntaining temporary tables */
    query_res = sqlite_get_table_printf((sqlite*)conn->connection,
					"SELECT tbl_name, sql FROM sqlite_temp_master where tbl_name='%s'",
					&table_result_table,
					&table_numrows,
					&table_numcols,
					&errmsg,
					curr_table);
    
    if (query_res || !table_numrows) {
      _dbd_internal_error_handler(conn, errmsg, query_res);
      if (table_result_table != NULL) {
	sqlite_free_table(table_result_table);
      }
/*       printf("field not found\n"); */
      return 0;
    }
  }
  

  /* table_result_table[3] now contains the sql statement that created
     the table containing the current field */
  /*  parse the sql statement to find the type of the current field */
/*   printf("table_result_table[3]=%s<<\ncurr_field_name=%s<<\n", table_result_table[3], curr_field_name); */

  /* curr_type will point to an allocated string */
  curr_type = get_field_type(table_result_table[3], curr_field_name);

  /* free memory */
  sqlite_free_table(table_result_table);

  if (!curr_type) {
/*     printf("out of memory\n"); */
    return 0;
  }
  
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

/*   printf("field type: %s<<\n", curr_type); */
  if (strstr(curr_type, "BLOB")
      || strstr(curr_type, "BYTEA")) {
    type = FIELD_TYPE_BLOB;
  }
  else if (strstr(curr_type, "CHAR(") /* note the opening bracket */
      || strstr(curr_type, "CLOB")
      || strstr(curr_type, "TEXT") /* also catches TINYTEXT */
      || strstr(curr_type, "VARCHAR")
      || strstr(curr_type, "ENUM")
      || strstr(curr_type, "SET")
      || strstr(curr_type, "YEAR")) { /* MySQL 2 or 4 digit year (string) */
    type = FIELD_TYPE_STRING;
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

/*   printf("type went to %d<<\n", type); */
  return type;
}

char* get_field_type(const char* statement, const char* curr_field_name) {
  /*
    statement is a ptr to a string with the "create table" statement
    curr_field_name is a ptr to a string containing the name of the
    field we need the type of.

    returns the field type as an allocated string or NULL
    if an error occurred
  */
  char *item;
  char* saveptr;
  char *my_statement;
  char *field_name;
  char *end_field_name;
  char *type;
  char *curr_type = NULL;

  /* make a copy that we may modify */
  if ((my_statement = strdup(statement)) == NULL) {
    return NULL;
  }

  /* the field list of the "create table" statement starts after the 
     first opening bracket */
  item = strchr(my_statement, '(');
  if (!item) {
    free(my_statement);
    return NULL;
  }

  /* make item point to the first item in the comma-separated list */
  item++;

  /* now tokenize the field list */
  for (item = strtok_r(item, ",", &saveptr); item; item = strtok_r(NULL, ",", &saveptr)) {
/*     printf("item:%s<<\n", item); */

    /* skip leading whitespace */
    field_name = item;
    while ((*field_name == ' ') || (*field_name == '\n')) {
      field_name++;
    }

    /* terminate field name */
    end_field_name = field_name+1;
    while (*end_field_name != ' ') {
      end_field_name++;
    }
    *end_field_name = '\0';

/*     printf("field_name:%s<<\n", field_name); */

    /* analyze type if the field name is the one we want to check */
    if (!strcmp(field_name, curr_field_name)) {
      /* skip leading whitespace */
      type = end_field_name + 1;
      while (*type == ' ') {
	type++;
      }

      curr_type = strdup(type);
/*       printf("curr_type:%s<<\n"); */
      break;
    }
  }

  free(my_statement);
  return curr_type;
}

const char *dbd_select_db(dbi_conn_t *conn, const char *db) {
  /*
    sqlite does not separate connecting to a database server and using
    or opening a database. If we want to switch to a different database,
    we have to drop the current connection and create a new one
    instead, using the new database.
   */

  if (!db || !*db) {
    return NULL;
  }

  if (conn->connection) {
    sqlite_close((sqlite *)conn->connection);
  }

  if (_real_dbd_connect(conn, db)) {
    return NULL;
  }

  return db;
}

int dbd_geterror(dbi_conn_t *conn, int *err_no, char **errstr) {
  /* put error number into err_no, error string into errstr
   * return 0 if error, 1 if err_no filled, 2 if errstr filled, 3 if both err_no and errstr filled */
  int result = 0;

  if (conn->error_number) {
    *err_no = conn->error_number;
    result++;
  }
  if (conn->error_message) {
    *errstr = strdup(conn->error_message);
    result += 2;
  }
  return result;
}


unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) {
  return (unsigned long long)sqlite_last_insert_rowid((sqlite*)conn->connection);
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

/* CORE SQLITE DATA FETCHING STUFF */

void _translate_sqlite_type(enum enum_field_types fieldtype, unsigned short *type, unsigned int *attribs) {
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


void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned int rowidx) {
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
	data->d_long = (int) atol(raw); break;
      case DBI_INTEGER_SIZE8:
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

int wild_case_compare(const char *str,const char *str_end,
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
static size_t sqlite_escape_string(char *to, const char *from, size_t length)
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

  dbdir = dbi_conn_get_option(conn, "sqlite_dbdir");
	
  if (!dbdir) {
    /* use default directory instead */
    dbdir = default_dbdir;
  }

  return dbdir;
}

