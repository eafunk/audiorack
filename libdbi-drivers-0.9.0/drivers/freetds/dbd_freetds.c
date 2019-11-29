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
 * dbd_freetds.c: MS SQL database support (using libct of FreeTDS library)
 * Copyright (C) Vadym Kononenko <konan_v@users.sourceforge.net>.
 * http://libdbi.sourceforge.net
 * 
 * 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE		/* we need asprintf */

#ifndef HAVE_ATOLL
long long atoll(const char *str);
#endif

#ifndef HAVE_STRTOLL
long long strtoll(const char *nptr, char **endptr, int base);
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

#include "dbd_freetds.h"
#include <ctpublic.h>

typedef struct freedts_type {
    CS_CONTEXT *ctx;
    CS_CONNECTION *conn;
    CS_COMMAND *cmd;
} FREETDSCON;
static FREETDSCON freetds;

static const dbi_info_t driver_info = {
    "freetds",
    "MS SQL and Sybase databases support (using libct)",
    "Vadym Kononenko <konan_v@users.sourceforge.net>",
    "http://libdbi.sourceforge.net",
    "dbd_freetds v" VERSION,
    __DATE__
};

static const char APP_NAME[] = "libdbi-freetds-driver";
static const char *custom_functions[] = { NULL };
static const char *reserved_words[] = TDS_RESERVED_WORDS;
static const char freetds_encoding_hash[][16] = {
    /* Sybase , www.iana.org *//* INTERNAL FREETDS NUMBER */
    /* Information from internal freetds header 'encodings.h' */
    "iso_1", "ISO-8859-1",	/*  0 */
    "ascii_8", "ISO-8859-1",	/*  0 */
    "utf8", "UTF-8",		/*  1 */
    "big5", "BIG-5",		/*  5 */
    "cp1250", "CP1250",		/*  15 */
    "cp1251", "CP1251",		/*  15 */
    "cp1252", "CP1252",		/*  15 */
    "cp1253", "CP1253",		/*  16 */
    "cp1254", "CP1254",		/*  17 */
    "cp1255", "CP1255",		/*  18 */
    "cp1256", "CP1256",		/*  19 */
    "cp1257", "CP1257",		/*  20 */
    "cp1258", "CP1258",		/*  21 */
    "cp437", "CP437",		/*  23 */
    "cp850", "CP850",		/*  24 */
    "cp862", "CP862",		/*  25 */
    "cp866", "CP866",		/*  26 */
    "cp874", "CP874",		/*  27 */
    "cp932", "CP932",		/*  28 */
    "cp936", "CP936",		/*  29 */
    "cp949", "CP949",		/*  30 */
    "cp950", "CP950",		/*  31 */
    "iso10", "ISO-8859-10",	/*  45 */
    "iso13", "ISO-8859-13",	/*  46 */
    "iso14", "ISO-8859-14",	/*  47 */
    "iso15", "ISO-8859-15",	/*  48 */
    "iso88592", "ISO-8859-2",	/*  50 */
    "iso88595", "ISO-8859-5",	/*  53 */
    "iso88596", "ISO-8859-6",	/*  54 */
    "iso88597", "ISO-8859-7",	/*  55 */
    "greek8", "ISO-8859-7",	/*  55 */
    "iso88598", "ISO-8859-8",	/*  56 */
    "iso88599", "ISO-8859-9",	/*  57 */
    "tis620", "ISO-IR-166",	/*  61 */
    "koi8", "KOI8-R",		/*  65 */
    "mac", "MAC",		/*  69 */
    "mac_cyr", "MACCYRILLIC",	/*  73 */
    "macgreek", "MACGREEK",	/*  74 */
    "macthai", "MACTHAI",	/*  78 */
    "macturk", "MACTURKISH",	/*  79 */
    "roman8", "ROMAN8",		/*  83 */
    "sjis", "SJIS",		/*  84 */
    "iso646", "US-ASCII",	/*  94 */
    "", ""
};
void _dbd_free_row(dbi_result_t * result, dbi_row_t * row);

void _translate_freetds_type(CS_DATAFMT * datafmt, unsigned short *type, unsigned int *attribs);

size_t _dbd_freetds_escape_chars(char *dest, const char *orig, size_t orig_size,
				 const char *toescape);

