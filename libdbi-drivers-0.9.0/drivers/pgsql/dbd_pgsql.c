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
 * dbd_pgsql.c: PostgreSQL database support (using libpq)
 * Copyright (C) 2001-2002, David A. Parker <david@neongoat.com>.
 * http://libdbi.sourceforge.net
 * 
 * $Id: dbd_pgsql.c,v 1.69 2013/02/08 01:01:31 mhoenicka Exp $
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

/* In 7.4 PQfreeNotify was deprecated and PQfreemem is used instead.  A
   macro exists in 7.4 for backwards compatibility. */
#ifndef PQfreeNotify   /* must be earlier than 7.4 */
#define PQfreemem PQfreeNotify
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h> /* for isdigit() */

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

#include <libpq-fe.h>
#include "dbd_pgsql.h"

static const dbi_info_t driver_info = {
	"pgsql",
	"PostgreSQL database support (using libpq)",
	"David A. Parker <david@neongoat.com>",
	"http://libdbi-drivers.sourceforge.net",
	"dbd_pgsql v" VERSION,
	__DATE__
};

static const char *custom_functions[] = PGSQL_CUSTOM_FUNCTIONS;
static const char *reserved_words[] = PGSQL_RESERVED_WORDS;

/* encoding strings, array is terminated by a pair of empty strings */
static const char pgsql_encoding_hash[][16] = {
  /* PostgreSQL , www.iana.org */
  "SQL_ASCII", "US-ASCII",
  "EUC_JP", "EUC-JP",
  "EUC_KR", "EUC-KR",
  "UNICODE", "UTF-8",
  "UTF8", "UTF-8",
  "LATIN1", "ISO-8859-1",
  "LATIN2", "ISO-8859-2",
  "LATIN3", "ISO-8859-3",
  "LATIN4", "ISO-8859-4",
  "LATIN5", "ISO-8859-9",
  "LATIN6", "ISO-8859-10",
  "LATIN7", "ISO-8859-13",
  "LATIN8", "ISO-8859-14",
  "LATIN9", "ISO-8859-15",
  "LATIN10", "ISO-8859-16",
  "ISO-8859-5", "ISO-8859-5",
  "ISO-8859-6", "ISO-8859-6",
  "ISO-8859-7", "ISO-8859-7",
  "ISO-8859-8", "ISO-8859-8",
  "KOI8", "KOI8-R",
  "WIN", "windows-1251",
  "ALT", "IBM866",
  "", ""
};

/* forward declarations of internal functions */
void _translate_postgresql_type(unsigned int oid, unsigned short *type, unsigned int *attribs);
void _get_field_info(dbi_result_t *result);
void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx);
int _dbd_real_connect(dbi_conn_t *conn, const char *db);
char *_unescape_hex_binary(char* raw, size_t in_len, size_t* out_len);
int _digit_to_number(const char c);

/* this function is available through the PostgreSQL client library, but it
   is not declared in any of their headers. I hope this won't break anything */
const char *pg_encoding_to_char(int encoding_id);


/* these are helpers for dbd_real_connect */
#define CONNINFO_APPEND_ESCAPED(conninfo, fmt, key, value )          \
    do {                                                             \
        size_t orig_size = strlen( value );                          \
        char *value_escaped = malloc( 2 * orig_size + 1 );           \
        _dbd_escape_chars( value_escaped, value, orig_size, "\\'" ); \
        CONNINFO_APPEND( conninfo, fmt, key, value_escaped );        \
        free( value_escaped );                                       \
    } while(0)

#define CONNINFO_APPEND(conninfo, fmt, key, value )                  \
    do {                                                             \
        char *tmp = conninfo;                                        \
        if( conninfo ) {                                             \
            asprintf( &conninfo, "%s " fmt, tmp, key, value );       \
            free( tmp );                                             \
        }                                                            \
        else                                                         \
            asprintf( &conninfo, fmt, key, value );                  \
	} while(0)


/* base36 decoding to convert the 5 alphanumeric chars of SQLSTATE to a 32bit int */
int base36decode(char *base36) {
    int len = strlen(base36);
    int output = 0;
    int pos = 0;

    for (; pos < len; pos++) {
        char c = base36[pos];
        if ( ((c - '0') >= 0) && ((c - '0') <= 9) ) {
            c = c - '0';
        } else {
            c = c - 'A' + 10;
        }
        output = 36 * output + c;
    }

    return output;
}

