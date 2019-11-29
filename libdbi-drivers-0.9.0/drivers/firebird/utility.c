/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * dbd_firebird: Firebird/Interbase database support
 * Copyright (C) 2004-2005, Christian M. Stamgren <christian@stamgren.com>
 * http://libdbi-drivers.sourceforge.net
 *
 */

#include <stdio.h>
#include <string.h>

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

#include <ibase.h>
/* #include <gds.h> */

#include "dbd_firebird.h"
#include "utility.h"

/* this is defined by the Makefile and passed via -D */
/* #define DBDIR /usr/local/var/lib/libdbi/firebird */

static const char default_dbdir[] = DBDIR;

char version[MAXLEN];

int _dbd_real_connect(dbi_conn_t *conn, char *enc) 
{
	char dpb_buffer[256], *dpb, *p, *fb_encoding; 
	char dbase[256];
	short dpb_length; 

	char db_fullpath[PATH_MAX];

        isc_db_handle db = 0L; /* database handle */
        isc_tr_handle trans = 0L; /* transaction handle */
	ibase_conn_t *iconn = (ibase_conn_t * ) malloc(sizeof(ibase_conn_t));
	ISC_STATUS status_vector[ISC_STATUS_LENGTH];

	const char *dbname =  dbi_conn_get_option(conn, "dbname");
	const char *host =  dbi_conn_get_option(conn, "host");
	const char *username = dbi_conn_get_option(conn, "username");
	const char *password = dbi_conn_get_option(conn, "password");
	const char *encoding = dbi_conn_get_option(conn, "encoding");
	
	if(encoding == NULL || *encoding == '\0') 
	  encoding = "NONE";

	dpb = dpb_buffer;
	*dpb++ = isc_dpb_version1;
	*dpb++ = isc_dpb_num_buffers;
	*dpb++ = 1;
	*dpb++ = 90;
	
	*dpb++ = isc_dpb_user_name;
	*dpb++ = strlen(username);
	for (p = (char*)username; *p; *dpb++ = *p++);

	*dpb++ = isc_dpb_password;
	*dpb++ = strlen(password);
	for (p = (char*)password; *p; *dpb++ = *p++);

	*dpb++ = isc_dpb_lc_ctype;
	fb_encoding = (char*)dbd_encoding_from_iana(encoding);
	*dpb++ = strlen(fb_encoding);
	for (p = fb_encoding; *p; *dpb++ = *p++);

	dpb_length = dpb - dpb_buffer;
	dpb = dpb_buffer;

	/* could be used here, but is tagged deprecated */
/* 	isc_expand_dpb(&dpb, &dpb_length, */
/* 		       isc_dpb_user_name, username, */
/* 		       isc_dpb_password, password, */
/* 		       isc_dpb_lc_ctype, dbd_encoding_from_iana(encoding), */
/* 		       NULL); */

	
	if (!dbname) {
		_dbd_internal_error_handler(conn, "no database specified", DBI_ERROR_DBD);
		return -1;
	}
	
	_firebird_populate_db_string( conn, dbname, db_fullpath );

	if (host == NULL || !*host) {
		snprintf(dbase, 256, "%s", db_fullpath);
	}
	else {
		snprintf(dbase, 256, "%s:%s", host, db_fullpath);
	}

/*  	printf("dbase went to: %s<<; host: %s, username: %s, passwd: %s, encoding: %s; dbp:%s<<\n", dbase, host, username, password, encoding, dpb); */
/*  	fflush(stdout); */
	isc_attach_database(status_vector, strlen(dbase), dbase, &db, dpb_length, dpb);
	if (status_vector[0] == 1 && status_vector[1]) {
		char msg[512];
		long* pvector = status_vector;
		dealocate_iconn( iconn );
		isc_interprete(msg, &pvector);
		_dbd_internal_error_handler(conn, msg, DBI_ERROR_DBD);
		return -1;
	}
	else {
		isc_start_transaction(status_vector, &trans, 1, &db, 0, NULL);
	}

	iconn->trans = trans;
	iconn->db = db;
	iconn->charset = strdup(encoding);
	conn->connection = (void *)iconn;

        if (dbase) conn->current_db = strdup(dbase);
	
	return 0;
}

char *_firebird_populate_db_string( dbi_conn_t *conn, const char *dbname, char *db_fullpath )
{
	/* ToDo: make OS-independent */
	const char dirsep[] = "/";
	const char *dbdir = dbi_conn_get_option(conn, "firebird_dbdir");
	
	if (!dbdir || !*dbdir) {
		/* use default directory instead */
		dbdir = default_dbdir;
	}

	bzero(db_fullpath, PATH_MAX);

	if (dbdir && *dbdir) {
		strcpy(db_fullpath, dbdir);
	}
	
	if (db_fullpath[strlen(db_fullpath)-1] != *dirsep && db_fullpath[0] != '\0') 
		strcat(db_fullpath, dirsep);
	
	strcat(db_fullpath, dbname);
	return db_fullpath;
}