dbi_row_t *_dbd_freetds_buffers_binding(dbi_conn_t * conn, dbi_result_t * result,
					CS_DATAFMT ** datafmt, CS_INT * datalength,
					CS_SMALLINT * ind, CS_RETCODE * ret);

void dbd_register_driver(const dbi_info_t ** _driver_info, const char ***_custom_functions,
			 const char ***_reserved_words)
{
    /* this is the first function called after the driver module is loaded into memory */
    *_driver_info = &driver_info;
    *_custom_functions = custom_functions;
    *_reserved_words = reserved_words;
}

int dbd_initialize(dbi_driver_t * driver)
{
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

int dbd_connect(dbi_conn_t * conn)
{

    FREETDSCON *tdscon = &freetds;
    CS_RETCODE ret;

    char *str;
    unsigned int num;
/*
 * Allocate memory for structs
 */
    if (cs_ctx_alloc(CS_VERSION_100, &tdscon->ctx) == CS_SUCCEED) {
	if (ct_init(tdscon->ctx, CS_VERSION_100) == CS_SUCCEED) {
	    if (ct_con_alloc(tdscon->ctx, &tdscon->conn) == CS_SUCCEED) {
		if (ct_cmd_alloc(tdscon->conn, &tdscon->cmd) == CS_SUCCEED) {
		    goto success_allocate;
		}
		/* Deallocate "conn" struct */
		ct_con_drop(tdscon->conn);
	    }
	    /* Go out from client library */
	    ct_exit(tdscon->ctx, CS_UNUSED);
	}
	/* Deallocate "ctx" struct */
	cs_ctx_drop(tdscon->ctx);
    }

    return -1;
  success_allocate:
    conn->connection = tdscon;

    /* Set parameters for login */
    /* USERNAME */
    ret = ct_con_props(tdscon->conn, CS_SET, CS_USERNAME,
		       (str = (char *) dbi_conn_get_option(conn, "username")) ? str : "",
		       CS_NULLTERM, NULL);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_con_props() SET USERNAME failed!\n");
	return -1;
    }

    /* PASSWORD */
    ret = ct_con_props(tdscon->conn, CS_SET, CS_PASSWORD,
		       (str = (char *) dbi_conn_get_option(conn, "password")) ? str : "",
		       CS_NULLTERM, NULL);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_con_props() SET PASSWORD failed!\n");
	return -1;
    }

    /* CHARSET or locale? */
    /* Describe all locales and charsets in locales.conf file */
/*    ret = ct_con_props(tdscon->conn, CS_SET, CS_LOC_PROP,
		       (str = (char *) dbi_conn_get_encoding(conn)) ? str : "",
		       CS_NULLTERM, NULL);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_con_props() SET LOCALE failed!\n");
	return -1;
    }
*/
    if ((str = (char *) dbi_conn_get_option(conn, "freetds_version"))) {
    	/* Last VERSION supported by ctlib is 7.0 */
	// Format = X.XX.XX
	switch (str[0]) {
//      We can use it if "CS_TDS_80" will be add to ct_con_props() by CS_SET action
	case '8':
	    num = CS_TDS_80;
	    break;
	case '7':
	    num = CS_TDS_70;
	    break;
	case '5':
	    num = CS_TDS_50;
	    break;
	case '4':
	    {
		switch (str[2]) {
		case '6':
		    num = CS_TDS_46;
		    break;
		case '9':
		    num = CS_TDS_495;
		    break;
		case '0':
		case '2':
		default:
		    num = CS_TDS_40;
		}
	    }
	    break;
	default:
	    num = CS_TDS_40;
	}

	ret = ct_con_props(tdscon->conn, CS_SET, CS_TDS_VERSION, &num, CS_NULLTERM, NULL);
	if (ret != CS_SUCCEED) {
	    // fprintf(stderr, "ct_con_props() SET VERSION failed!\n");
	    return -1;
	}
    }

    /* Connect to CLI */
    ret = ct_connect(tdscon->conn,
		     (str = (char *) dbi_conn_get_option(conn, "host")) ? str : "", CS_NULLTERM);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_connect() failed!\n");
	return -1;
    }

    return 0;
}