/* real code starts here */
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
  return _dbd_real_connect(conn, NULL);
}

int _dbd_real_connect(dbi_conn_t *conn, const char *db) {
	const char *dbname;
	const char *encoding = dbi_conn_get_option(conn, "encoding");

	PGconn *pgconn;
	char *conninfo = NULL;

	const char *optname = NULL;
	const char *pgopt;
	const char *optval;
	int         optval_num;
	int have_port = 0;

	/* PQconnectdb accepts additional options as a string of
	   "key=value" pairs. Assemble that string from the option
	   list */
	while(( pgopt = optname = dbi_conn_get_option_list( conn, optname ) ))
	{
		/* Ignore "encoding" and "dbname"; we'll deal with them later */
	  if ( !strcmp( pgopt, "encoding" ) || !strcmp( pgopt, "dbname" ) ) {
	    continue;
	  }

	  /* Map "username" to "user" */
	  else if( !strcmp( pgopt, "username" ) ) {
	    pgopt = "user";
	  }

	  else if (!strcmp(pgopt, "timeout")) {
	    pgopt = "connect_timeout";
	  }

	  /* Map "pgsql_foo" to "foo" */
	  else if( !strncmp( pgopt, "pgsql_", 6 ) ) {
	    pgopt += 6;
	  }

	  /* Accept these non-pgsql_ options but discard all others */
	  else if (strcmp(pgopt, "password")
		   && strcmp(pgopt, "host")
		   && strcmp(pgopt, "port")) {
	    continue;
	  }

	  if (!strcmp(pgopt, "port")) {
	    have_port++;
	  }

	  optval     = dbi_conn_get_option( conn, optname );
	  optval_num = dbi_conn_get_option_numeric( conn, optname );

	  if( optval ) {
	    CONNINFO_APPEND_ESCAPED( conninfo, "%s='%s'", pgopt, optval );
	  }
	  else {
	    CONNINFO_APPEND( conninfo, "%s='%d'", pgopt, optval_num );
	  }
	}

	if (db && *db) {
	  dbname = db;
	}
	else {
	  dbname = dbi_conn_get_option(conn, "dbname");
	}

	if( dbname )
		CONNINFO_APPEND_ESCAPED( conninfo, "%s='%s'", "dbname", dbname );

	/* if no port was specified, fill in the default PostgreSQL port */
	if (!have_port) {
	    CONNINFO_APPEND( conninfo, "%s='%d'", "port", 5432 );
	}
	  
	/* send an empty string instead of NULL if there are no options */
	pgconn = PQconnectdb(conninfo ? conninfo : "");
	if (conninfo) free(conninfo);
	if (!pgconn) return -1;

	if (PQstatus(pgconn) == CONNECTION_BAD) {
		conn->connection = (void *)pgconn; // still need this set so _error_handler can grab information
		_dbd_internal_error_handler(conn, NULL, DBI_ERROR_DBD);
		PQfinish(pgconn);
		conn->connection = NULL; // pgconn no longer valid
		return -2;
	}
	else {
		conn->connection = (void *)pgconn;
		if (dbname) conn->current_db = strdup(dbname);
	}
	
	if (encoding && *encoding) {
	  /* set connection encoding */
	  if (strcmp(encoding, "auto")) {
	    if (PQsetClientEncoding(pgconn, dbd_encoding_from_iana(encoding))) {
/* 	      printf("could not set client encoding to %s\n", dbd_encoding_from_iana(encoding)); */
	    }
	  }
	  /* else: by default, pgsql uses the database encoding
	     as the client encoding, nothing to do */
	}

	return 0;
}

int dbd_disconnect(dbi_conn_t *conn) {
  if (conn->connection) {
    PQfinish((PGconn *)conn->connection);
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
	PQclear((PGresult *)result->result_handle);
	return 0;
}

int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) {
	/* libpq doesn't have to do anything, the row index is specified when
	 * fetching fields */
	return 1;
}

int dbd_get_socket(dbi_conn_t *conn)
{
	PGconn *pgconn = (PGconn*) conn->connection;

	if(!pgconn) return -1;

	return PQsocket(pgconn);
}

