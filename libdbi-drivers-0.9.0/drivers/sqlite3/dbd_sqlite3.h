/*
 * libdbi - database independent abstraction layer for C.
 * Copyright (C) 2001, David Parker and Mark Tobenkin.
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
 * dbd_sqlite3.c: SQLite3 database support (using libsqlite3) header file
 * Copyright (C) 2005-2007, Markus Hoenicka <mhoenicka@users.sourceforge.net>
 * http://libdbi.sourceforge.net
 *
 * $Id: dbd_sqlite3.h,v 1.4 2008/07/05 19:49:53 mhoenicka Exp $
 */

enum enum_field_types { FIELD_TYPE_DECIMAL, FIELD_TYPE_TINY,
			FIELD_TYPE_SHORT,  FIELD_TYPE_LONG,
			FIELD_TYPE_FLOAT,  FIELD_TYPE_DOUBLE,
			FIELD_TYPE_NULL,   FIELD_TYPE_TIMESTAMP,
			FIELD_TYPE_LONGLONG,FIELD_TYPE_INT24,
			FIELD_TYPE_DATE,   FIELD_TYPE_TIME,
			FIELD_TYPE_DATETIME, FIELD_TYPE_YEAR,
			FIELD_TYPE_NEWDATE,
			FIELD_TYPE_ENUM=247,
			FIELD_TYPE_SET=248,
			FIELD_TYPE_TINY_BLOB=249,
			FIELD_TYPE_MEDIUM_BLOB=250,
			FIELD_TYPE_LONG_BLOB=251,
			FIELD_TYPE_BLOB=252,
			FIELD_TYPE_VAR_STRING=253,
			FIELD_TYPE_STRING=254
};

/* this is the maximum length of identifiers like table names and
   column names. SQLite does not seem to have any limit like this, but
   other systems use limits like 32 (PostgreSQL) and 64 (MySQL) */
#define MAX_IDENT_LENGTH 128
#define MAX_TABLES_IN_QUERY 50

/* this is the maximum length of a path that the system is guaranteed
   to handle. This is usually defined in limits.h but appears to be
   missing e.g. from MinGW's version of this file */
#ifndef _POSIX_PATH_MAX
#define _POSIX_PATH_MAX 256
#endif

