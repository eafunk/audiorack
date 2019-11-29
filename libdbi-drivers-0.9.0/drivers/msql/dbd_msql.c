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
 * dbd_msql.c: mSQL database support 
 * Copyright (C) 2003, Christian M. Stamgren <christian@centiongroup.com>
 * http://libdbi-drivers.sourceforge.net
 * 
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

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>


#include <msql.h>
#include "dbd_msql.h"


static const dbi_info_t driver_info = {
	"msql",
	"Mini SQL (mSQL) database support",
	"Christian M. Stamgren <christian@centiongroup.com>",
	"libdbi-drivers.sourceforge.net", 
	"dbd_msql v" VERSION,
	__DATE__
};

static const char *custom_functions[] = {NULL}; 
static const char *reserved_words[] = MSQL_RESERVED_WORDS;

static const char msql_encoding[] = "ISO-8859-1";

void _translate_msql_type(int fieldtype, unsigned short *type, unsigned int *attribs);
void _get_field_info(dbi_result_t *result);
void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx);


void dbd_register_driver(const dbi_info_t **_driver_info, const char ***_custom_functions, 
			 const char ***_reserved_words) 
{
        *_driver_info = &driver_info;
	*_custom_functions = custom_functions;
	*_reserved_words = reserved_words;
}

int dbd_initialize(dbi_driver_t *driver) 
{
        /* this indicates the driver can be safely unloaded when libdbi is
	   shut down. Change the value to '0' (zero) if the driver, or a
	   library it is linked against, installs exit handlers via
	   atexit() */
        _dbd_register_driver_cap(driver, "safe_dlclose", 1);

	/* this indicates the database engine does not support transactions */
        _dbd_register_driver_cap(driver, "transaction_support", 0);

	/* this indicates the database engine does not support savepoints */
        _dbd_register_driver_cap(driver, "savepoint_support", 0);

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
        int connection;
	const char *host = dbi_conn_get_option(conn, "host");
	const char *dbname = dbi_conn_get_option(conn, "dbname");
	
	if(( connection = msqlConnect( (char *)host )) > 0){
		conn->connection = (void *)connection;
		if( dbname ) {
			msqlSelectDB(connection, (char *)dbname);
			conn->current_db = strdup(dbname);
		}
		return 0;
	}
	return -1;
}

int dbd_disconnect(dbi_conn_t *conn) 
{
        if (conn->connection) msqlClose((int)conn->connection);
	return 0;
}


int dbd_fetch_row(dbi_result_t *result, unsigned long long rowidx) 
{
        dbi_row_t *row = NULL;
  
	if (result->result_state == NOTHING_RETURNED) return 0;
	
	if (result->result_state == ROWS_RETURNED) {
		row = _dbd_row_allocate(result->numfields); 
		_get_row_data(result, row, rowidx);
		_dbd_row_finalize(result, row, rowidx);
	}
	
	return 1; 
}


int dbd_free_query(dbi_result_t *result) 
{
        if (result->result_handle) msqlFreeResult((m_result *)result->result_handle);
	return 0;
}


int dbd_goto_row(dbi_result_t *result, unsigned long long rowidx, unsigned long long currowidx) 
{
  if (rowidx != currowidx+1) {
    msqlDataSeek((m_result *)result->result_handle, rowidx);
  }
  /* else: nothing to do, next fetch will fetch the desired row */
  return 1;
}


int dbd_get_socket(dbi_conn_t *conn)
{
        if(! conn->connection) return -1;
	/* todo: does this make sense? */
	return (int)conn->connection;
}


const char *dbd_get_encoding(dbi_conn_t *conn){
  /* ToDo: can mSQL do better than this? */
  return msql_encoding;
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
	/* todo: query db engine for version */
	*versionstring = '\0';
	return versionstring;
}

dbi_result_t *dbd_list_dbs(dbi_conn_t *conn, const char *pattern) 
{
	dbi_result_t *result = NULL;
	m_result *dbs;

	dbs = msqlListDBs((int)conn->connection);
  
	if( dbs != NULL ) {
		result = _dbd_result_create(conn, dbs, (dbs ? msqlNumRows(dbs) : 0), 0);
		_dbd_result_set_numfields(result, msqlNumFields(((m_result *)result->result_handle)));
		_get_field_info(result);
		
	}
	return result;
}