const char *dbd_get_encoding(dbi_conn_t *conn){
	const char* my_enc;
	int n_encoding;
	const char* encodingopt;
	char* sql_cmd;
	dbi_result dbires = NULL;
	PGconn *pgconn = (PGconn*) conn->connection;
	
	if(!pgconn) return NULL;

	encodingopt = dbi_conn_get_option(conn, "encoding");
	if (encodingopt && !strcmp(encodingopt, "auto")) {

	  /* this is somewhat murky as the pg_encoding_to_char()
	     function is not declared properly by the PostgreSQL client
	     library headers.  This may indicate that it is not supposed
	     to be exported or that it may disappear without a trace
	     eventually. If it breaks, use a query "SHOW CLIENT_ENCODING"
	     instead */
	  my_enc = pg_encoding_to_char(PQclientEncoding(pgconn));
/*  	  printf("use PQclientEncoding, auto\n"); */
	}
	else if (encodingopt) {
	  my_enc = pg_encoding_to_char(PQclientEncoding(pgconn));
/*  	  printf("use PQclientEncoding, %s\n", encodingopt); */
	}
	else {
	  asprintf(&sql_cmd, "SELECT encoding FROM pg_database WHERE datname='%s'", conn->current_db);
	  
	  dbires = dbi_conn_query(conn, sql_cmd);
	  free(sql_cmd);

	  if (dbires && dbi_result_next_row(dbires)) {
	    n_encoding = dbi_result_get_int_idx(dbires, 1);
	    my_enc = pg_encoding_to_char(n_encoding);
/*  	    printf("select returned encoding %d<<%s\n", n_encoding, my_enc); */
	  }
	}

	if (!my_enc) {
	  return NULL;
	}
	else {
	  return dbd_encoding_to_iana(my_enc);
	}
}

const char* dbd_encoding_to_iana(const char *db_encoding) {
  int i = 0;

  /* loop over all even entries in hash and compare to penc */
  while (*pgsql_encoding_hash[i]) {
    if (!strcmp(pgsql_encoding_hash[i], db_encoding)) {
      /* return corresponding odd entry */
      return pgsql_encoding_hash[i+1];
    }
    i+=2;
  }

  /* don't know how to translate, return original encoding */
  return db_encoding;
}

const char* dbd_encoding_from_iana(const char *iana_encoding) {
  int i = 0;

  /* loop over all odd entries in hash and compare to ienc */
  while (*pgsql_encoding_hash[i+1]) {
    if (!strcmp(pgsql_encoding_hash[i+1], iana_encoding)) {
      /* return corresponding even entry */
      return pgsql_encoding_hash[i];
    }
    i+=2;
  }

  /* don't know how to translate, return original encoding */
  return iana_encoding;
}

char *dbd_get_engine_version(dbi_conn_t *conn, char *versionstring) {
  snprintf(versionstring, VERSIONSTRING_LENGTH, "%d", PQserverVersion((PGconn *)conn->connection));
  return versionstring;
}

dbi_result_t *dbd_list_dbs(dbi_conn_t *conn, const char *pattern) {
	dbi_result_t *res;
	char *sql_cmd;

	if (pattern == NULL) {
		return dbd_query(conn, "SELECT datname FROM pg_database");
	}
	else {
		asprintf(&sql_cmd, "SELECT datname FROM pg_database WHERE datname LIKE '%s'", pattern);
		res = dbd_query(conn, sql_cmd);
		free(sql_cmd);
		return res;
	}
}

dbi_result_t *dbd_list_tables(dbi_conn_t *conn, const char *db, const char *pattern) {
	if (db == NULL) {
		return NULL;
	}

	if (pattern == NULL) {
		return (dbi_result_t *)dbi_conn_queryf((dbi_conn)conn, "SELECT relname FROM pg_class WHERE relname !~ '^pg_' AND relkind = 'r' AND relowner = (SELECT datdba FROM pg_database WHERE datname = '%s') ORDER BY relname", db);
	}
	else {
		return (dbi_result_t *)dbi_conn_queryf((dbi_conn)conn, "SELECT relname FROM pg_class WHERE relname !~ '^pg_' AND relname LIKE '%s' AND relkind = 'r' AND relowner = (SELECT datdba FROM pg_database WHERE datname = '%s') ORDER BY relname", pattern, db);
	}
}

size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) {
	/* foo's -> 'foo\'s' */
	size_t len;

	strcpy(dest, "'");
	len = PQescapeString(dest+1, orig, strlen(orig));
	strcat(dest, "'");
	
	return len+2;
}

size_t dbd_conn_quote_string(dbi_conn_t *conn, const char *orig, char *dest) {
  return dbd_quote_string(conn->driver, orig, dest);
}