#define SQLITE3_RESERVED_WORDS { \
	"ACTION", \
	"ADD", \
	"AGGREGATE", \
	"ALL", \
	"ALTER", \
	"AFTER", \
	"AND", \
	"AS", \
	"ASC", \
	"AVG", \
	"AVG_ROW_LENGTH", \
	"AUTO_INCREMENT", \
	"BETWEEN", \
	"BIGINT", \
	"BIT", \
	"BINARY", \
	"BLOB", \
	"BOOL", \
	"BOTH", \
	"BY", \
	"CASCADE", \
	"CASE", \
	"CHAR", \
	"CHARACTER", \
	"CHANGE", \
	"CHECK", \
	"CHECKSUM", \
	"COLUMN", \
	"COLUMNS", \
	"COMMENT", \
	"CONSTRAINT", \
	"CREATE", \
	"CROSS", \
	"CURRENT_DATE", \
	"CURRENT_TIME", \
	"CURRENT_TIMESTAMP", \
	"DATA", \
	"DATABASE", \
	"DATABASES", \
	"DATE", \
	"DATETIME", \
	"DAY", \
	"DAY_HOUR", \
	"DAY_MINUTE", \
	"DAY_SECOND", \
	"DAYOFMONTH", \
	"DAYOFWEEK", \
	"DAYOFYEAR", \
	"DEC", \
	"DECIMAL", \
	"DEFAULT", \
	"DELAYED", \
	"DELAY_KEY_WRITE", \
	"DELETE", \
	"DESC", \
	"DESCRIBE", \
	"DISTINCT", \
	"DISTINCTROW", \
	"DOUBLE", \
	"DROP", \
	"END", \
	"ELSE", \
	"ESCAPE", \
	"ESCAPED", \
	"ENCLOSED", \
	"ENUM", \
	"EXPLAIN", \
	"EXISTS", \
	"FIELDS", \
	"FILE", \
	"FIRST", \
	"FLOAT", \
	"FLOAT4", \
	"FLOAT8", \
	"FLUSH", \
	"FOREIGN", \
	"FROM", \
	"FOR", \
	"FULL", \
	"FUNCTION", \
	"GLOBAL", \
	"GRANT", \
	"GRANTS", \
	"GROUP", \
	"HAVING", \
	"HEAP", \
	"HIGH_PRIORITY", \
	"HOUR", \
	"HOUR_MINUTE", \
	"HOUR_SECOND", \
	"HOSTS", \
	"IDENTIFIED", \
	"IGNORE", \
	"IN", \
	"INDEX", \
	"INFILE", \
	"INNER", \
	"INSERT", \
	"INSERT_ID", \
	"INT", \
	"INTEGER", \
	"INTERVAL", \
	"INT1", \
	"INT2", \
	"INT3", \
	"INT4", \
	"INT8", \
	"INTO", \
	"IF", \
	"IS", \
	"ISAM", \
	"JOIN", \
	"KEY", \
	"KEYS", \
	"KILL", \
	"LAST_INSERT_ID", \
	"LEADING", \
	"LEFT", \
	"LENGTH", \
	"LIKE", \
	"LINES", \
	"LIMIT", \
	"LOAD", \
	"LOCAL", \
	"LOCK", \
	"LOGS", \
	"LONG", \
	"LONGBLOB", \
	"LONGTEXT", \
	"LOW_PRIORITY", \
	"MAX", \
	"MAX_ROWS", \
	"MATCH", \
	"MEDIUMBLOB", \
	"MEDIUMTEXT", \
	"MEDIUMINT", \
	"MIDDLEINT", \
	"MIN_ROWS", \
	"MINUTE", \
	"MINUTE_SECOND", \
	"MODIFY", \
	"MONTH", \
	"MONTHNAME", \
	"MYISAM", \
	"NATURAL", \
	"NUMERIC", \
	"NO", \
	"NOT", \
	"NULL", \
	"ON", \
	"OPTIMIZE", \
	"OPTION", \
	"OPTIONALLY", \
	"OR", \
	"ORDER", \
	"OUTER", \
	"OUTFILE", \
	"PACK_KEYS", \
	"PARTIAL", \
	"PASSWORD", \
	"PRECISION", \
	"PRIMARY", \
	"PROCEDURE", \
	"PROCESS", \
	"PROCESSLIST", \
	"PRIVILEGES", \
	"READ", \
	"REAL", \
	"REFERENCES", \
	"RELOAD", \
	"REGEXP", \
	"RENAME", \
	"REPLACE", \
	"RESTRICT", \
	"RETURNS", \
	"REVOKE", \
	"RLIKE", \
	"ROW", \
	"ROWS", \
	"SECOND", \
	"SELECT", \
	"SET", \
	"SHOW", \
	"SHUTDOWN", \
	"SMALLINT", \
	"SONAME", \
	"SQL_BIG_TABLES", \
	"SQL_BIG_SELECTS", \
	"SQL_LOW_PRIORITY_UPDATES", \
	"SQL_LOG_OFF", \
	"SQL_LOG_UPDATE", \
	"SQL_SELECT_LIMIT", \
	"SQL_SMALL_RESULT", \
	"SQL_BIG_RESULT", \
	"SQL_WARNINGS", \
	"STRAIGHT_JOIN", \
	"STARTING", \
	"STATUS", \
	"STRING", \
	"TABLE", \
	"TABLES", \
	"TEMPORARY", \
	"TERMINATED", \
	"TEXT", \
	"THEN", \
	"TIME", \
	"TIMESTAMP", \
	"TINYBLOB", \
	"TINYTEXT", \
	"TINYINT", \
	"TRAILING", \
	"TO", \
	"TYPE", \
	"USE", \
	"USING", \
	"UNIQUE", \
	"UNLOCK", \
	"UNSIGNED", \
	"UPDATE", \
	"USAGE", \
	"VALUES", \
	"VARCHAR", \
	"VARIABLES", \
	"VARYING", \
	"VARBINARY", \
	"WITH", \
	"WRITE", \
	"WHEN", \
	"WHERE", \
	"YEAR", \
	"YEAR_MONTH", \
	"ZEROFILL", \
	NULL }

