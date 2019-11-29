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
 * dbd_db2.c: DB2 database support
 * Copyright (C) 2008-2009, Joao Henrique F. de Freitas <joaohf@users.sourceforge.net>
 * http://libdbi.sourceforge.net
 *
 * Based on works from Christian M. Stamgren, Oracle's drivers author.
 *
 * $Id: dbd_db2.c,v 1.3 2013/01/09 21:30:19 mhoenicka Exp $
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

#include <sqlcli1.h>
#include "dbd_db2.h"

static const dbi_info_t driver_info = {
  "db2",
  "IBM DB2 database support (using DB2 Call Level Interface)",
  "João Henrique F. Freitas <joaohf@users.sourceforge.net>",
  "http://libdbi-drivers.sourceforge.net",
  "dbd_db2 v" VERSION,
  __DATE__
};

static const char *custom_functions[] = {NULL}; // TODO
static const char *reserved_words[] = DB2_RESERVED_WORDS;

/* encoding strings, array is terminated by a pair of empty strings */
static const char db2_encoding_hash[][16] = {
  /* Example, www.iana.org */
  "ascii", "US-ASCII",
  "utf8", "UTF-8",
  "latin1", "ISO-8859-1",
  "", ""
};

#define ROWSET_SIZE 35
SQLUINTEGER rowsFetchedNb;
SQLUSMALLINT row_status[ROWSET_SIZE];

/* forward declarations of local functions */
//enum enum_field_types
void _translate_db2_type(int fieldtype, short int scale, unsigned short *type, unsigned int *attribs);
void _get_field_info(dbi_result_t *result);
void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx);
void _set_error_handle(dbi_conn_t *conn, SQLSMALLINT htype, SQLHANDLE hndl);


/* Driver Infrastructure Functions */


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

int dbd_connect(dbi_conn_t *conn) {

  Db2conn *Dconn = malloc ( sizeof( Db2conn ));

  const char *username =  dbi_conn_get_option(conn, "username");
  const char *password =  dbi_conn_get_option(conn, "password");
  const char *dbalias      =  dbi_conn_get_option(conn, "dbname");

  SQLRETURN cliRC = SQL_SUCCESS;

  //TODO: get something from environment?

  /* allocate an environment handle */
  cliRC = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, (SQLHANDLE *) &(Dconn->env));
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to initialize environment handle");
    return -1;
  }

  /* set options */
  cliRC = SQLSetEnvAttr((SQLHANDLE) Dconn->env,
      SQL_ATTR_ODBC_VERSION,
      (void *)SQL_OV_ODBC3,
      0);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to set ODBC3 attribute");
    return -1;
  }

  // TODO: others options need to be set here.

  /* create a database connection */
  /* allocate a database connection handle */
  cliRC = SQLAllocHandle(SQL_HANDLE_DBC, (SQLHANDLE) Dconn->env, (SQLHANDLE *) &(Dconn->con));
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to initialize connection handle");
    return -1;
  }

  /* connect to the database */
  cliRC = SQLConnect((SQLHANDLE) Dconn->con,
      (SQLCHAR *)dbalias, SQL_NTS,
      (SQLCHAR *)username,
      SQL_NTS,
      (SQLCHAR *)password,
      SQL_NTS);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to login to the database.");
    return -1;
  }

  conn->connection = (void *)Dconn;

  return 0;
}

int dbd_disconnect(dbi_conn_t *conn) {
  /* close connection */
  Db2conn *Dconn = conn->connection;
  SQLRETURN cliRC = SQL_SUCCESS;

  if (Dconn) {
    SQLDisconnect((SQLHANDLE) Dconn->con);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
      _verbose_handler(conn, "Unable to disconnect.");
    }
    SQLFreeHandle(SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
      _verbose_handler(conn, "Unable free connection handle.");
    }
    SQLFreeHandle(SQL_HANDLE_ENV, (SQLHANDLE) Dconn->env);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_ENV, (SQLHANDLE) Dconn->env);
      _verbose_handler(conn, "Unable free environment handle.");
    }
    free(conn->connection);
  }

  conn->connection = NULL;

  return 0;
}

