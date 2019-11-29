/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * libdbi - database independent abstraction layer for C.
 * Copyright (C) 2001-2004, David Parker and Mark Tobenkin.
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
 * dbd_firebird.c: Firebird/Interbase database support
 * Copyright (C) 2004-2005, Christian M. Stamgren <christian@stamgren.com>
 * http://libdbi-drivers.sourceforge.net
 *
 */

/**
 * Warning!! Warning!! Warning!!
 * This driver has a big gotha, You can't use dbi_result_numrows() 
 * This is because I don't know of a way to fetch the number of rows in a result set from firebird.
 * The only way to do it is to fetch all rows and count them..... 
 */


#ifdef  HAVE_CONFIG_H
#  include <config.h>
#endif

#define _GNU_SOURCE /* we need asprintf */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ibase.h>

#include "dbd_firebird.h"
#include "firebird_charsets.h"
#include "utility.h"

/* is this correct? Firebird 1.5 used this instead:
   #define FB_ALIGN(n,b) ((n+b-1)&~(b-1)) */
#ifndef FB_ALIGN
#  define FB_ALIGN(n,b) ((n+1) & ~1)
/* #define FB_ALIGN(n,b) ((n+b-1)&~(b-1)) */
#endif

/* firebird versions prior to 2.0 do not typedef ISC_SCHAR but use TEXT
   instead. ISC_SCHAR's presence is checked for by the configure
   script */
#ifndef HAVE_ISC_SCHAR
#define ISC_SCHAR TEXT
#endif
	
static const dbi_info_t driver_info = {
        "firebird",
	"Firebird/Interbase database support",
	"Christian M. Stamgren <cms@cention.se>",
	"http://libdbi-drivers.sourceforge.net",
	"dbd_firebird v" VERSION,
	__DATE__
};


static const char *custom_functions[] = FIREBIRD_CUSTOM_FUNCTIONS; 
static const char *reserved_words[] = FIREBIRD_RESERVED_WORDS;
static const char firebird_encoding_NONE[] = "NONE"; /* the encoding strings */

extern char version[]; //this is dirty

void dbd_register_driver(const dbi_info_t **_driver_info, const char ***_custom_functions, 
			 const char ***_reserved_words) 
{
	*_driver_info = &driver_info;
	*_custom_functions = custom_functions;
	*_reserved_words = reserved_words;
}

int dbd_initialize(dbi_driver_t *driver) 
{
        /* this indicates the driver cannot be safely unloaded when
	   libdbi is shut down. Change the value to '1' if the driver,
	   or a library it is linked against, is finally fixed to not
	   install exit handlers via atexit() */
        _dbd_register_driver_cap(driver, "safe_dlclose", 0);

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

int dbd_connect(dbi_conn_t *conn) 
{
	return _dbd_real_connect(conn, NULL);
}


int dbd_disconnect(dbi_conn_t *conn) 
{
	ibase_conn_t *iconn = conn->connection;
    
	if(iconn != NULL) {
		isc_commit_transaction(iconn->status_vector, &(iconn->trans));
		isc_detach_database(iconn->status_vector, &(iconn->db));
		
		dealocate_iconn(iconn);
	}

	return 0;
}


int dbd_fetch_row(dbi_result_t *result, unsigned long long rowidx) 
{
	dbi_row_t *row = NULL;
	
	if (result->result_state == NOTHING_RETURNED) return -1;
	if (result->result_state == ROWS_RETURNED) {
	        row = _dbd_row_allocate(result->numfields);
		if( _get_row_data(result, row, rowidx) == 0 ) 
			return 0;
		
		_dbd_row_finalize(result, row, rowidx);
	}
  
	return 1; 
}


int dbd_free_query(dbi_result_t *result) 
{
	dbi_conn_t *conn = dbi_result_get_conn(result);
	ibase_conn_t *iconn = conn->connection;
	ibase_stmt_t *istmt = result->result_handle;
	
	if(istmt != NULL) {
		isc_dsql_free_statement(iconn->status_vector, &(istmt->stmt), DSQL_drop);
		free(istmt->osqlda);
		free(istmt);
	}

	istmt = NULL;
	return 0;
}


int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) 
{	
	/* no-op */
	return 1;
}


int dbd_get_socket(dbi_conn_t *conn)
{
	return 0; 
}

/* This implemetation is not the best, 
 * but i cant find a coprrect way of doing this 
 */