size_t dbd_quote_binary(dbi_conn_t *conn, const unsigned char* orig, size_t from_length, unsigned char **ptr_dest) {
  unsigned char *temp = NULL;
  unsigned char *quoted_temp = NULL;
  size_t to_length;

  temp = PQescapeByteaConn((PGconn *)conn->connection, orig, from_length, &to_length);

  if (!temp) {
    return 0;
  }

  if ((quoted_temp = malloc(to_length+2)) == NULL) {
    PQfreemem((void *)temp);
    return 0;
  }
  
  strcpy((char *)quoted_temp, "'");
  strcpy((char *)(quoted_temp+1), (char *)temp);
  strcat((char *)quoted_temp, "'");

  PQfreemem((void*)temp);

  *ptr_dest = quoted_temp;

  /* to_length already contains one extra byte for the trailing NULL byte */
  return to_length+1;
}

dbi_result_t *dbd_query(dbi_conn_t *conn, const char *statement) {
	/* allocate a new dbi_result_t and fill its applicable members:
	 * 
	 * result_handle, numrows_matched, and numrows_changed.
	 * everything else will be filled in by DBI */
	
	dbi_result_t *result;
	PGresult *res;
	int resstatus;
	
	res = PQexec((PGconn *)conn->connection, statement);
	if (res) resstatus = PQresultStatus(res);
	if (!res || ((resstatus != PGRES_COMMAND_OK) && (resstatus != PGRES_TUPLES_OK) && (resstatus != PGRES_COPY_OUT) && (resstatus != PGRES_COPY_IN))) {
	  char *base36 = PQresultErrorField(res, PG_DIAG_SQLSTATE);
	  conn->error_number = (! base36) ? 0 : base36decode(base36);
	  PQclear(res);
	  return NULL;
	}

	conn->error_number = 0;

	result = _dbd_result_create(conn, (void *)res, (unsigned long long)PQntuples(res), (unsigned long long)atoll(PQcmdTuples(res)));
	_dbd_result_set_numfields(result, (unsigned int)PQnfields((PGresult *)result->result_handle));
	_get_field_info(result);

	return result;
}

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const unsigned char *statement, size_t st_length) {
	return NULL;
}

int dbd_transaction_begin(dbi_conn_t *conn) {
  if (dbd_query(conn, "BEGIN TRANSACTION") == NULL) {
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
  /* postgresql doesn't support switching databases without reconnecting */
  if (!db || !*db) {
    return NULL;
  }

  if (conn->connection) {
    PQfinish((PGconn *)conn->connection);
    conn->connection = NULL;
  }

  if (_dbd_real_connect(conn, db)) {
    return NULL;
  }

  return db;
}

int dbd_geterror(dbi_conn_t *conn, int *err_no, char **errstr) {
	/* put error number into err_no, error string into errstr
	 * return 0 if error, 1 if err_no filled, 2 if errstr filled, 3 if both err_no and errstr filled */
	
	*err_no = conn->error_number;
	*errstr = strdup(PQerrorMessage((PGconn *)conn->connection));
	
	return 3;
}

unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) {
	unsigned long long seq_last = 0;
	char *sql_cmd;
	char *rawdata;
	dbi_result_t *result;

	asprintf(&sql_cmd, "SELECT currval('%s')", sequence);
	if (!sql_cmd) return 0;
	result = dbd_query(conn, sql_cmd);
	free(sql_cmd);

	if (result) {
		rawdata = PQgetvalue((PGresult *)result->result_handle, 0, 0);
		if (rawdata) {
			seq_last = (unsigned long long)atoll(rawdata);
		}
		dbi_result_free((dbi_result)result);
	}

	return seq_last;
}

unsigned long long dbd_get_seq_next(dbi_conn_t *conn, const char *sequence) {
	unsigned long long seq_next = 0;
	char *sql_cmd;
	char *rawdata;
	dbi_result_t *result;

	asprintf(&sql_cmd, "SELECT nextval('%s')", sequence);
	if (!sql_cmd) return 0;
	result = dbd_query(conn, sql_cmd);
	free(sql_cmd);

	if (result) {	
		rawdata = PQgetvalue((PGresult *)result->result_handle, 0, 0);
		if (rawdata) {
			seq_next = (unsigned long long)atoll(rawdata);
		}
		dbi_result_free((dbi_result)result);
	}

	return seq_next;
}

