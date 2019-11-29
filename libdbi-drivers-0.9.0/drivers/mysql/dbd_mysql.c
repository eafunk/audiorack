/*
 * libdbi - database independent abstraction layer for C.
 * Copyright (C) 2001-2002, David Parker and Mark Tobenkin.
 * http://libdbi.sourceforge.net
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
 * dbd_mysql.c: MySQL database support (using libmysqlclient)
 * Copyright (C) 2001-2002, Mark Tobenkin <mark@brentwoodradio.com>
 * http://libdbi.sourceforge.net
 * 
 * $Id: dbd_mysql.c,v 1.109 2013/02/27 23:16:20 mhoenicka Exp $
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE /* we need asprintf */

#ifndef HAVE_ATOLL
long long atoll(const char *str);
#endif

#ifndef HAVE_STRTOLL
long long strtoll(const char *nptr, char **endptr, int base);
#endif

#ifdef __MINGW32__
#include <winsock.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

#include <mysql.h>
#include "dbd_mysql.h"

static const dbi_info_t driver_info = {
	"mysql",
	"MySQL database support (using libmysqlclient)",
	"Mark M. Tobenkin <mark@brentwoodradio.com>",
	"http://libdbi-drivers.sourceforge.net",
	"dbd_mysql v" VERSION,
	__DATE__
};

static const char *custom_functions[] = MYSQL_CUSTOM_FUNCTIONS;
static const char *reserved_words[] = MYSQL_RESERVED_WORDS;

/* encoding strings, array is terminated by a pair of empty strings */
static const char mysql_encoding_hash[][16] = {
  /* MySQL, www.iana.org */
  "ascii", "US-ASCII",
  "ujis", "EUC-JP",
  "euc_kr", "EUC-KR", 
  "euckr", "EUC-KR", 
  "utf8", "UTF-8",
  "latin1", "ISO-8859-1",
  "latin2", "ISO-8859-2",
  "latin5", "ISO-8859-9",
  "latin7", "ISO-8859-13",
  "greek", "ISO-8859-7",
  "hebrew", "ISO-8859-8",
  "koi8_ru", "KOI8-R",
  "koi8r", "KOI8-R",
  "koi8_ukr","KOI8-U",
  "koi8u","KOI8-U",
  "cp1251","windows-1251",
  "win1250","windows-1250",
  "cp1250","windows-1250",
  "cp1256","windows-1256",
  "win1257","windows-1257",
  "cp1257","windows-1257",
  "cp850","IBM850",
  "cp852","IBM852",
  "dos","IBM865",
  "cp866","IBM866",
  "big5","Big5",
  "gb2312","GB2312",
  "sjis","Shift_JIS",
  "danish","ISO646-NO",
  "hungarian","ISO646-HU",
  "gbk","GBK",
  "ucs2","ISO-10646-UCS-2",
  "dec8","DEC-MCS",
  "tis620","TIS-620",
  "hp8","hp-roman8",
  "", ""
};

/* forward declarations of local functions */
void _translate_mysql_type(MYSQL_FIELD *field, unsigned short *type, unsigned int *attribs);
void _get_field_info(dbi_result_t *result);
void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx);



void dbd_register_driver(const dbi_info_t **_driver_info, const char ***_custom_functions, const char ***_reserved_words) {
	/* this is the first function called after the driver module is loaded into memory */
	*_driver_info = &driver_info;
	*_custom_functions = custom_functions;
	*_reserved_words = reserved_words;
}

int dbd_initialize(dbi_driver_t *driver) {
	/* perform any database-specific server initialization.
	 * this is called right after dbd_register_driver().
	 * return -1 on error, 0 on success.
	 * if -1 is returned, the driver will not
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

	/* free memory allocated by the library. mysql_server_end() is
	   deprecated as of MySQL 5.0.3, but the replacement
	   mysql_library_end() is simply an alias defined in
	   mysql.h. Leave this as is until it breaks */
	   
//        mysql_server_end();  It's broke... this needs to be comment out now.

	return 0;
}