int dbd_disconnect(dbi_conn_t * conn)
{
    FREETDSCON *tdscon = (FREETDSCON *) conn->connection;

    if (ct_cancel(tdscon->conn, NULL, CS_CANCEL_ALL) != CS_SUCCEED) {
	return 1;
    }

    ct_cmd_drop(tdscon->cmd);
    ct_close(tdscon->conn, CS_UNUSED);
    ct_con_drop(tdscon->conn);
    ct_exit(tdscon->ctx, CS_UNUSED);
    cs_ctx_drop(tdscon->ctx);

    return 0;
}

int dbd_fetch_row(dbi_result_t * result, unsigned long long rownum)
{
    if (result->result_state == NOTHING_RETURNED)
	return -1;

    if (result->result_state == ROWS_RETURNED)
	/* We have already all rows writed to result */
	return 1;
    return 0;
    /* 0 on error, 1 on successful fetchrow */
}

int dbd_free_query(dbi_result_t * result)
{
    /* We don't use any additional memory buffers */
    return 0;
}

int dbd_goto_row(dbi_result_t * result, unsigned long long rowidx, unsigned long long currowidx)
{
  /* no-op */
  return 1;
}

int dbd_get_socket(dbi_conn_t * conn)
{
/*    FREETDSCON *tdscon = (FREETDSCON *) conn->connection;

    if (!(tdscon || tdscon->conn || tdscon->conn->tds_socket))
	return -1;

    return (int) tdscon->conn->tds_socket->s;
*/
/* 
 * ctlib not allowed to do it and we can't use internal 
 * header ctlib.h for direct acceess to tds structs
 */
    return 0;
}

const char *dbd_get_encoding(dbi_conn_t * conn)
{
    FREETDSCON *tdscon = (FREETDSCON *) conn->connection;
    const char *tds_enc = NULL;
    CS_RETCODE ret;

/*    if (!(tdscon || tdscon->conn || tdscon->conn->tds_login))
	return NULL;
*/
    /* Freetds library use iconv for converting 
     * different charsets between client and server
     * (if compiled with HAVE_ICONV_ALWAYS and clien_charset is set)
     * server_charset setted as current clients locale...
     */
/*    if ((((tds_enc = (const char *) tdscon->conn->tds_login->client_charset)) && strlen(tds_enc)) &&
	((tds_enc = (const char *) tdscon->conn->tds_login->server_charset)) && strlen(tds_enc))
	return dbd_encoding_to_iana(tds_enc);
*/

/* in ct_con_props(..., CS_VOID* buffer, ...) allocated the mistake */
/*                case CS_LOC_PROP:
                        buffer = (CS_VOID *) con->locale;
		    break;
*/
/* I think there should be "*buffer = (CS_VOID *) con->locale;" */
    ret = ct_con_props(tdscon->conn, CS_GET, CS_LOC_PROP, &tds_enc, CS_NULLTERM, NULL);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_con_props() SET LOCALE failed!\n");
	return NULL;
    }
    return tds_enc ? dbd_encoding_to_iana(tds_enc) : NULL;
}

const char *dbd_encoding_to_iana(const char *db_encoding)
{
    int i = 0;

    /* loop over all even entries in hash and compare to menc */
    while (*freetds_encoding_hash[i]) {
	if (!strncmp(freetds_encoding_hash[i], db_encoding, strlen(freetds_encoding_hash[i]))) {
	    /* return corresponding odd entry */
	    return freetds_encoding_hash[i + 1];
	}
	i += 2;
    }

    /* don't know how to translate, return original encoding */
    return db_encoding;
}

const char *dbd_encoding_from_iana(const char *iana_encoding)
{
    int i = 0;

    /* loop over all odd entries in hash and compare to ienc */
    while (*freetds_encoding_hash[i + 1]) {
	if (!strcmp(freetds_encoding_hash[i + 1], iana_encoding)) {
	    /* return corresponding even entry */
	    return freetds_encoding_hash[i];
	}
	i += 2;
    }

    /* don't know how to translate, return original encoding */
    return iana_encoding;
}