int dbd_geterror(dbi_conn_t *conn, int *errno, char **errstr) {
  char *errbuf = NULL;
  SQLCHAR *message = NULL;
  SQLCHAR sqlstate[SQL_SQLSTATE_SIZE];
  SQLINTEGER sqlcode;
  SQLSMALLINT length, i = 1;

  Db2conn *Dconn = conn->connection;

  message = (char *) malloc(SQL_MAX_MESSAGE_LENGTH);
  if (!message) {
    *errstr = strdup("Unable to allocate memory");
    _error_handler(conn, DBI_ERROR_NOMEM);
    return 0;
  }

  if (!conn->connection || !Dconn->errorh) {
    *errstr = strdup("Unable to connect to database.");
    return 2;
  } else {
    SQLGetDiagRec(
        (SQLSMALLINT) Dconn->errorhtype,
        (SQLHANDLE) Dconn->errorh,
        (SQLSMALLINT) i,
        (SQLCHAR *) sqlstate,
        (SQLINTEGER *) &sqlcode,
        (SQLCHAR *) message,
        (SQLSMALLINT) SQL_MAX_MESSAGE_LENGTH,
        (SQLSMALLINT *) &length
        );

    errbuf = (char *) malloc(sizeof(char *) * (SQL_SQLSTATE_SIZE + strlen(message) + 1));
    if (!errbuf) {
      *errstr = strdup("Unable to allocate memory");
      free(message);
      _error_handler(conn, DBI_ERROR_NOMEM);
      return 0;
    }

    sprintf(errbuf, "%s: %s\n", sqlstate, message);

    fprintf(stderr, "dbd_geterror buf: %s", errbuf);

    *errstr = strdup(errbuf);
    *errno = sqlcode;

    free(message);
    free(errbuf);
  }

  return 3;
}

int dbd_get_socket(dbi_conn_t *conn){
  return 0;
}


/* Internal Database Query Functions */
int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) {
  /* no-op */
  return 1;
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
  SQLRETURN cliRC = SQL_SUCCESS;

  if (result->result_handle) {
    cliRC = SQLFreeHandle(SQL_HANDLE_STMT, (SQLHANDLE) result->result_handle);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(result->conn, SQL_HANDLE_STMT, (SQLHANDLE) result->result_handle);
      _dbd_internal_error_handler(result->conn, "Unable free handle.", DBI_ERROR_DBD);
    }
  }
  result->result_handle = NULL;
  return 0;
}


/* Public Database Query Functions */
const char *dbd_get_encoding(dbi_conn_t *conn){
  /* return connection encoding as an IANA name */
  return "UTF-8";
}

const char* dbd_encoding_to_iana(const char *db_encoding) {
  int i = 0;

  /* loop over all even entries in hash and compare to menc */
  while (*db2_encoding_hash[i]) {
    if (!strncmp(db2_encoding_hash[i], db_encoding, strlen(db2_encoding_hash[i]))) {
      /* return corresponding odd entry */
      return db2_encoding_hash[i+1];
    }
    i+=2;
  }

  /* don't know how to translate, return original encoding */
  return db_encoding;
}

const char* dbd_encoding_from_iana(const char *iana_encoding) {
  int i = 0;

  /* loop over all odd entries in hash and compare to ienc */
  while (*db2_encoding_hash[i+1]) {
    if (!strcmp(db2_encoding_hash[i+1], iana_encoding)) {
      /* return corresponding even entry */
      return db2_encoding_hash[i];
    }
    i+=2;
  }

  /* don't know how to translate, return original encoding */
  return iana_encoding;
}