/* This function needs some work, to function like the other drivers */
dbi_result_t *dbd_list_tables(dbi_conn_t *conn, const char *db, const char *pattern) 
{
	dbi_result_t *result = NULL;
	m_result *tables;
	
	tables = msqlListTables((int)conn->connection);
	
	if( tables != NULL ) {
		result = _dbd_result_create(conn, (void *)tables, (tables ? msqlNumRows(tables) : 0), 0);
		_dbd_result_set_numfields(result, msqlNumFields(((m_result *)result->result_handle)));
		_get_field_info(result);
		
	}
	return result;
}


size_t dbd_quote_string(dbi_driver_t *driver, const char *orig, char *dest) 
{
	size_t len;
	const char *escaped = "\'\"\\";
	strcpy(dest, "'");
	len = _dbd_escape_chars(dest, orig, strlen(orig), escaped);
	
	strcat(dest, "'");
	
	return len+2; 
}


int dbd_conn_quote_string(dbi_conn_t *conn, const char *orig, char *dest) {
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

dbi_result_t *dbd_query_null(dbi_conn_t *conn, const unsigned char *statement, size_t st_length) 
{
	return NULL;
}

dbi_result_t *dbd_query(dbi_conn_t *conn, const char *statement) 
{
	dbi_result_t *result = NULL;
	int rows = 0;
	m_result *res;
	
	if ((rows = msqlQuery((int)conn->connection, (char *)statement)) < 0 ) {
		return NULL;
	}
   
	res = msqlStoreResult();
	result = _dbd_result_create(conn, (void *)res, (rows > 0 ? rows : 0), 0);
	
	if(strncasecmp(statement, "select", 6) == 0) { //This is a dirty hack and I know it...
		_dbd_result_set_numfields(result, msqlNumFields(((m_result *)result->result_handle)));
		_get_field_info(result);
	}
	
	return result;
}


int dbd_transaction_begin(dbi_conn_t *conn) {
  /* not supported */
  return 0;
}

int dbd_transaction_commit(dbi_conn_t *conn) {
  /* not supported */
        return 0;
}

int dbd_transaction_rollback(dbi_conn_t *conn) {
  /* not supported */
        return 0;
}

int dbd_savepoint(dbi_conn_t *conn, const char *savepoint) {
  /* SAVEPOINT */
        return 0;
}

int dbd_rollback_to_savepoint(dbi_conn_t *conn, const char *savepoint) {
  /* ROLLBACK TO SAVEPOINT */
        return 0;
}

int dbd_release_savepoint(dbi_conn_t *conn, const char *savepoint) {
  /* RELEASE SAVEPOINT */
        return 0;
}

const char *dbd_select_db(dbi_conn_t *conn, const char *db) 
{
	if (msqlSelectDB((int)conn->connection, (char *)db) < 0) {
		_dbd_internal_error_handler(conn, NULL, DBI_ERROR_DBD);
		return "";
	}
	return db;
}

int dbd_geterror(dbi_conn_t *conn, int *err_no, char **errstr) 
{
	if(!msqlErrMsg)
		return -1;
  
	*err_no = 0;
	*errstr = strdup(msqlErrMsg);
	return 2;
}


unsigned long long dbd_get_seq_last(dbi_conn_t *conn, const char *sequence) 
{  
  _dbd_internal_error_handler(conn, NULL, DBI_ERROR_UNSUPPORTED);
  return 0;
}


unsigned long long dbd_get_seq_next(dbi_conn_t *conn, const char *sequence) 
{	
  _dbd_internal_error_handler(conn, NULL, DBI_ERROR_UNSUPPORTED);
  return 0;
}


int dbd_ping(dbi_conn_t *conn) 
{
  /* assume the server is still alive */
	return 1;
}

/* CORE MSQL DATA FETCHING STUFF */
void _translate_msql_type(int fieldtype, unsigned short *type, unsigned int *attribs) 
{
	unsigned int _type = 0;
	unsigned int _attribs = 0;
	
	switch (fieldtype) {
		
	case UINT_TYPE:
		_attribs |= DBI_INTEGER_UNSIGNED;
		/* fall trough */
	case INT_TYPE:
		_type = DBI_TYPE_INTEGER;
		_attribs |= DBI_INTEGER_SIZE3;
		break;
		
	case UINT64_TYPE:
		_attribs |= DBI_INTEGER_UNSIGNED;
		/* fall trough */
	case INT64_TYPE:
		_type = DBI_TYPE_INTEGER;
		_attribs |= DBI_INTEGER_SIZE8; 
		break;
		
	case UINT8_TYPE:		
		_attribs |= DBI_INTEGER_UNSIGNED;
		/* fall trough */
	case INT8_TYPE:
		_type = DBI_TYPE_INTEGER;
		_attribs |= DBI_INTEGER_SIZE1;
		break;
		
	case UINT16_TYPE:
		_attribs |= DBI_INTEGER_UNSIGNED;
		/* fall trough */
	case INT16_TYPE:
		_type = DBI_TYPE_INTEGER;
		_attribs |= DBI_INTEGER_SIZE2;
		break;
		
	case BYTE_TYPE:
		_type = DBI_TYPE_INTEGER;
		_attribs |= DBI_INTEGER_SIZE1;
		break;

	case MONEY_TYPE:
	case IPV4_TYPE:
	case CIDR4_TYPE:
	case CIDR6_TYPE:
	case TEXT_TYPE:
	case CHAR_TYPE:
		_type = DBI_TYPE_STRING;
		break;
		
	case DATE_TYPE:
		_type = DBI_TYPE_DATETIME;
		_attribs |= DBI_DATETIME_DATE;
		break;
	case TIME_TYPE:
		_type = DBI_TYPE_DATETIME;
		_attribs |= DBI_DATETIME_TIME;
		break;
			
	case REAL_TYPE:
		_type = DBI_TYPE_DECIMAL;
		_attribs |= DBI_DECIMAL_SIZE4;
		break;
	
	case IDENT_TYPE:
	case NULL_TYPE:	
	
	default:
		_type = DBI_TYPE_STRING;
		break;
	}

	*type = _type;
	*attribs = _attribs;
}
 
void _get_field_info(dbi_result_t *result) 
{
	unsigned int idx = 0;
	m_field *field;
	unsigned short fieldtype;
	unsigned int fieldattribs;
	
	while ((field = msqlFetchField((m_result *)result->result_handle)) != NULL) {
		_translate_msql_type(field->type, &fieldtype, &fieldattribs);
		_dbd_result_add_field(result, idx, field->name, fieldtype, fieldattribs);
		idx++;
	}
}

void _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx) 
{
	m_result *_res = result->result_handle;
	m_row _row;
	
	unsigned int curfield = 0;
	char *raw = NULL;
	unsigned int sizeattrib;
	dbi_data_t *data;
  
	msqlDataSeek(_res, rowidx);
	_row = msqlFetchRow(_res);
	while ( curfield < result->numfields  ) {
		raw = _row[curfield];
		data = &row->field_values[curfield];
    
		row->field_sizes[curfield] = 0;

		if( raw == NULL) _set_field_flag( row, curfield, DBI_VALUE_NULL, 1);
    
		switch (result->field_types[curfield]) {
		case DBI_TYPE_INTEGER:
			sizeattrib = _isolate_attrib(result->field_attribs[curfield], 
						     DBI_INTEGER_SIZE1, DBI_INTEGER_SIZE8);
			switch (sizeattrib) {
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
			sizeattrib = _isolate_attrib(result->field_attribs[curfield], 
						     DBI_DECIMAL_SIZE4, DBI_DECIMAL_SIZE8);
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
			row->field_sizes[curfield] = strlen( raw );
			break;
		case DBI_TYPE_DATETIME:
		  sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_DATETIME_DATE\
					       , DBI_DATETIME_TIME);
		  data->d_datetime = _dbd_parse_datetime(raw, sizeattrib); 
		  break;
		default:	  
			data->d_string = strdup(raw);
			row->field_sizes[curfield] = strlen( raw );
			break;
		}
		curfield++;
	}
}