char *dbd_get_engine_version(dbi_conn_t * conn, char *versionstring)
{
    dbi_result_t *dbi_result;
    const char *versioninfo = NULL;

    /* initialize return string */
    *versionstring = '\0';

    dbi_result = dbd_query(conn, "select @@version");

    if (dbi_result) {
	if (dbi_result_next_row(dbi_result)) {
	    char *start = NULL;
	    char *stop = NULL;
	    int len = 0;
	    versioninfo = dbi_result_get_string_idx(dbi_result, 1);

	    /* try to locate the version number. Look for the first dot, go
	       back where the number before the dot starts, then walk
	       forward to the last dot or number */
	    start = strchr(versioninfo, (int) '.');
	    if (start) {
		while (--start > versioninfo && isdigit((int) (*start)));

		stop = ++start;
		while (*stop && (isdigit((int) (*stop)) || *stop == '.')) {
		    stop++;
		}

		len = stop - start;
		/* Last digit not copied to versionstring */
		if (len && --len < VERSIONSTRING_LENGTH) {
		    strncpy(versionstring, start, len);
		    versionstring[len] = '\0';
		}
	    }
	}
	dbi_result_free(dbi_result);
    }

    return versionstring;
}

dbi_result_t *dbd_list_dbs(dbi_conn_t * conn, const char *pattern)
{
    dbi_result_t *res;
    char *sql_cmd;

    if (pattern == NULL) {

	return dbd_query(conn, "exec sp_databases");
    } else {
	/* Only this way - nothing another */
	asprintf(&sql_cmd,
		 " create table #t (\n"
		 "		    DATABASE_NAME sysname NOT NULL,\n"
		 "		    DATABASE_SIZE int NOT NULL,\n"
		 "		    REMARKS varchar(254)\n"
		 "		  )\n"
		 " Insert Into #t exec sp_databases\n"
		 " Select * From #t Where DATABASE_NAME Like '%%%s%%'\n" " Drop table #t", pattern);
	res = dbd_query(conn, sql_cmd);
	free(sql_cmd);
	return res;
    }
}

dbi_result_t *dbd_list_tables(dbi_conn_t * conn, const char *db, const char *pattern)
{
    dbi_result_t *res;
    char *sql_cmd;

    if (db == NULL || db[0] == '\0') {
	/* Use current database */
	res = dbd_query(conn, "EXEC sp_tables");
	return res;
    }

    char *current_db = NULL;
    if (conn->current_db)
	/* Reserved current DB name */
	current_db = strdup(conn->current_db);
    /* MS SQL can show tables only from current DB */
    dbd_select_db(conn, db);

    if (pattern == NULL) {
	asprintf(&sql_cmd, "create table #t (\n"
		 "		    TABLE_QUALIFIER sysname,\n"
		 "		    TABLE_OWNER sysname,\n"
		 "		    TABLE_NAME sysname NOT NULL,\n"
		 "		    TABLE_TYPE sysname,\n"
		 "		    REMARKS varchar(254)\n"
		 "		   )\n"
		 "Insert Into #t exec sp_tables\n"
		 "Select TABLE_NAME From #t Where TABLE_TYPE='TABLE'\n" "Drop table #t\n");
	res = dbd_query(conn, sql_cmd);
	free(sql_cmd);
    } else {
	asprintf(&sql_cmd, "create table #t (\n"
		 "		    TABLE_QUALIFIER sysname,\n"
		 "		    TABLE_OWNER sysname,\n"
		 "		    TABLE_NAME sysname NOT NULL,\n"
		 "		    TABLE_TYPE sysname,\n"
		 "		    REMARKS varchar(254)\n"
		 "		   )\n"
		 "Insert Into #t exec sp_tables\n"
		 "Select TABLE_NAME From #t Where TABLE_TYPE='TABLE' And TABLE_NAME Like '%%%s%%'\n"
		 "Drop table #t\n", pattern);
	res = dbd_query(conn, sql_cmd);
	free(sql_cmd);
    }

    if (current_db) {
	/* Restore previous DB */
	dbd_select_db(conn, current_db);
	free(current_db);
    }
    return res;
}



size_t dbd_quote_string(dbi_driver_t * driver, const char *orig, char *dest)
{
    /* foo's -> 'foo''s' */
    size_t len;

    strcpy(dest, "\'");
    const char *escaped = "\'";

    len = _dbd_freetds_escape_chars(dest + 1, orig, strlen(orig), escaped);

    strcat(dest, "'");

    return len + 2;
}

size_t dbd_conn_quote_string(dbi_conn_t * conn, const char *orig, char *dest)
{
    return dbd_quote_string(conn->driver, orig, dest);
}