void _get_firebird_version( void *ptr, char *vrs ) 
{ 
	static int counter; 
	if( counter == 0 ) {
		strncpy(version,vrs, MAXLEN-1);
		++counter;
	}	
	return;
}


/* CORE FIREBIRD DATA FETCHING STUFF */
void _translate_firebird_type(int fieldtype, unsigned short *type, unsigned int *attribs) 
{
	unsigned int _type = 0;
	unsigned int _attribs = 0;
		
	switch (fieldtype) {
		
	  
	case SQL_TEXT:
		_type = DBI_TYPE_STRING;
	        _attribs |= DBI_STRING_FIXEDSIZE;
	case SQL_VARYING:
	        _type = DBI_TYPE_STRING;
		break;
	case SQL_SHORT:
	        _type = DBI_TYPE_INTEGER;
	        _attribs |= DBI_INTEGER_SIZE2;
		break;
	case SQL_LONG:
	        _type = DBI_TYPE_INTEGER;
	        _attribs |= DBI_INTEGER_SIZE4;
		break;
	case SQL_INT64:
	        _type = DBI_TYPE_INTEGER;
	        _attribs |= DBI_INTEGER_SIZE8;
		break;
	case SQL_FLOAT:
	        _type = DBI_TYPE_DECIMAL;
	        _attribs |= DBI_DECIMAL_SIZE4;
		break;
	case SQL_DOUBLE:
	        _type = DBI_TYPE_DECIMAL;
	        _attribs |= DBI_DECIMAL_SIZE8;
		break;
	case SQL_TIMESTAMP:
		_type = DBI_TYPE_DATETIME;
	        _attribs |= DBI_DATETIME_TIME;
		_attribs |= DBI_DATETIME_DATE;
		break;
	case SQL_TYPE_TIME:
	        _type = DBI_TYPE_DATETIME;
	        _attribs |= DBI_DATETIME_TIME;
		break;
	case SQL_TYPE_DATE:
	        _type = DBI_TYPE_DATETIME;
		_attribs |= DBI_DATETIME_DATE;
		break;
	case SQL_BLOB:
	case SQL_ARRAY:
	        _type = DBI_TYPE_BINARY;
		break;
	}
	
	*type = _type;
	*attribs = _attribs;
}
 
void _get_field_info(dbi_result_t *result) 
{
	unsigned int idx = 0;
	unsigned short fieldtype;
	unsigned int fieldattribs;
	ibase_stmt_t *istmt = result->result_handle;

	while (idx < result->numfields) {
		_translate_firebird_type((istmt->osqlda->sqlvar[idx].sqltype & ~1), &fieldtype, &fieldattribs);
		_dbd_result_add_field(result, idx, istmt->osqlda->sqlvar[idx].sqlname, fieldtype, fieldattribs);
		idx++;
	}
}