const char *dbd_get_encoding(dbi_conn_t *conn)
{
	ibase_conn_t *iconn = conn->connection;
	if( iconn != NULL)
		return dbd_encoding_to_iana( iconn->charset );

	return firebird_encoding_NONE;
}

const char* dbd_encoding_to_iana(const char *db_encoding) 
{
	register int i = 0;

	/* loop over all even entries in hash and compare to menc */
	while (*firebird_encoding_hash[i]) {
		if (!strncmp(firebird_encoding_hash[i], db_encoding, 
			     strlen(firebird_encoding_hash[i]))) {
			/* return corresponding odd entry */
			return firebird_encoding_hash[i+1];
		}
		i+=2;
	}

	/* don't know how to translate, return original encoding */
	return db_encoding;
}

const char* dbd_encoding_from_iana(const char *iana_encoding) 
{
	register int i = 0;

	/* loop over all odd entries in hash and compare to ienc */
	while (*firebird_encoding_hash[i+1]) {
		if (!strcmp(firebird_encoding_hash[i+1], iana_encoding)) {
			/* return corresponding even entry */
			return firebird_encoding_hash[i];
		}
		i+=2;
	}

	/* don't know how to translate, return original encoding */
	return iana_encoding;
}


char *dbd_get_engine_version(dbi_conn_t *conn, char *versionstring) 
{
	ibase_conn_t *iconn = conn->connection;
	char *dot;
	char *start;
	char *stop;

	/* Firebird make some easy things hard ... this is one of them ... */
	isc_version(&(iconn->db), (isc_callback)_get_firebird_version, NULL);

	/* version now contains something like:
	   Firebird/linux Intel (access method), version "LI-V1.5.1.4500 Firebird 1.5"
	*/

	/* try to locate the version number. Look for the first dot, go
	   back where the number before the dot starts, then walk
	   forward to the last dot or number */
	dot = strchr(version, (int)'.');
	if (dot) {
		start = dot-1;
		while (start>version && isdigit((int)(*(start-1)))) {
			start--;
		}

		stop = start;
		while (*(stop+1) && (isdigit((int)(*(stop+1))) || *(stop+1)=='.')) {
			stop++;
		}

		if (stop-start < VERSIONSTRING_LENGTH) {
			/* BAD BAD BAD hack: we chop off the last two
			   digits of the version string as the numeric
			   form can't handle 3+digit sub-versions */
			strncpy(versionstring, start, stop-start-1);
			versionstring[stop-start-1] = '\0';
		}
	}
	return versionstring;
}

dbi_result_t *dbd_list_dbs(dbi_conn_t *conn, const char *pattern) 
{
	return NULL; 
}


dbi_result_t *dbd_list_tables(dbi_conn_t *conn, const char *db, const char *pattern) 
{

	if (pattern == NULL) {
		return  dbd_query(conn, "SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG = 0");
	}
	else {
		dbi_result_t *res;
		char *sql_cmd;
		asprintf(&sql_cmd, "SELECT RDB$RELATION_NAME FROM RDB$RELATIONS WHERE RDB$SYSTEM_FLAG = 0 "
			           " AND RDB$RELATION_NAME LIKE '%s'", pattern);
		res = dbd_query(conn, sql_cmd);
		free(sql_cmd);
		return res;
	}
}


size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) 
{
	const char *worker = orig;
	register int i = 0, j = 1;
	size_t length = strlen(orig);

	strcpy(dest, "'");
	for(i = 0; i <= length; i++) {
		
		switch(worker[i]) {
		case '\'': 
			dest[j++] = '\'';
			break;
				
		}
		dest[j++] = worker[i];
	}
	strcat(dest, "'");
	return j;
}


size_t dbd_conn_quote_string(dbi_conn_t *conn, const char *orig, char *dest) 
{
	return dbd_quote_string(conn->driver, orig, dest);
}

size_t dbd_quote_binary(dbi_conn_t *conn, const unsigned char *orig, 
			size_t from_length, unsigned char **ptr_dest) 
{
	unsigned char *temp;
	size_t len;

	/* todo: this uses the libdbi builtin encoding routine. firebird may
	   have its own version */
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

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const char unsigned *statement, size_t st_length) 
{	
	return NULL;
}