char *dbd_get_engine_version(dbi_conn_t *conn, char *versionstring) {
  Db2conn *Dconn = conn->connection;
  SQLRETURN cliRC = SQL_SUCCESS;
  SQLCHAR verInfoBuf[255];
  SQLSMALLINT outlen;

  *versionstring = '\0';

  cliRC = SQLGetInfo((SQLHANDLE) Dconn->con, SQL_DBMS_VER, verInfoBuf, 255, &outlen);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to allocate statement handle.");
    return versionstring;
  }

  /* SQL_DBMS_VER is mm.vv.rrrr always less than VERSIONSTRING_LENGTH */
  strncpy(versionstring, verInfoBuf, outlen);
  versionstring[outlen+1] = '\0';

  return versionstring;
}

dbi_result_t *dbd_list_dbs(dbi_conn_t *conn, const char *pattern) {
  /* return a list of available databases. If pattern is non-NULL,
     return only the databases that match. Return NULL if an error
     occurs */
  return dbd_query(conn, "SELECT datname FROM pg_database");;
}

dbi_result_t *dbd_list_tables(dbi_conn_t *conn, const char *db, const char *pattern) {
  /* return a list of available tables. If pattern is non-NULL,
     return only the tables that match */
  Db2conn *Dconn = conn->connection;
  SQLHANDLE hstmt;
  SQLRETURN cliRC = SQL_SUCCESS;

  dbi_result_t *result;

  /*
   * We just ignore the db param,
   * Oracle can't read from diffrent databases at runtime.
   */

  cliRC = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con, &hstmt);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to allocate statement handle.");
    return NULL;
  }

  if (pattern == NULL) {
    cliRC = SQLTables(hstmt,
        NULL, 0,
        NULL, 0,
        NULL, 0,
        NULL, 0);
  }
  else {
    SQLCHAR tbSchemaPattern[] = "%";
    SQLCHAR *tbNamePattern = (SQLCHAR *) pattern;

    cliRC = SQLTables(hstmt,
        NULL, 0,
        tbSchemaPattern, SQL_NTS,
        tbNamePattern, SQL_NTS,
        NULL, 0);
  }

  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to query tables.");
    return NULL;
  }
  /* How I can count the result set returned by SQLTables? */
  result = _dbd_result_create(conn, (void *)hstmt, (unsigned long long)100, (unsigned long long)0);

  /* Always 5 fields:
   * TABLE_CAT TABLE_SCHEM TABLE_NAME TABLE_TYPE REMARKS */
  _dbd_result_set_numfields(result, (unsigned int) 5);
  _get_field_info(result);

  /* TODO: return only the TABLE_NAME field
   * get all TABLE_NAME and put in stringarray and
   * use _dbd_result_create_from_stringarray to build a result
   * alloc stringarray
   * if(result && dbi_result_next_row(result)){
        stringarray[x] = dbi_result_get_string_idx(result,3);
        x++;
        }
        if(result) dbi_result_free(result);
        result = _dbd_result_create_from_stringarray(conn, x, *stringarray) */
  return result;
}

size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) {
  /* foo's -> 'foo\'s' */
  /* driver-specific, deprecated */
  return 0;
}

size_t dbd_conn_quote_string(dbi_conn_t *conn, const char *orig, char *dest) {
  /* foo's -> 'foo\'s' */
  /* connection-specific. Should take character encoding of current
     connection into account if db engine supports this */
  return 0;
}

// TODO: xxx
//size_t dbd_quote_binary(dbi_conn_t *conn, const char* orig, size_t from_length, char **ptr_dest) {
size_t dbd_quote_binary(dbi_conn_t *conn, const unsigned char *orig, size_t from_length, unsigned char **ptr_dest ) {
  /* *ptr_dest shall point to a zero-terminated string that can be
     used in SQL queries. Returns the lenght of that string in
     bytes, or DBI_LENGTH_ERROR in case of an error */
  return DBI_LENGTH_ERROR;
}