int _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx) 
{
	unsigned int curfield = 0;
	XSQLVAR var;
	long fetch_stat = 0, blob_stat = 0;
	ISC_QUAD bid;
	isc_blob_handle blob_handle = NULL; /* Blob handle. */ 
	char blob_segment[80];
	unsigned short actual_seg_len;		
	struct tm times;
	char date_s[25];
	unsigned int sizeattrib;
	dbi_data_t *data = NULL;
	ibase_stmt_t *istmt = (ibase_stmt_t *)result->result_handle;
	ibase_conn_t *iconn = (ibase_conn_t *)result->conn->connection;

	fetch_stat = isc_dsql_fetch(iconn->status_vector, &(istmt->stmt), SQL_DIALECT_V6, istmt->osqlda);

	if (fetch_stat != 0) {
		result->numrows_matched--;
		return 0;
	}

	while (curfield < result->numfields) {
		var = istmt->osqlda->sqlvar[curfield];
		data = &row->field_values[curfield];

		/**
		 * If the column holds a NULL value mark it as NULL 
		 */
		if ( (var.sqltype & 1) && ( *var.sqlind < 0)) {
			_set_field_flag( row, curfield, DBI_VALUE_NULL, 1);
			curfield++;
			continue;
		}
		
		switch ( result->field_types[curfield] ) {
		case DBI_TYPE_STRING: 
			if(result->field_attribs[curfield] & DBI_STRING_FIXEDSIZE) {
				data->d_string = strdup(var.sqldata);
				data->d_string[var.sqllen] = '\0'; /* hack */
				row->field_sizes[curfield] = (unsigned long long) var.sqllen;
			} else {
				vary_t *vary = NULL;
				vary = (vary_t *) var.sqldata;
				data->d_string = malloc(vary->vary_length+1);
				memcpy(data->d_string, vary->vary_string, vary->vary_length);
				data->d_string[vary->vary_length] = '\0';
				row->field_sizes[curfield] = (unsigned long long) vary->vary_length;
			}
			break;
			
		case DBI_TYPE_INTEGER:
			sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_INTEGER_SIZE1, DBI_INTEGER_SIZE8);
			switch (sizeattrib) {
			case DBI_INTEGER_SIZE1:
				
			case DBI_INTEGER_SIZE2:
				data->d_short = *(short *) var.sqldata; break;
			case DBI_INTEGER_SIZE3:
			case DBI_INTEGER_SIZE4:
				data->d_long = *(int *) var.sqldata; break;
			case DBI_INTEGER_SIZE8:
				data->d_longlong = *(ISC_INT64 *) var.sqldata; break;
			default:
				break;
			}
			break;
		case DBI_TYPE_DECIMAL:
			sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_DECIMAL_SIZE4, DBI_DECIMAL_SIZE8);
			switch (sizeattrib) {
			case DBI_DECIMAL_SIZE4:
				data->d_float = *(float *) (var.sqldata); break;
			case DBI_DECIMAL_SIZE8:
				data->d_double = *(double *) (var.sqldata); break;
			default:
				break;
			}
			break;
		case DBI_TYPE_DATETIME:
			sizeattrib = _isolate_attrib(result->field_attribs[curfield], DBI_DATETIME_DATE, DBI_DATETIME_TIME);
			
			if (sizeattrib&DBI_DATETIME_TIME
			    && sizeattrib&DBI_DATETIME_DATE) {
 				isc_decode_timestamp((ISC_TIMESTAMP *)var.sqldata, &times);
				sprintf(date_s, "%04d-%02d-%02d %02d:%02d:%02d",
					times.tm_year + 1900,
					times.tm_mon+1,
					times.tm_mday,
					times.tm_hour,
					times.tm_min,
					times.tm_sec);
			}
			else if (sizeattrib&DBI_DATETIME_TIME) {
				isc_decode_sql_time((ISC_TIME *)var.sqldata, &times);
				sprintf(date_s, "%02d:%02d:%02d",
					times.tm_hour,
					times.tm_min,
					times.tm_sec);
			}
			else {
				isc_decode_sql_date((ISC_DATE *)var.sqldata, &times);
				sprintf(date_s, "%04d-%02d-%02d",
					times.tm_year + 1900,
					times.tm_mon+1,
					times.tm_mday);
			}
			data->d_datetime = _dbd_parse_datetime(date_s, sizeattrib);		
			break;
		
		case DBI_TYPE_BINARY:
			bid = *(ISC_QUAD *) var.sqldata;
			
			isc_open_blob2( iconn->status_vector, &(iconn->db), &(iconn->trans), &blob_handle, &bid, 0, NULL );
			blob_stat = isc_get_segment( iconn->status_vector, &blob_handle,  &actual_seg_len,  
						     sizeof(blob_segment), blob_segment  );

			data->d_string = malloc((size_t)actual_seg_len);
			memcpy(data->d_string, blob_segment, actual_seg_len);
			row->field_sizes[curfield] = actual_seg_len;

			while (blob_stat == 0 || iconn->status_vector[1] == isc_segment) {
				blob_stat = isc_get_segment(iconn->status_vector, &blob_handle, 
							    &actual_seg_len, 
							    sizeof(blob_segment), blob_segment); 
				if (!actual_seg_len) {
					break;
				}
				data->d_string = realloc(data->d_string, 
							 row->field_sizes[curfield] + 
							 actual_seg_len);
				memcpy(data->d_string+row->field_sizes[curfield], 
				       blob_segment, actual_seg_len);
				row->field_sizes[curfield] += actual_seg_len;
			} 
			isc_close_blob(iconn->status_vector, &blob_handle);

			/* terminate encoded blob string */
			data->d_string = realloc(data->d_string, (row->field_sizes[curfield])+1);
			data->d_string[row->field_sizes[curfield]] = '\0';

			row->field_sizes[curfield] = _dbd_decode_binary(data->d_string, data->d_string);
			break;
				
		default:
			break;
		}
		
		
		curfield++;
	}

	if( fetch_stat != 100L ) {
		result->rows = realloc(result->rows, (sizeof(dbi_row_t *) * (result->numrows_matched+1)));
		result->numrows_matched++;
	}

	return result->numrows_matched;
}

unsigned long long return_generator_value(dbi_conn_t *conn, const char *sequence, int type)
{
	unsigned long long retval = 0;
	char *sql_cmd = NULL;
	dbi_result_t *result;
	ibase_stmt_t *istmt = NULL;
	ibase_conn_t *iconn = conn->connection;

	asprintf(&sql_cmd, "SELECT GEN_ID( %s ,%d ) FROM RDB$DATABASE",sequence, type );	
	result = dbd_query(conn, sql_cmd);

	istmt = result->result_handle;
	if(! isc_dsql_fetch(iconn->status_vector, &(istmt->stmt), SQL_DIALECT_V6, istmt->osqlda) ) {
		retval = *(long *) istmt->osqlda->sqlvar[0].sqldata;
	}
	dbi_result_free(result);
	free(sql_cmd);
	return retval;
} 


void dealocate_iconn( ibase_conn_t *iconn )
{
	if( iconn != NULL) {
		if(iconn->charset) free ( (void *)iconn->charset );
		free(iconn);
		iconn = NULL;
	}
	return;
} 