size_t dbd_quote_binary(dbi_conn_t * conn, const unsigned char *orig, size_t from_length,
			unsigned char **ptr_dest)
{
    unsigned char *temp;
    size_t len;

    if ((temp = malloc(from_length * 2)) == NULL) {
	return 0;
    }

    strcpy((char *) temp, "\'");
    if (from_length) {
	len = _dbd_encode_binary(orig, from_length, temp + 1);
    } else {
	len = 0;
    }
    strcat((char *) temp, "'");

    *ptr_dest = temp;

    return len + 2;
}

/* 
 * Here is a roblem - freetds return only one row and we should step
 * by step get all rows and save it separately
 */
dbi_result_t *dbd_query(dbi_conn_t * conn, const char *statement)
{
    /* allocate a new dbi_result_t and fill its applicable members:
     * 
     * result_handle, numrows_matched, and numrows_changed.
     * everything else will be filled in by DBI */

    unsigned int idx = 0;
    dbi_result_t *result = NULL;
    dbi_row_t *row = NULL;
    FREETDSCON *tdscon = (FREETDSCON *) conn->connection;
    CS_RETCODE ret, results_ret;
    CS_INT result_type, num_cols;
    CS_DATAFMT **datafmt = NULL;
    CS_INT datalength = 0;
    CS_SMALLINT ind = 0;
    CS_INT count, row_count = 0;

    ret = ct_command(tdscon->cmd, CS_LANG_CMD, statement, CS_NULLTERM, CS_UNUSED);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_command() failed\n");
	return NULL;
    }

    ret = ct_send(tdscon->cmd);
    if (ret != CS_SUCCEED) {
	// fprintf(stderr, "ct_send() failed\n");
	return NULL;
    }

    while ((results_ret = ct_results(tdscon->cmd, &result_type)) == CS_SUCCEED) {
	switch ((int) result_type) {
	case CS_ROWFMT_RESULT:
	    /* here we should allocate memory for received data */
	    break;
	case CS_CMD_SUCCEED:
	    if (!result) {
		if (!((result = _dbd_result_create(conn, NULL, 0 /* Unknown num rows */ ,
						   0 /*numrows_affected */ ))))
		    /* Out of memory */
		    return NULL;
		result->result_state = NOTHING_RETURNED;
	    }
	    break;
	case CS_CMD_DONE:
	    break;
	case CS_CMD_FAIL:
	    /* ? */
	    return NULL;
	    break;
	case CS_ROW_RESULT:
	    ret = ct_res_info(tdscon->cmd, CS_NUMDATA, &num_cols, CS_UNUSED, NULL);
	    if (ret != CS_SUCCEED) {
		// fprintf(stderr, "ct_res_info() failed");
		return NULL;
	    }

	    if (!(result = _dbd_result_create(conn, NULL, 0 /* Unknown num rows */ ,
					      0 /*numrows_affected */ )))
		/* Out of memory */
		return NULL;
	    result->result_state = ROWS_RETURNED;

	    _dbd_result_set_numfields(result, num_cols);

	    /* Fill columns type */
	    for (idx = 0; idx < result->numfields; ++idx) {
		/* Get information about current column */
		datafmt = realloc(datafmt, sizeof(CS_DATAFMT *) * (idx + 1));
		datafmt[idx] = malloc(sizeof(CS_DATAFMT));

		ret = ct_describe(tdscon->cmd, idx + 1, datafmt[idx]);
		if (ret != CS_SUCCEED) {
		    // fprintf(stderr, "ct_describe() failed");
		    return NULL;
		}

		unsigned short type = 0;
		unsigned int attribs = 0;

		_translate_freetds_type(datafmt[idx], &type, &attribs);
		/* Fill fields value in result */
		_dbd_result_add_field(result, idx, datafmt[idx]->name, type, attribs);
	    }

	    /* Bind all column buffers for current row */
	    if (!(row = _dbd_freetds_buffers_binding(conn, result, datafmt, &datalength,
						     &ind, &ret)))
		return NULL;

	    while (((ret =
		     ct_fetch(tdscon->cmd, CS_UNUSED, CS_UNUSED, CS_UNUSED, &count)) == CS_SUCCEED)
		   || (ret == CS_ROW_FAIL)) {
		row_count += count;
		if (ret == CS_ROW_FAIL) {
		    // fprintf(stderr, "ct_fetch() CS_ROW_FAIL on row %d.\n", row_count);
		    return NULL;
		} else if (ret == CS_SUCCEED) {
		    /* Save row */
		    if (!(result->rows = realloc(result->rows, (result->numrows_matched + 2)
						 * sizeof(dbi_row_t *))))
			/* Out of memory */
			return NULL;

		    _dbd_row_finalize(result, row, result->numrows_matched++);

		    /* Prepare buffer for next row */
		    if (!(row = _dbd_freetds_buffers_binding(conn, result, datafmt, &datalength,
							     &ind, &ret)))
			return NULL;

		} else {
		    break;
		}
	    }
	    /* Last return */
	    switch ((int) ret) {
	    case CS_END_DATA:
		/* Last row is reserved - free it */
		_dbd_free_row(result, row);

		for (idx = 0; idx < result->numfields; ++idx)
		    free(datafmt[idx]);
		free(datafmt);
		break;
	    case CS_FAIL:
		// fprintf(stderr, "ct_fetch() returned CS_FAIL.\n");
		return NULL;
	    default:
		// fprintf(stderr, "ct_fetch() unexpected return.\n");
		return NULL;
	    }
	    break;
	case CS_COMPUTE_RESULT:
	    // fprintf(stderr, "ct_results() unexpected CS_COMPUTE_RESULT.\n");
	    return NULL;
	case CS_STATUS_RESULT:
	    break;
	default:
	    // fprintf(stderr, "ct_results() unexpected result_type.\n");
	    return NULL;
	}
    }

    switch ((int) results_ret) {
    case CS_END_RESULTS:
	break;
    case CS_FAIL:
	return NULL;
	break;
    default:
	return NULL;
    }