int dbd_connect(dbi_conn_t *conn) {
	MYSQL *mycon;
	dbi_result_t *result = NULL;
	char* sql_cmd;
	unsigned long client_flags = 0;
	long n_port = 0;

	const char *host = dbi_conn_get_option(conn, "host");
	const char *username = dbi_conn_get_option(conn, "username");
	const char *password = dbi_conn_get_option(conn, "password");
	const char *dbname = dbi_conn_get_option(conn, "dbname");
	const char *encoding = dbi_conn_get_option(conn, "encoding");
	const char *port = dbi_conn_get_option(conn, "port");
	if (port) {
	  n_port = strtol(port, NULL, 10);
	}
	else {
	  n_port = (long)dbi_conn_get_option_numeric(conn, "port");
	}

	/* if no port was specified, use MySQL's default port to let
	   things run gracefully */
	if (n_port == 0) {
	  n_port = 3306;
	}

	int timeout = dbi_conn_get_option_numeric(conn, "timeout");

	/* mysql specific options */
	const char *unix_socket = dbi_conn_get_option(conn, "mysql_unix_socket");
	/* we honor the mysql_compression flag for the sake of
	   backwards compatibility. Newer code should use the
	   mysql_client_whatever flags instead */
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_compression") > 0) ? CLIENT_COMPRESS : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_compress") > 0) ? CLIENT_COMPRESS : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_found_rows") > 0) ? CLIENT_FOUND_ROWS : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_ignore_SPACE") > 0) ? CLIENT_IGNORE_SPACE : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_interactive") > 0) ? CLIENT_INTERACTIVE : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_local_files") > 0) ? CLIENT_LOCAL_FILES : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_multi_statements") > 0) ? CLIENT_MULTI_STATEMENTS : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_multi_results") > 0) ? CLIENT_MULTI_RESULTS : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_no_schema") > 0) ? CLIENT_NO_SCHEMA : 0;
	client_flags |= (dbi_conn_get_option_numeric(conn, "mysql_client_odbc") > 0) ? CLIENT_ODBC : 0;
	
	mycon = mysql_init(NULL);
	if (!mycon) {
		_dbd_internal_error_handler(conn, NULL, DBI_ERROR_NOMEM);
		return -2;
	}

	if (timeout != -1) { /* option was specified */
	  /* the mysql_options prototype is braindead */
	  mysql_options(mycon, MYSQL_OPT_CONNECT_TIMEOUT, (const char*) &timeout);
	}

	if (!mysql_real_connect(mycon, host, username, password, dbname, (unsigned int)n_port, unix_socket, client_flags)) {
/* 	  printf("mysql_real_connect failed with host=%s\nusername=%s\npassword=%s\ndbname=%s\nport=%s\n", username,password,dbname,port); */
		conn->connection = (void *)mycon; // still need this set so _error_handler can grab information
		_dbd_internal_error_handler(conn, NULL, DBI_ERROR_DBD);
		mysql_close(mycon);
		conn->connection = NULL; // myconn no longer valid
		return -2;
	}
	else {
		conn->connection = (void *)mycon;
		if (dbname) conn->current_db = strdup(dbname);
	}

	if (encoding && *encoding) {
	  /* set connection encoding */
	  if (!strcmp(encoding, "auto")) {
	    encoding = dbd_get_encoding(conn);
	    if (encoding) {
	      asprintf(&sql_cmd, "SET NAMES '%s'", dbd_encoding_from_iana(encoding));
	      result = dbd_query(conn, sql_cmd);
	      free(sql_cmd);
	      dbi_result_free(result);
	    }
	  }
	  else {
	    asprintf(&sql_cmd, "SET NAMES '%s'", dbd_encoding_from_iana(encoding));
	    result = dbd_query(conn, sql_cmd);
	    free(sql_cmd);
	    dbi_result_free(result);
	  }
	}

	return 0;
}

int dbd_disconnect(dbi_conn_t *conn) {
	if (conn->connection) mysql_close((MYSQL *)conn->connection);
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
	if (result->result_handle) mysql_free_result((MYSQL_RES *)result->result_handle);
	return 0;
}

int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) {
  if (rowidx != currowidx+1) {
	mysql_data_seek((MYSQL_RES *)result->result_handle, rowidx);
	/* the return type of this function is indeed void, so it is
	   unclear what happens if rowidx is outside the range. The
	   calling function must make sure the row index is valid */
  }
  /* else: nothing to do, next fetch will fetch next row */
	return 1;
}