dbi_result_t *dbd_query(dbi_conn_t *conn, const char *statement) {
  /* allocate a new dbi_result_t and fill its applicable members:
   *
   * result_handle, numrows_matched, and numrows_changed.
   * everything else will be filled in by DBI */

  Db2conn *Dconn = conn->connection;
  SQLHANDLE hstmt;
  SQLSMALLINT numfields = 0;
  unsigned long long numrows = 0;
  SQLINTEGER  affectedrows = 0;
  SQLSMALLINT strLenPtr;
  SQLRETURN cliRC = SQL_SUCCESS;
  dbi_result_t *result;
  char *countquery = NULL;

  cliRC = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con, &hstmt);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
    _verbose_handler(conn, "Unable to allocate statement handle.");
    return NULL;
  }

  /* TODO: config option to select SQLExecute or ExecuteDirect */

  cliRC = SQLPrepare(hstmt, (char *) statement, SQL_NTS);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con);
    return NULL;
  }

  cliRC = SQLNumResultCols(hstmt, &numfields);
  if (cliRC != SQL_SUCCESS) {
    _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
    return NULL;
  }

  // This query is INSERT, UPDATE or DELETE?
  if (numfields < 1) {
    cliRC = SQLExecute(hstmt);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
      return NULL;
    }

    cliRC = SQLRowCount((SQLHANDLE) hstmt, &affectedrows);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
      return NULL;
    }
  } else {
    // Counting rows
    countquery = malloc(strlen(statement) + 25 +1);
    if (!countquery) {
      _error_handler(conn, DBI_ERROR_NOMEM);
       return NULL;
    }
    sprintf(countquery, "SELECT COUNT(*) FROM (%s)", statement);

    cliRC = SQLPrepare(hstmt, (char *) countquery, SQL_NTS);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con);
      return NULL;
    }

    cliRC = SQLExecute(hstmt);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
      return NULL;
    }

    // How many rows we have?
    cliRC = SQLFetch(hstmt);
    if(cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
      return NULL;
    }

    SQLINTEGER rows;
    SQLINTEGER res_size = 0;

    cliRC = SQLGetData ((SQLHANDLE) hstmt,
        1,
        SQL_C_LONG,
        &rows,
        sizeof(SQLINTEGER),
        &res_size);
    if(cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
      return NULL;
    }

    numrows = (unsigned long long) rows;

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    cliRC = SQLAllocHandle(SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con, &hstmt);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_DBC, (SQLHANDLE) Dconn->con);
      return NULL;
    }

    free(countquery);

    // Execute the statement
    cliRC = SQLPrepare(hstmt, (char *) statement, SQL_NTS);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) Dconn->con);
      return NULL;
    }

    cliRC = SQLExecute(hstmt);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
      return NULL;
    }
  }

  result = _dbd_result_create(conn, (void *)hstmt, (unsigned long long)numrows, (unsigned long long)affectedrows);
  _dbd_result_set_numfields(result, (unsigned int)numfields);
  _get_field_info(result);

  return result;
}

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const unsigned char *statement, size_t st_length) {
  /* run query using a query string that may contain NULL bytes */
  return NULL;
}

int dbd_transaction_begin(dbi_conn_t *conn) {
  /* starting a transaction (or rather a unit of work) appears to be
     implicit in DB2. Just do nothing and succeed */
  return 0;
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
  /* make the requested database the current database */
  return NULL;
}

unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) {
  /* return ID of last INSERT */
  unsigned long long seq = 0;
  char *sql;
  dbi_result_t *res;

  asprintf(&sql, "SELECT PREVIOUS VALUE FOR %s", sequence);
  res = dbd_query(conn, sql);
  free(sql);
  if(res && dbi_result_next_row(res))
    seq = dbi_result_get_int_idx(res,1);
  if(res) dbi_result_free(res);
  return seq;
}

unsigned long long dbd_get_seq_next(dbi_conn_t *conn, const char *sequence) {
  /* return ID of next INSERT */
  unsigned long long seq = 0;
  char *sql;
  dbi_result_t *res;

  asprintf(&sql, "SELECT NEXT VALUE FOR %s", sequence);
  res = dbd_query(conn, sql);
  free(sql);
  if(res && dbi_result_next_row(res))
    seq = dbi_result_get_int_idx(res,1);
  if(res) dbi_result_free(res);
  return seq;
}