#define SQLITE3_CUSTOM_FUNCTIONS { \
        "sqlite3_aggregate_context", \
        "sqlite3_aggregate_count", \
        "sqlite3_bind_blob", \
        "sqlite3_bind_double", \
        "sqlite3_bind_int", \
        "sqlite3_bind_int64", \
        "sqlite3_bind_null", \
        "sqlite3_bind_parameter_count", \
        "sqlite3_bind_parameter_index", \
        "sqlite3_bind_parameter_name", \
        "sqlite3_bind_text", \
        "sqlite3_bind_text16", \
        "sqlite3_busy_handler", \
        "sqlite3_busy_timeout", \
        "sqlite3_changes", \
        "sqlite3_close", \
        "sqlite3_collation_needed", \
        "sqlite3_collation_needed16", \
        "sqlite3_column_blob", \
        "sqlite3_column_bytes", \
        "sqlite3_column_bytes16", \
        "sqlite3_column_count", \
        "sqlite3_column_decltype", \
        "sqlite3_column_double", \
        "sqlite3_column_int", \
        "sqlite3_column_int64", \
        "sqlite3_column_name", \
        "sqlite3_column_name16", \
        "sqlite3_column_text", \
        "sqlite3_column_text16", \
        "sqlite3_column_type", \
        "sqlite3_commit_hook", \
        "sqlite3_complete", \
        "sqlite3_complete16", \
        "sqlite3_create_collation", \
        "sqlite3_create_collation16", \
        "sqlite3_create_function", \
        "sqlite3_create_function16", \
        "sqlite3_data_count", \
        "sqlite3_errcode", \
        "sqlite3_errmsg", \
        "sqlite3_errmsg16", \
        "sqlite3_exec", \
        "sqlite3_finalize", \
        "sqlite3_free", \
        "sqlite3_free_table", \
        "sqlite3_get_table", \
        "sqlite3_interrupt", \
        "sqlite3_last_insert_rowid", \
        "sqlite3_libversion", \
        "sqlite3_mprintf", \
        "sqlite3_open", \
        "sqlite3_open16", \
        "sqlite3_prepare", \
        "sqlite3_prepare16", \
        "sqlite3_progress_handler", \
        "sqlite3_reset", \
        "sqlite3_result_blob", \
        "sqlite3_result_double", \
        "sqlite3_result_error", \
        "sqlite3_result_error16", \
        "sqlite3_result_int", \
        "sqlite3_result_int64", \
        "sqlite3_result_null", \
        "sqlite3_result_text", \
        "sqlite3_result_text16", \
        "sqlite3_result_text16be", \
        "sqlite3_result_text16le", \
        "sqlite3_result_value", \
        "sqlite3_set_authorizer", \
        "sqlite3_step", \
        "sqlite3_total_changes", \
        "sqlite3_trace", \
        "sqlite3_user_data", \
        "sqlite3_value_blob", \
        "sqlite3_value_bytes", \
        "sqlite3_value_bytes16", \
        "sqlite3_value_double", \
        "sqlite3_value_int", \
        "sqlite3_value_int64", \
        "sqlite3_value_text", \
        "sqlite3_value_text16", \
        "sqlite3_value_text16be", \
        "sqlite3_value_text16le", \
        "sqlite3_value_type", \
        "sqlite3_vmprintf", \
        NULL}