int dbd_ping(dbi_conn_t *conn) {
	PGconn *pgsql = (PGconn *)conn->connection;
	PGresult *res;

	res = PQexec(pgsql, "SELECT 1");
	if (res) {
	  PQclear (res);
	}

	if (PQstatus(pgsql) == CONNECTION_OK) {
		return 1;
	}

	PQreset(pgsql); // attempt a reconnection
	
	if (PQstatus(pgsql) == CONNECTION_OK) {
		return 1;
	}

	return 0;
}

/* CORE POSTGRESQL DATA FETCHING STUFF */

void _translate_postgresql_type(unsigned int oid, unsigned short *type, unsigned int *attribs) {
	unsigned int _type = 0;
	unsigned int _attribs = 0;

/* 	  fprintf(stderr, "oid went to %d\n", oid); */
	switch (oid) {
		case PG_TYPE_CHAR:
			_type = DBI_TYPE_INTEGER;
			_attribs |= DBI_INTEGER_SIZE1;
			break;
		case PG_TYPE_INT2:
			_type = DBI_TYPE_INTEGER;
			_attribs |= DBI_INTEGER_SIZE2;
			break;
		case PG_TYPE_INT4:
			_type = DBI_TYPE_INTEGER;
			_attribs |= DBI_INTEGER_SIZE4;
			break;
		case PG_TYPE_INT8:
			_type = DBI_TYPE_INTEGER;
			_attribs |= DBI_INTEGER_SIZE8;
			break;
		case PG_TYPE_OID:
			_type = DBI_TYPE_INTEGER;
			_attribs |= DBI_INTEGER_SIZE8;
			_attribs |= DBI_INTEGER_UNSIGNED;
			break;
			
		case PG_TYPE_FLOAT4:
			_type = DBI_TYPE_DECIMAL;
			_attribs |= DBI_DECIMAL_SIZE4;
			break;
		case PG_TYPE_FLOAT8:
			_type = DBI_TYPE_DECIMAL;
			_attribs |= DBI_DECIMAL_SIZE8;
			break;

        case PG_TYPE_DATE:
	        _type = DBI_TYPE_DATETIME;
            _attribs |= DBI_DATETIME_DATE;
            break;
        case PG_TYPE_TIME:
        case PG_TYPE_TIMETZ:
            _type = DBI_TYPE_DATETIME;
            _attribs |= DBI_DATETIME_TIME;
            break;
        case PG_TYPE_TIMESTAMP:
        case PG_TYPE_TIMESTAMPTZ:
			_type = DBI_TYPE_DATETIME;
			_attribs |= DBI_DATETIME_DATE;
			_attribs |= DBI_DATETIME_TIME;
			break;

		case PG_TYPE_NAME:
		case PG_TYPE_TEXT:
		case PG_TYPE_CHAR2:
		case PG_TYPE_CHAR4:
		case PG_TYPE_CHAR8:
		case PG_TYPE_BPCHAR:
		case PG_TYPE_VARCHAR:
			_type = DBI_TYPE_STRING;
			break;

		case PG_TYPE_BYTEA:
			_type = DBI_TYPE_BINARY;
			break;
			
		default:
			_type = DBI_TYPE_STRING;
			break;
	}
	
	*type = _type;
	*attribs = _attribs;
}

void _get_field_info(dbi_result_t *result) {
	unsigned int idx = 0;
	unsigned int pgOID = 0;
	char *fieldname;
	unsigned short fieldtype;
	unsigned int fieldattribs;
	
	while (idx < result->numfields) {
		pgOID = PQftype((PGresult *)result->result_handle, idx);
		fieldname = PQfname((PGresult *)result->result_handle, idx);
		_translate_postgresql_type(pgOID, &fieldtype, &fieldattribs);
		_dbd_result_add_field(result, idx, fieldname, fieldtype, fieldattribs);
		idx++;
	}
}