//    ++(result->numrows_affected);
//    result->result_handle = tdscon->tds_socket /*->res_info*/ ;

    return result;
}

dbi_result_t *dbd_query_null(dbi_conn_t * conn, const unsigned char *statement, size_t st_length)
{
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
  if (dbd_query(conn, "COMMIT TRANSACTION") == NULL) {
    return 1;
  }
  else {
    return 0;
  }
}

int dbd_transaction_rollback(dbi_conn_t *conn) {
  if (dbd_query(conn, "ROLLBACK TRANSACTION") == NULL) {
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

  asprintf(&query, "SAVE TRANSACTION %s", savepoint);

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

  asprintf(&query, "ROLLBACK TRANSACTION %s", savepoint);

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
  /* this is apparently not used by SYBASE/SQL Server */
        return 0;
}

const char *dbd_select_db(dbi_conn_t * conn, const char *db)
{
    dbi_result_t *res;
    char *sql_cmd;

    asprintf(&sql_cmd, "USE %s ", db);
    res = dbd_query(conn, sql_cmd);
    free(sql_cmd);

    if (!res) {
	return NULL;
    }

    dbi_result_free(res);
    return (char *) db;
}

int dbd_geterror(dbi_conn_t * conn, int *err_no, char **errstr)
{
    /* put error number into errno, error string into errstr
     * return 0 if error, 1 if err_no filled, 2 if errstr filled, 3 if both err_no and errstr filled */

    /* We havn't functions for read error types in freetds */
    return -1;
}

unsigned long long dbd_get_seq_last(dbi_conn_t * conn, const char *sequence)
{
//    FREETDSCON *tdscon = (FREETDSCON *) conn->connection;
/*    int len = 0;

    if (sequence)
	len = strlen(sequence);

    return tds_quote_id(tdscon->tds_socket, NULL, sequence, len) ;
*/
    return 0;
}

unsigned long long dbd_get_seq_next(dbi_conn_t * conn, const char *sequence)
{
    return 0;
}

int dbd_ping(dbi_conn_t * conn)
{

    int res;
    /* Freetds haven't ping function - use simple query */
    /* Maybe get another simple query? */
    res = dbd_query(conn, "SELECT 1") ? 1 : 0;

    if (!res) {
	// server is alive and kicking
	dbd_disconnect(conn);
	res = dbd_connect(conn);
	return (res ? 0 : res);
    }
    return res;
}

void _translate_freetds_type(CS_DATAFMT * datafmt, unsigned short *type, unsigned int *attribs)
{
    unsigned int _type = 0;
    unsigned int _attribs = 0;
    datafmt->format = CS_FMT_UNUSED;

    switch (datafmt->datatype /* field type */ ) {

    case CS_LONG_TYPE:		/* 8 */
	_type = DBI_TYPE_INTEGER;
	_attribs |= DBI_INTEGER_SIZE8;
	break;
    case CS_INT_TYPE:		/* 4 */
	_type = DBI_TYPE_INTEGER;
	_attribs |= DBI_INTEGER_SIZE4;
	break;
    case CS_SMALLINT_TYPE:	/* 2 */
	_type = DBI_TYPE_INTEGER;
	_attribs |= DBI_INTEGER_SIZE2;
	break;
    case CS_BIT_TYPE:		/* 1 */
    case CS_TINYINT_TYPE:
	_type = DBI_TYPE_INTEGER;
	_attribs |= DBI_INTEGER_SIZE1;
	break;
    case CS_DATETIME_TYPE:	/* 8 */
	_type = DBI_TYPE_DATETIME;
	_attribs |= DBI_DATETIME_DATE;
	_attribs |= DBI_DATETIME_TIME;
	break;
    case CS_DATETIME4_TYPE:	/* 4 */
	_type = DBI_TYPE_DATETIME;
	_attribs |= DBI_DATETIME_DATE;
	_attribs |= DBI_DATETIME_TIME;
	break;
    case CS_REAL_TYPE:		/* 4 */
	_type = DBI_TYPE_DECIMAL;
	_attribs |= DBI_DECIMAL_SIZE4;
	break;
    case CS_FLOAT_TYPE:	/* 8 */
	_type = DBI_TYPE_DECIMAL;
	_attribs |= DBI_DECIMAL_SIZE8;
	break;
    case CS_CHAR_TYPE:
    case CS_TEXT_TYPE:
    case CS_VARCHAR_TYPE:
	_type = DBI_TYPE_STRING;
	datafmt->format = CS_FMT_NULLTERM;
	++datafmt->maxlength; /* 1 extra byte for \0 */
	break;
    case CS_MONEY_TYPE:	/* 8 */
    case CS_MONEY4_TYPE:	/* 4 */
    case CS_NUMERIC_TYPE:
    case CS_DECIMAL_TYPE:
    case CS_UNIQUE_TYPE:
    case CS_IMAGE_TYPE:
    case CS_BINARY_TYPE:
    case CS_VARBINARY_TYPE:
    case CS_UNICHAR_TYPE:

    default:
	_type = DBI_TYPE_BINARY;
    }

    *type = _type;
    *attribs = _attribs;
}

size_t _dbd_freetds_escape_chars(char *dest, const char *orig, size_t orig_size,
				 const char *toescape)
{
    char *curdest = dest;
    const char *curorig = orig;
    const char *curescaped;
    size_t len = 0;

    while (curorig && curorig < orig + orig_size) {
	curescaped = toescape;
	while (curescaped && *curescaped) {
	    if (*curorig == *curescaped) {
		*(curdest++) = '\'';
		len++;
		break;
	    }
	    curescaped++;
	}
	/* Copy char to destination */
	*curdest = *curorig;

	curorig++;
	curdest++;
	len++;
    }

    /* append a NULL byte. This is required if orig was a
       zero-terminated string. It does not hurt if orig was a
       binary string as the calling function is not supposed to
       read past len bytes */
    *curdest = '\0';
    return len;
}

dbi_row_t *_dbd_freetds_buffers_binding(dbi_conn_t * conn, dbi_result_t * result,
					CS_DATAFMT ** datafmt, CS_INT * datalength,
					CS_SMALLINT * ind, CS_RETCODE * ret)
{
    dbi_row_t *row = NULL;
    unsigned int idx = 0;
    void *addr = NULL;

    FREETDSCON *tdscon = (FREETDSCON *) conn->connection;

    /* Allocate memory for current row */
    if (!(row = _dbd_row_allocate(result->numfields)))
	/* Out of memory */
	return NULL;
    for (idx = 0; idx < result->numfields; ++idx) {
	/* Processing data from previous row */
	if (result->numrows_matched > 0) {
	    /* 
	     * We should convert data from previous row 
	     * to libdbi datatypes. We converted types here 
	     * because we should know CS_ data types.
	     */
	    switch (datafmt[idx]->datatype) {
	    case CS_MONEY_TYPE:	/* 8 */
	    case CS_MONEY4_TYPE:	/* 4 */
		{
		    CS_DATAFMT dstfmt;

		    dstfmt.datatype = CS_NUMERIC_TYPE;
		    dstfmt.maxlength = sizeof(CS_NUMERIC_TYPE);
		    dstfmt.locale = NULL;
		    dstfmt.format = CS_FMT_UNUSED;

		    addr = malloc(sizeof(CS_NUMERIC_TYPE));
		    char **orig =
			&(result->rows[result->numrows_matched]->field_values[idx].d_string);

		    if (cs_convert(tdscon->ctx, datafmt[idx], *orig, &dstfmt, addr, NULL) !=
			CS_SUCCEED) {
			// fprintf(stderr, "cs_convert() succeeded when failure was expected\n");
		    }
		    free(*orig);
		    *orig = addr;
		}
		break;
	    case CS_DATETIME_TYPE:	/* 8 */
	    case CS_DATETIME4_TYPE:	/* 4 */
		{
		    char datastring[20];
		    CS_DATAFMT dstfmt;

		    dstfmt.datatype = CS_TEXT_TYPE;
		    dstfmt.maxlength = 20;
		    dstfmt.locale = NULL;
		    dstfmt.format = CS_FMT_NULLTERM;

		    addr = &(result->rows[result->numrows_matched]->field_values[idx]);

		    if (cs_convert(tdscon->ctx, datafmt[idx], addr, &dstfmt, datastring, NULL) !=
			CS_SUCCEED) {
			// fprintf(stderr, "cs_convert() succeeded when failure was expected\n");
		    }
		    ((dbi_data_t *) addr)->d_datetime =
			_dbd_parse_datetime(datastring, DBI_DATETIME_TIME | DBI_DATETIME_DATE);
		}
		break;
		/* decode binary string */
	    case CS_UNIQUE_TYPE:
	    case CS_IMAGE_TYPE:
	    case CS_BINARY_TYPE:
	    case CS_VARBINARY_TYPE:
	    case CS_UNICHAR_TYPE:

		addr = result->rows[result->numrows_matched];


		((dbi_row_t *) addr)->field_sizes[idx] =
		    _dbd_decode_binary(((dbi_row_t *) addr)->field_values[idx].d_string,
				       ((dbi_row_t *) addr)->field_values[idx].d_string);
		break;
	    case CS_CHAR_TYPE:
	    case CS_TEXT_TYPE:
	    case CS_VARCHAR_TYPE:
		addr = result->rows[result->numrows_matched];

		((dbi_row_t *) addr)->field_sizes[idx] =
		    strlen(((dbi_row_t *) addr)->field_values[idx].d_string);
		break;
	    }
	}

	/* Bind all columns buffer for current row */
	row->field_sizes[idx] = datafmt[idx]->maxlength;

	switch (result->field_types[idx]) {
	case DBI_TYPE_BINARY:
	case DBI_TYPE_STRING:
	    /* 
	     * Result is more that 8 bytes - 
	     * allocate additional memory
	     * 1 extra byte for \0
	     */
	    addr = row->field_values[idx].d_string = (char *) malloc(row->field_sizes[idx] + 1);
	    break;
	default:
	    /* Prepare union to data copy */
	    bzero((addr = &row->field_values[idx]), sizeof(dbi_data_t));
	}
	*ret = ct_bind(tdscon->cmd, idx + 1, datafmt[idx], addr, datalength, ind);
	if (*ret != CS_SUCCEED) {
	    // fprintf(stderr, "ct_bind() failed\n");
	    return NULL;
	}
    }
    /* All columns buffer binded */
    return row;
}

void _dbd_free_row(dbi_result_t * result, dbi_row_t * row)
{
    unsigned int idx = 0;

    for (idx = 0; idx < result->numfields; ++idx) {
	if (result->field_types[idx] == DBI_TYPE_BINARY ||
	    result->field_types[idx] == DBI_TYPE_STRING) {
	    free(row->field_values[idx].d_string);
	}
    }
    free(row->field_values);
    free(row->field_sizes);
    free(row->field_flags);
    free(row);
}