int dbd_get_socket(dbi_conn_t *conn){
	MYSQL *mycon = (MYSQL*)conn->connection;
	return mycon ? mycon->net.fd : -1;
}

const char *dbd_get_encoding(dbi_conn_t *conn){
	MYSQL *mycon = (MYSQL*)conn->connection;
	const char* my_enc = NULL;
	const char* iana_enc = NULL;
	dbi_result dbires = NULL;
	dbi_result dbires1 = NULL;
	dbi_result dbires2 = NULL;
	const char* schemastring;
	const char* encodingopt;
	char* sql_cmd;

	if (!mycon) return NULL;

	/* quite a mess. Set the dbi option "encoding" to "auto" to get
	   a behaviour similar to PostgreSQL where the default connection
	   encoding is identical to the database encoding. If you do not 
	   use this option, or if you explicitly requested a particular
	   connection encoding, this encoding will be returned
	   [it has been set in dbd_connect()]
	*/

	encodingopt = dbi_conn_get_option(conn, "encoding");
	if (encodingopt && !strcmp(encodingopt, "auto")) {
	  asprintf(&sql_cmd, "SHOW CREATE DATABASE %s", conn->current_db);
/* 	  printf("SHOW CREATE DATABASE %s", conn->current_db); */

	  dbires = dbi_conn_query(conn, sql_cmd);

	  if (dbires && dbi_result_next_row(dbires)) {
	    /* 4.x apparently returns these values as binary, 5.x as string */
	    if (dbi_result_get_field_type_idx(dbires, 2) == DBI_TYPE_STRING) {
	      schemastring = dbi_result_get_string_idx(dbires, 2);
	    }
	    else {
	      schemastring = dbi_result_get_binary_idx(dbires, 2);
	    }

	    if (schemastring && *schemastring) {
	      my_enc = strstr(schemastring, "CHARACTER SET");
/*   	      printf("schemastring:%s<<enc:%s<<\n", schemastring, my_enc); */
	      if (my_enc) {
		my_enc += 14; /* set to start of encoding name */	    }
	    }
	  }
	}

	if (!my_enc) {
	  /* use connection encoding instead */
	  asprintf(&sql_cmd, "SHOW VARIABLES LIKE '%s'", "character_set_connection");
/* 	  printf("SHOW CREATE DATABASE %s", conn->current_db); */

	  dbires1 = dbi_conn_query(conn, sql_cmd);

	  if (dbires1 && dbi_result_next_row(dbires1)) {
	    if (dbi_result_get_field_type_idx(dbires1, 2) == DBI_TYPE_STRING) {
	      my_enc = dbi_result_get_string_idx(dbires1, 2);
	    }
	    else {
	      my_enc = dbi_result_get_binary_idx(dbires1, 2);
	    }
	  }

/* 	  my_enc = mysql_character_set_name(mycon); */
/*   	  printf("mysql claims enc:%s<<\n", my_enc); */
	  if (!my_enc) {
	    /* this may be a pre-4.1 server w/o per-connection encoding */
	    asprintf(&sql_cmd, "SHOW VARIABLES LIKE '%s'", "character_set");
/* 	  printf("SHOW CREATE DATABASE %s", conn->current_db); */

	    dbires2 = dbi_conn_query(conn, sql_cmd);


	    if (dbires2 && dbi_result_next_row(dbires2)) {
	      if (dbi_result_get_field_type_idx(dbires2, 2) == DBI_TYPE_STRING) {
		my_enc = dbi_result_get_string_idx(dbires2, 2);
	      }
	      else {
		my_enc = dbi_result_get_binary_idx(dbires2, 2);
	      }
	    }
	  }

	  free(sql_cmd);

	  if (!my_enc) {
	    if (dbires) {
	      dbi_result_free(dbires);
	    }
	    if (dbires1) {
	      dbi_result_free(dbires1);
	    }
	    if (dbires2) {
	      dbi_result_free(dbires1);
	    }

	    return NULL;
	  }
	}


	iana_enc = dbd_encoding_to_iana(my_enc);
	
	if (dbires) {
	  dbi_result_free(dbires);
	}

	if (dbires1) {
	  dbi_result_free(dbires1);
	}
	if (dbires2) {
	  dbi_result_free(dbires2);
	}

	return iana_enc;
}