int dbd_ping(dbi_conn_t *conn) {
  /* return 1 if connection is alive, otherwise 0 */
  int test = 0;
  dbi_result_t *res;

  res = dbd_query(conn, "SELECT 1");
  if(res && dbi_result_next_row(res))
    test = dbi_result_get_int_idx(res,1);
  if(res) dbi_result_free(res);
  return test;
}

void _translate_db2_type(int fieldtype, short int scale, unsigned short *type, unsigned int *attribs) {
  unsigned int _type = 0;
  unsigned int _attribs = 0;

  switch (fieldtype) {
  case SQL_DECIMAL:
  case SQL_NUMERIC:
  case SQL_DECFLOAT:
  case SQL_CHAR:
  case SQL_LONGVARCHAR:
  case SQL_VARCHAR:
  case SQL_CLOB:
    _type = DBI_TYPE_STRING;
    break;
  case SQL_TINYINT:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE1;
    break;
  case SQL_INTEGER:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE4;
    break;
  case SQL_BIGINT:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE8;
    break;
  case SQL_SMALLINT:
    _type = DBI_TYPE_INTEGER;
    _attribs |= DBI_INTEGER_SIZE2;
    break;
  case SQL_FLOAT:
    _type = DBI_TYPE_DECIMAL;
    _attribs |= DBI_DECIMAL_SIZE4;
    break;
  case SQL_REAL:
    _type = DBI_TYPE_DECIMAL;
    _attribs |= DBI_DECIMAL_SIZE4;
    break;
  case SQL_DOUBLE:
    _type = DBI_TYPE_DECIMAL;
    _attribs |= DBI_DECIMAL_SIZE8;
    break;
  case SQL_TYPE_DATE:
    _type = DBI_TYPE_DATETIME;
    _attribs |= DBI_DATETIME_DATE;
    break;
  case SQL_TYPE_TIME:
    _type = DBI_TYPE_DATETIME;
    _attribs = DBI_DATETIME_TIME;
    break;
  case SQL_TYPE_TIMESTAMP:
    _type = DBI_TYPE_DATETIME;
    _attribs |= DBI_DATETIME_DATE;
    _attribs |= DBI_DATETIME_TIME;
    break;
  case SQL_XML:
  case SQL_BIT:
  case SQL_BLOB:
  case SQL_VARBINARY:
  case SQL_LONGVARBINARY:
  case SQL_BINARY:
    _type = DBI_TYPE_BINARY;
    break;
  case SQL_WCHAR:
  case SQL_WVARCHAR:
  case SQL_WLONGVARCHAR:
  case SQL_LONGVARGRAPHIC:
  //case SQL_WLONGVARGRAPHIC:
  case SQL_GRAPHIC:
  case SQL_CLOB_LOCATOR:
  case SQL_BLOB_LOCATOR:
  case SQL_DBCLOB:
  case SQL_DBCLOB_LOCATOR:
  case SQL_VARGRAPHIC:
  case SQL_UNKNOWN_TYPE:
  default:
    _type = DBI_TYPE_STRING;
    break;
  }

  *type = _type;
  *attribs = _attribs;

  fprintf(stderr, "DB2:    type '%d' scale '%d'\n", fieldtype, scale);
  fprintf(stderr, "libdbi: type '%d' att   '%d'\n", _type, _attribs);
}