dbi_result_t *dbd_query(dbi_conn_t *conn, const char *statement) 
{

	XSQLVAR *var;
	XSQLDA  *sqlda; /* output SQLDA */
	isc_stmt_handle stmt = NULL; /* statement handle */
	ibase_stmt_t *res;
	dbi_result_t *result;
	static char stmt_info[] = { isc_info_sql_stmt_type };
	char info_buffer[20];
	short l;
	long statement_type;
	short num_cols, i;
	short length, alignment, type, offset;
/* 	long buffer[MAXLEN*8]; */
	void* buffer = NULL;
	unsigned long long numrows = 0, affectedrows = 0;
	ibase_conn_t *iconn = conn->connection;

       if (isc_dsql_allocate_statement(iconn->status_vector, &(iconn->db), &stmt)) {
	       if (iconn->status_vector[0] == 1 && iconn->status_vector[1]) {
		       char msg[512];
		       long* pvector = iconn->status_vector;
		       isc_interprete(msg, &pvector);
		       _dbd_internal_error_handler(conn, msg, DBI_ERROR_CLIENT);
	       }
	       return NULL;
       }
       
       sqlda = (XSQLDA *) malloc(XSQLDA_LENGTH(1));
       sqlda->sqln = 1;
       sqlda->version = 1;

       if (isc_dsql_prepare(iconn->status_vector, &(iconn->trans), &stmt, 0, (char *)statement, 3, sqlda)) {
	       if (iconn->status_vector[0] == 1 && iconn->status_vector[1]) {
		       char msg[512];
		       long* pvector = iconn->status_vector;
		       isc_interprete(msg, &pvector);
		       _dbd_internal_error_handler(conn, msg, DBI_ERROR_CLIENT);
	       }
	       free(sqlda);
	       isc_dsql_free_statement(iconn->status_vector, &stmt, DSQL_drop);
	       return NULL;
       }

       if (!isc_dsql_sql_info(iconn->status_vector, &stmt, sizeof(stmt_info), stmt_info,
			      sizeof(info_buffer), info_buffer)) {
	       l = (short) isc_vax_integer((char *) info_buffer + 1, 2);
	       statement_type = isc_vax_integer((char *) info_buffer + 3, l); 
       }

       /* Execute a non-select statement.*/
       if (!sqlda->sqld) {
	       if (isc_dsql_execute(iconn->status_vector, &(iconn->trans), &stmt , SQL_DIALECT_V6, NULL)) {
		       if (iconn->status_vector[0] == 1 && iconn->status_vector[1]) {
			       char msg[512];
			       long* pvector = iconn->status_vector;
			       isc_interprete(msg, &pvector);
			       _dbd_internal_error_handler(conn, msg, DBI_ERROR_CLIENT);
		       }
		       free(sqlda);
		       isc_dsql_free_statement(iconn->status_vector, &stmt, DSQL_drop);
		       return NULL;
	       }
	       /* Commit DDL statements if that is what sql_info says */
	       if (iconn->trans && (statement_type == isc_info_sql_stmt_ddl)) {

		       if (isc_commit_transaction(iconn->status_vector, &(iconn->trans))) {
			       if (iconn->status_vector[0] == 1 && iconn->status_vector[1]) {
				       char msg[512];
				       long* pvector = iconn->status_vector;
				       isc_interprete(msg, &pvector);
				       _dbd_internal_error_handler(conn, msg, DBI_ERROR_CLIENT);
			       }
			       free(sqlda);
			       isc_dsql_free_statement(iconn->status_vector, &stmt, DSQL_drop);
			       return NULL;
		       }
		       isc_start_transaction(iconn->status_vector, &(iconn->trans), 1, &(iconn->db), 0, NULL);
	       }


       /* Process select statements. */
       } else {
	       
	       
	       num_cols = sqlda->sqld;
	       numrows = 1; /*  Firebird  can't say how many rows there is, in this early stage. 
				We need to fetch all rows and count them :( */

	       /* HACK HACK HACK MH:2008-01-02 */
	       /* I don't really know how much needs to be allocated
		  here. The Firebird example code and the docs won't
		  tell me. I just know that the previously used hard
		  limit (4096) is not enough to run the test program
		  successfully. I'm assuming here that in the worst
		  case num_cols columns contain strings of the maximum
		  allowed length, and that this is just about
		  sufficient. I may be wasting memory here, or the
		  code just so happens to work for the tests
		  applied. If crashes or strange results are reported,
		  revisit this issue */
	       buffer = malloc(32768*num_cols);

	       /* Need more room. */
	       if (sqlda->sqln < num_cols) {
		       sqlda = (XSQLDA *) realloc(sqlda, XSQLDA_LENGTH(num_cols));
		       sqlda->sqln = num_cols;
		       sqlda->version = 1;
		       
		       if (isc_dsql_describe(iconn->status_vector, &stmt, SQL_DIALECT_V6, sqlda)) {
			       if (iconn->status_vector[0] == 1 && iconn->status_vector[1]) {
				       char msg[512];
				       long* pvector = iconn->status_vector;
				       isc_interprete(msg, &pvector);
				       _dbd_internal_error_handler(conn, msg, DBI_ERROR_CLIENT);
			       }
			       free(sqlda);
			       isc_dsql_free_statement(iconn->status_vector, &stmt, DSQL_drop);
			       return NULL;
		       }
		       
	       }
	       for (var = sqlda->sqlvar, offset = 0, i = 0; i < num_cols; var++, i++) {
		       length = alignment = var->sqllen;
		       type = var->sqltype & ~1;
		       
		       if (type == SQL_TEXT)
			       alignment = 1;
		       else if (type == SQL_VARYING) {
			       length += sizeof (short) + 1;
			       alignment = sizeof (short);
		       }
		       
		       offset = FB_ALIGN(offset, alignment);
		       var->sqldata = (char *) buffer + offset;
		       offset += length;
		       offset = FB_ALIGN(offset, sizeof (short));
		       var->sqlind = (short*) ((char *) buffer + offset);
		       offset += sizeof  (short);
	       }
	       
	       if (isc_dsql_execute(iconn->status_vector, &(iconn->trans), &stmt, SQL_DIALECT_V6, NULL)) {
		       free(sqlda);
		       if (iconn->status_vector[0] == 1 && iconn->status_vector[1]) {
			       char msg[512];
			       long* pvector = iconn->status_vector;
			       isc_interprete(msg, &pvector);
			       _dbd_internal_error_handler(conn, msg, DBI_ERROR_CLIENT);
		       }
		       isc_dsql_free_statement(iconn->status_vector, &stmt, DSQL_drop);
		       return NULL;
	       }       
       }

       res = (ibase_stmt_t *)malloc(sizeof(ibase_stmt_t));
       res->stmt = stmt;
       res->osqlda = sqlda;
       res->rowcount = 1;
       
       result = _dbd_result_create(conn, (void *)res, numrows , affectedrows);
       _dbd_result_set_numfields(result, res->osqlda->sqld);
       _get_field_info(result);

       /* obviously we're not supposed to free this buffer here, but
	  who else is going to do this? */       
/*        if (buffer) { */
/* 	       free(buffer); */
/*        } */
       return result;
}