const char* dbd_encoding_to_iana(const char *db_encoding) {
  int i = 0;

  /* loop over all even entries in hash and compare to menc */
  while (*mysql_encoding_hash[i]) {
    if (!strncmp(mysql_encoding_hash[i], db_encoding, strlen(mysql_encoding_hash[i]))) {
      /* return corresponding odd entry */
      return mysql_encoding_hash[i+1];
    }
    i += 2;
  }

  /* don't know how to translate, return original encoding */
  return db_encoding;
}

const char* dbd_encoding_from_iana(const char *iana_encoding) {
  int i = 0;

  /* loop over all odd entries in hash and compare to ienc */
  while (*mysql_encoding_hash[i+1]) {
    if (!strcmp(mysql_encoding_hash[i+1], iana_encoding)) {
      /* return corresponding even entry */
      return mysql_encoding_hash[i];
    }
    i += 2;
  }

  /* don't know how to translate, return original encoding */
  return iana_encoding;
}

char *dbd_get_engine_version(dbi_conn_t *conn, char *versionstring) {
  dbi_result_t *dbi_result;
  const char *versioninfo = NULL;

  /* initialize return string */
  *versionstring = '\0';

  dbi_result = dbd_query(conn, "SELECT VERSION()");

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
	dbi_result_t *res;
	char *sql_cmd;

	if (pattern == NULL) {
		return dbd_query(conn, "SHOW DATABASES");
	}
	else {
		asprintf(&sql_cmd, "SHOW DATABASES LIKE '%s'", pattern);
		res = dbd_query(conn, sql_cmd);
		free(sql_cmd);
		return res;
	}
}

dbi_result_t *dbd_list_tables(dbi_conn_t *conn, const char *db, const char *pattern) {
	dbi_result_t *res;
	char *sql_cmd;

	if (db == NULL || db[0] == '\0') {
		return dbd_query(conn, "SHOW TABLES");
	}

	if (pattern == NULL) {
		asprintf(&sql_cmd, "SHOW TABLES FROM %s", db);
		res = dbd_query(conn, sql_cmd);
		free(sql_cmd);
		return res;
	}
	else {
		asprintf(&sql_cmd, "SHOW TABLES FROM %s LIKE '%s'", db, pattern);
		res = dbd_query(conn, sql_cmd);
		free(sql_cmd);
		return res;
	}
}

size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) {
	/* foo's -> 'foo\'s' */
	unsigned long len;
	
	strcpy(dest, "'");
	len = mysql_escape_string(dest+1, orig, strlen(orig));	
	strcat(dest, "'");
	
	return (size_t)(len+2);
}

size_t dbd_conn_quote_string(dbi_conn_t *conn, const char *orig, char *dest) {
	/* foo's -> 'foo\'s' */
	unsigned long len;
	MYSQL *mycon = (MYSQL*)conn->connection;
	
	strcpy(dest, "'");
	len = mysql_real_escape_string(mycon, dest+1, orig, strlen(orig));	
	strcat(dest, "'");
	
	return (size_t)(len+2);
}

size_t dbd_quote_binary(dbi_conn_t *conn, const unsigned char* orig, size_t from_length, unsigned char **ptr_dest) {
  unsigned char *temp;
  unsigned long len;
  MYSQL *mycon = (MYSQL*)conn->connection;

  /* we allocate what mysql_real_escape_string needs, plus an extra two escape chars and a terminating zero*/
  temp = malloc(2*from_length+1+2);

  if (!temp) {
    return DBI_LENGTH_ERROR;
  }

  strcpy((char *)temp, "'");
  len = mysql_real_escape_string(mycon, (char *)(temp+1), (const char *)orig, from_length);
  strcpy((char *)(temp+len+1), "\'");
  *ptr_dest = temp;
  return (size_t)len+2;
}