void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx) {
	unsigned int curfield = 0;
	char *raw = NULL;
	size_t strsize = 0;
	unsigned int sizeattrib;
	dbi_data_t *data;
	unsigned char *temp = NULL;
	size_t unquoted_length;


	while (curfield < result->numfields) {
		raw = PQgetvalue((PGresult *)result->result_handle, rowidx, curfield);
		data = &row->field_values[curfield];

		row->field_sizes[curfield] = 0;
		/* will be set to strlen later on for strings */
		
		if (PQgetisnull((PGresult *)result->result_handle, rowidx, curfield) == 1) {
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
						data->d_longlong = (long long) atoll(raw); break; /* hah, wonder if that'll work */
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
			case DBI_TYPE_STRING:
			    strsize = (size_t)PQgetlength((PGresult *)result->result_handle, rowidx, curfield);
				data->d_string = strdup(raw);
				row->field_sizes[curfield] = strsize;
				break;
			case DBI_TYPE_BINARY:
			  strsize = (size_t)PQgetlength((PGresult *)result->result_handle, rowidx, curfield);
			  if (strsize > 2
			      && raw[0] == '\\'
			      && raw[1] == 'x') {
			    /* hex format */
				/* row->field_sizes[curfield] = strsize; */
				/* data->d_string = malloc(strsize); */
				/* memcpy(data->d_string, raw, strsize); */
			    temp = PQunescapeBytea((const unsigned char *)_unescape_hex_binary(raw, strsize, &unquoted_length), &(row->field_sizes[curfield]));
			    if ((data->d_string = malloc(row->field_sizes[curfield])) == NULL) {
			      PQfreemem(temp);
			      break;
			    }
			    memmove(data->d_string, temp, row->field_sizes[curfield]);
			    PQfreemem(temp);
			  }
			  else {
			    temp = PQunescapeBytea((const unsigned char *)raw, &unquoted_length);
			    if ((data->d_string = malloc(unquoted_length)) == NULL) {
			      PQfreemem(temp);
			      break;
			    }
			    memmove(data->d_string, temp, unquoted_length);
			    PQfreemem(temp);
			    row->field_sizes[curfield] = unquoted_length;
			  }
			  break;
				
			case DBI_TYPE_DATETIME:
				sizeattrib = result->field_attribs[curfield] & (DBI_DATETIME_DATE|DBI_DATETIME_TIME);
				data->d_datetime = _dbd_parse_datetime(raw, sizeattrib);
				break;
				
			default:
				break;
		}
		
		curfield++;
	}
}

/* this function reverts the changes done by PQescapeByteaConn to a
   binary string. libpq does not provide such a function.  Returns the
   result as a malloc'ed string which must be freed by the caller. The
   output string is in the BYTEA escape format with single backslashes
   and single single quotes. It must be post-processed by
   PQunescapeBytea() to obtain true binary data
*/
char *_unescape_hex_binary(char* raw, size_t in_len, size_t* out_len) {
  size_t i;
  int in_pair = 0;
  int last_nibble = 0;
  char *outstring;
  char *end_of_outstring;
  int have_backslash = 0;
  int have_singlequote = 0;
  char tempchar;

  /* algorithm borrowed and modified from:
     http://pqxx.org/development/libpqxx/browser/trunk/src/binarystring.cxx
  */

  if ((outstring = malloc(((in_len-2)/2)+1)) == NULL) {
    return NULL;
  }
  end_of_outstring = outstring;

  for (i=2; i<in_len; ++i) {
    const unsigned char c = raw[i];
    if (isspace(c)) {
      if (in_pair) {
	/* "Escaped binary data is malformed." */
      }
    }
    else if (!isxdigit(c)) {
      /* "Escaped binary data contains invalid characters." */
    }
    else {
      const int nibble = (isdigit(c) ? _digit_to_number(c) : (10 + tolower(c) - 'a'));
      if (in_pair) {
	tempchar = (char)((last_nibble<<4) | nibble);
	if (tempchar == '\\' && have_backslash) {
	  /* skip second consecutive backslash */
	  have_backslash = 0;
	}
	else if (tempchar == '\'' && have_singlequote) {
	  /* skip second consecutive single quote */
	  have_singlequote = 0;
	}
	else {
	  if (tempchar == '\\') {
	    have_backslash = 1;
	  }
	  else if (tempchar == '\'') {
	    have_singlequote = 1;
	  }
	  else {
	    have_backslash = 0;
	    have_singlequote = 0;
	  }
	  *end_of_outstring = tempchar;
	  end_of_outstring++;
	}
      }
      else {
	last_nibble = nibble;
      }
      in_pair = !in_pair;
    }
  }
  *end_of_outstring = '\0';
  *out_len = end_of_outstring-outstring;
  return outstring;
}

/* converts an ASCII character code to an integer */
int _digit_to_number(const char c) {
  return (int)c - (int)'0';
}