void _get_field_info(dbi_result_t *result) {
  /* retrieve field meta info */
  unsigned int idx = 0;
  unsigned short fieldtype;
  unsigned int fieldattribs;
  char* col_name_dbi;
  Db2conn *Dconn = (Db2conn *) result->conn->connection;
  SQLHANDLE *hstmt = (SQLHANDLE *) result->result_handle;

  SQLCHAR fieldname[32];
  SQLSMALLINT fieldnamelen;
  SQLSMALLINT type;
  SQLUINTEGER size;
  SQLSMALLINT scale;
  SQLRETURN cliRC = SQL_SUCCESS;

  while (idx < result->numfields) {
    cliRC = SQLDescribeCol( (SQLHANDLE) hstmt,
        (SQLSMALLINT)(idx + 1),
        fieldname,
        sizeof(fieldname),
        &fieldnamelen,
        &type,
        &size,
        &scale,
        NULL);
    if (cliRC != SQL_SUCCESS) {
      _set_error_handle(result->conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
    }

    _verbose_handler(result->conn, "field info %s %d %d\n", fieldname, type, size);

    _translate_db2_type(type, scale, &fieldtype, &fieldattribs);
    _dbd_result_add_field(result, idx, fieldname, fieldtype, fieldattribs);
    idx++;
  }

}

void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx) {
  /* get data of the current row */
  SQLHANDLE *hstmt = (SQLHANDLE *) result->result_handle;
  Db2conn *Dconn = (Db2conn *) result->conn->connection;

  SQLINTEGER curfield = 0;
  SQLRETURN cliRC = SQL_SUCCESS;

  SQLPOINTER *ptr = NULL;
  SQLINTEGER size_raw = 0;
  SQLINTEGER res_size = 0;
  SQLINTEGER strlen_or_indptr;

  dbi_data_t *data;
  unsigned int sizeattrib = 0;
  unsigned int attribs = 0;
  size_t strsize = 0;

  const char *ptr_date;

  union {
    SQLDOUBLE        dbl;
    SQLREAL          real;
    SQLINTEGER       integer;
    SQLSMALLINT      smallint;
    DATE_STRUCT      date;
    TIME_STRUCT      time;
    TIMESTAMP_STRUCT timestamp;
  } ptr_value;

  _verbose_handler(result->conn, "numfields '%d' curfield '%d' rowidx '%d' field_type '%d'\n",
      result->numfields, curfield, rowidx + 1, result->field_types[curfield]);

  cliRC = SQLFetch((SQLHANDLE) hstmt);
  if(cliRC != SQL_SUCCESS) {
    _set_error_handle(result->conn, SQL_HANDLE_STMT, (SQLHANDLE) hstmt);
    return;
  }

  #define CALL_SQL_GET_DATA(ptr, type, len)  \
    cliRC = SQLGetData(                      \
      (SQLHSTMT)     hstmt,                  \
      (SQLUSMALLINT) curfield + 1,           \
      (SQLSMALLINT)  type,                   \
      (SQLPOINTER)   ptr,                    \
      (SQLINTEGER)   len,                    \
      (SQLINTEGER*)  &strlen_or_indptr       \
    );

  #define RETVAL                                           \
    if (strlen_or_indptr == SQL_NULL_DATA) {               \
      _set_field_flag( row, curfield, DBI_VALUE_NULL, 1);  \
      break;                                               \
    }                                                      \

  while (curfield < result->numfields) {
    data = &row->field_values[curfield];
    row->field_sizes[curfield] = 0; /* will be set to strlen later on for strings */

    data = &row->field_values[curfield];
    row->field_sizes[curfield] = 0; /* will be set to strlen later on for strings */

    switch (result->field_types[curfield]) {
      case DBI_TYPE_INTEGER:
        switch (result->field_attribs[curfield] & DBI_INTEGER_SIZEMASK) {
        case DBI_INTEGER_SIZE1:
          CALL_SQL_GET_DATA(&ptr_value, SQL_C_CHAR, sizeof(SQLCHAR));
          RETVAL
          data->d_char = (char) ptr_value.smallint;
          break;
        case DBI_INTEGER_SIZE2:
          CALL_SQL_GET_DATA(&ptr_value, SQL_C_SHORT, sizeof(SQLSMALLINT));
          RETVAL
          data->d_short = (short) ptr_value.smallint;
          break;
        case DBI_INTEGER_SIZE3:
        case DBI_INTEGER_SIZE4:
          CALL_SQL_GET_DATA(&ptr_value, SQL_C_LONG, sizeof(SQLINTEGER));
          RETVAL
          data->d_long = (int) ptr_value.integer;
          break;
        case DBI_INTEGER_SIZE8:
          // SQL_BIGINT is a string!
          ptr = (SQLPOINTER) malloc(200);
          CALL_SQL_GET_DATA(ptr, SQL_C_CHAR, 200);
          RETVAL
          data->d_longlong = atoll((const char *)ptr);
          free((SQLPOINTER)ptr);
          break;
        default:
          break;
      }
      break;
      case DBI_TYPE_DECIMAL:
        switch (result->field_attribs[curfield] & DBI_DECIMAL_SIZEMASK) {
        case DBI_DECIMAL_SIZE4:
          CALL_SQL_GET_DATA(&ptr_value, SQL_C_FLOAT, sizeof(SQLREAL));
          RETVAL
          data->d_float = (float) ptr_value.real;
          break;
        case DBI_DECIMAL_SIZE8:
          CALL_SQL_GET_DATA(&ptr_value, SQL_C_DOUBLE, sizeof(SQLDOUBLE));
          RETVAL
          data->d_double = (double) ptr_value.dbl;
          break;
        default:
          break;
        }
        break;
        case DBI_TYPE_STRING:
          ptr = (SQLPOINTER) malloc(100);
          CALL_SQL_GET_DATA(ptr, SQL_C_CHAR, 100);
          RETVAL
          data->d_string = strdup((char *)ptr);
          free((SQLPOINTER)ptr);
          row->field_sizes[curfield] = 100;
          break;
        case DBI_TYPE_BINARY:
          ptr = (SQLPOINTER) malloc(100);
          CALL_SQL_GET_DATA(ptr, SQL_C_CHAR, 100);
          RETVAL
          data->d_string = strdup((char *)ptr);
          free((SQLPOINTER)ptr);
          row->field_sizes[curfield] = 100;
          break;
        case DBI_TYPE_DATETIME:
          ptr_date = malloc(sizeof(char *) * 20);
          attribs = result->field_attribs[curfield];
          switch(attribs & (DBI_DATETIME_DATE|DBI_DATETIME_TIME)) {
            case DBI_DATETIME_DATE:
              CALL_SQL_GET_DATA(&ptr_value, SQL_C_TYPE_DATE, sizeof(DATE_STRUCT));
              snprintf(ptr_date, 11, "%d-%d-%d",
                  ptr_value.date.year,
                  ptr_value.date.month,
                  ptr_value.date.day);
              break;
            case DBI_DATETIME_TIME:
              CALL_SQL_GET_DATA(&ptr_value, SQL_C_TYPE_TIME, sizeof(TIME_STRUCT));
              snprintf(ptr_date, 9, "%.2d:%.2d:%.2d",
                  ptr_value.time.hour,
                  ptr_value.time.minute,
                  ptr_value.time.second);
              break;
            case DBI_DATETIME_DATE | DBI_DATETIME_TIME:
              CALL_SQL_GET_DATA(&ptr_value, SQL_C_TYPE_TIMESTAMP, sizeof(TIMESTAMP_STRUCT));
              snprintf(ptr_date, 20, "%d-%-d-%d %.2d:%.2d:%.2d",
                  ptr_value.timestamp.year,
                  ptr_value.timestamp.month,
                  ptr_value.timestamp.day,
                  ptr_value.timestamp.hour,
                  ptr_value.timestamp.minute,
                  ptr_value.timestamp.second);
              break;
            default:
              break;
          }
          data->d_datetime = _dbd_parse_datetime(ptr_date, attribs);
          free((char *) ptr_date);
          break;
        default:
          break;
    }

    curfield++;
  }

  #undef CALL_SQL_GET_DATA
  #undef RETVAL

}

void _set_error_handle(dbi_conn_t *conn, SQLSMALLINT htype, SQLHANDLE hndl)
{

  Db2conn *Dconn = conn->connection;

  /* We need the handle to get error messages and status code */
  Dconn->errorhtype = htype;
  Dconn->errorh = (SQLHANDLE *) hndl;

}