dbi_result_t *dbd_query(dbi_conn_t *conn, const char *statement) {
	/* allocate a new dbi_result_t and fill its applicable members:
	 * 
	 * result_handle, numrows_matched, and numrows_changed.
	 * everything else will be filled in by DBI */
	
	dbi_result_t *result;
	MYSQL_RES *res;
	
	if (mysql_query((MYSQL *)conn->connection, statement)) {
		return NULL;
	}
	
	res = mysql_store_result((MYSQL *)conn->connection);
	
	/* if res is null and mysql reports an error,
	   we encountered a select failure */
	if (!res && mysql_errno((MYSQL *)conn->connection)) {
	  return NULL;
	}

	/* else: if res is null and no error is indicated, the
	   query was something that doesn't return rows (like an
	   INSERT) */
	result = _dbd_result_create(conn, (void *)res, (res ? mysql_num_rows(res) : 0), 
								mysql_affected_rows((MYSQL *)conn->connection));

	if (res) {
	  _dbd_result_set_numfields(result, mysql_num_fields((MYSQL_RES *)result->result_handle));
	  _get_field_info(result);
	}
	else {
	  _dbd_result_set_numfields(result, 0);
	}

	return result;
}

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const unsigned char *statement, size_t st_length) {
	dbi_result_t *result;
	MYSQL_RES *res;
	
	if (mysql_real_query((MYSQL *)conn->connection, (const char *)statement, st_length)) {
		return NULL;
	}
	
	res = mysql_store_result((MYSQL *)conn->connection);
	
	/* if res is null, the query was something that doesn't return rows (like an INSERT) */
	result = _dbd_result_create(conn, (void *)res, (res ? mysql_num_rows(res) : 0), 
								mysql_affected_rows((MYSQL *)conn->connection));

	if (res) {
	  _dbd_result_set_numfields(result, mysql_num_fields((MYSQL_RES *)result->result_handle));
	  _get_field_info(result);
	}

	return result;
}