int dbd_transaction_begin(dbi_conn_t *conn) {
  if (dbd_query(conn, "SET TRANSACTION") == NULL) {
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

const char *dbd_select_db(dbi_conn_t *conn, const char *db) 
{
	ibase_conn_t *iconn = conn->connection;

	if (!db || !*db) {
		return NULL;
	}

	if (iconn) {
		isc_commit_transaction(iconn->status_vector, &(iconn->trans));
                isc_detach_database(iconn->status_vector, &(iconn->db));
		if(conn->current_db) free(conn->current_db);
		free(iconn);
		iconn = NULL;
	}

	dbi_conn_set_option(conn, "dbname", db);
	if (dbd_connect(conn)) {
		return NULL;
	}

	return db; 
}

int dbd_geterror(dbi_conn_t *conn, int *err_no, char **errstr) 
{
	/* error_message and error_number were already set by calls to _dbd_internal_error_handler */
	*err_no = conn->error_number;
	*errstr = (conn->error_message) ? strdup(conn->error_message):NULL;
	return 1;
}


unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) 
{  
	return return_generator_value(conn, sequence, 0); //0 is currval
}

unsigned long long dbd_get_seq_next(dbi_conn_t *conn, const char *sequence) 
{		
	return return_generator_value(conn, sequence, 1); //1 is nextval
}


int dbd_ping(dbi_conn_t *conn) 
{
	char buf[100];
	ibase_conn_t *iconn = conn->connection;
        
	if (isc_database_info(iconn->status_vector, &(iconn->db), 0, NULL, sizeof(buf), buf)) {
		free(iconn);
		if (conn->current_db ) free(conn->current_db);
		if(! dbd_connect(conn)) return 0;
	}

	return 1;
}