int dbd_transaction_begin(dbi_conn_t *conn) {
  if (dbd_query(conn, "START TRANSACTION") == NULL) {
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

const char *dbd_select_db(dbi_conn_t *conn, const char *db) {
	if (mysql_select_db((MYSQL *)conn->connection, db)) {
		_dbd_internal_error_handler(conn, NULL, DBI_ERROR_DBD);
		return "";
	}

	if (conn->current_db) {
	  free(conn->current_db);
	}
	conn->current_db = strdup(db);
	return db;
}

int dbd_geterror(dbi_conn_t *conn, int *err_no, char **errstr) {
	/* put error number into err_no, error string into errstr
	 * return 0 if error, 1 if err_no filled, 2 if errstr filled, 3 if both err_no and errstr filled */

	if (strcmp("",mysql_error((MYSQL *)conn->connection)) == 0) {
		return -1;
	}
	
	*err_no = mysql_errno((MYSQL *)conn->connection);
	*errstr = strdup(mysql_error((MYSQL *)conn->connection));
	return 3;
}

unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) {
	return mysql_insert_id((MYSQL *)conn->connection);
}

unsigned long long dbd_get_seq_next(dbi_conn_t *conn, const char *sequence) {
	return 0;
}

int dbd_ping(dbi_conn_t *conn) {
	MYSQL *mysql = (MYSQL *)conn->connection;

	if (mysql_ping(mysql) == 0) {
		// server is alive and kicking
		return 1;
	}

	return 0;
}

/* CORE MYSQL DATA FETCHING STUFF */

void _translate_mysql_type(MYSQL_FIELD *field, unsigned short *type, unsigned int *attribs) {
	unsigned int _type = 0;
	unsigned int _attribs = 0;

	switch (field->type) {
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
#ifdef FIELD_TYPE_BIT
		case FIELD_TYPE_BIT: // BIT field (MySQL 5.0.3 and up)
			_attribs |= DBI_INTEGER_UNSIGNED;
#endif
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
			_attribs |= DBI_DATETIME_DATE | DBI_DATETIME_TIME;
			break;
			
		case FIELD_TYPE_TINY_BLOB:
		case FIELD_TYPE_MEDIUM_BLOB:
		case FIELD_TYPE_LONG_BLOB:
		case FIELD_TYPE_BLOB:
		default:
			// see MySQL reference manual 22.2.1 on detecting binary flavour
			// http://dev.mysql.com/doc/refman/5.0/en/c-api-datatypes.html
			if(field->charsetnr == 63){
				_type = DBI_TYPE_BINARY;
				break;
			}
		case FIELD_TYPE_VAR_STRING:
		case FIELD_TYPE_STRING:
#ifdef FIELD_TYPE_NEWDECIMAL
		case FIELD_TYPE_NEWDECIMAL: // Precision math DECIMAL or NUMERIC field (MySQL 5.0.3 and up)
#endif
		case FIELD_TYPE_DECIMAL: /* decimal is actually a string, has arbitrary precision, no floating point rounding */
		case FIELD_TYPE_ENUM:
		case FIELD_TYPE_SET:
			_type = DBI_TYPE_STRING;
			break;
	}
	
	*type = _type;
	*attribs = _attribs;
}

void _get_field_info(dbi_result_t *result) {
	unsigned int idx = 0;
	MYSQL_FIELD *field;
	unsigned short fieldtype;
	unsigned int fieldattribs;

	field = mysql_fetch_fields((MYSQL_RES *)result->result_handle);
	
	while (idx < result->numfields) {
		_translate_mysql_type(&field[idx], &fieldtype, &fieldattribs);
		if ((fieldtype == DBI_TYPE_INTEGER) && (field[idx].flags & UNSIGNED_FLAG)) 
			fieldattribs |= DBI_INTEGER_UNSIGNED;
		_dbd_result_add_field(result, idx, field[idx].name, fieldtype, fieldattribs);
		idx++;
	}
}

void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx) {
	MYSQL_RES *_res = result->result_handle;
	MYSQL_ROW _row;
	
	unsigned int curfield = 0;
	char *raw = NULL;
	size_t *strsizes = NULL;
	unsigned int sizeattrib;
	dbi_data_t *data;	

	_row = mysql_fetch_row(_res);
	strsizes = (size_t *)mysql_fetch_lengths(_res);

	while (curfield < result->numfields) {
		raw = _row[curfield];
		data = &row->field_values[curfield];
		
		row->field_sizes[curfield] = 0;
		/* this will be set to the string size later on if the field is indeed a string */

		if ((strsizes[curfield] == 0) && (raw == NULL)) {
		        _set_field_flag( row, curfield, DBI_VALUE_NULL, 1);
			curfield++;
			continue;
		}
		
		switch (result->field_types[curfield]) {
			case DBI_TYPE_INTEGER:
				switch (result->field_attribs[curfield] & DBI_INTEGER_SIZEMASK) {
					case DBI_INTEGER_SIZE1:
						data->d_char = (char) atol(raw); break;
					case DBI_INTEGER_SIZE2:
						data->d_short = (short) atol(raw); break;
					case DBI_INTEGER_SIZE3:
					case DBI_INTEGER_SIZE4:
						data->d_long = (int) atol(raw); break;
					case DBI_INTEGER_SIZE8:
						data->d_longlong = (long long) atoll(raw); break;
					default:
						break;
				}
				break;
			case DBI_TYPE_DECIMAL:
				switch (result->field_attribs[curfield] & DBI_DECIMAL_SIZEMASK) {
					case DBI_DECIMAL_SIZE4:
						data->d_float = (float) strtod(raw, NULL); break;
					case DBI_DECIMAL_SIZE8:
						data->d_double = (double) strtod(raw, NULL); break;
					default:
						break;
				}
				break;
			default:
			case DBI_TYPE_STRING:
				data->d_string = strdup(raw);
				row->field_sizes[curfield] = strsizes[curfield];
				break;
			case DBI_TYPE_BINARY:
				row->field_sizes[curfield] = strsizes[curfield];
				data->d_string = malloc(strsizes[curfield]+1); // one extra char for libdbi's null
				if (!data->d_string) {
					break;
				}
				memcpy(data->d_string, raw, strsizes[curfield]);
				/* todo: revise as this looks kinda strange */
				data->d_string[strsizes[curfield]] = '\0'; // manually null-terminate the data so C string casting still works
				if (dbi_conn_get_option_numeric(result->conn, "mysql_include_trailing_null") == 1) {
					row->field_sizes[curfield]++; // the extra null we added was actually part of the original data
				}
				break;
			case DBI_TYPE_DATETIME:
				sizeattrib = result->field_attribs[curfield] & (DBI_DATETIME_DATE|DBI_DATETIME_TIME);
				data->d_datetime = _dbd_parse_datetime(raw, sizeattrib);
				break;
		}
		
		curfield++;
	}
}


