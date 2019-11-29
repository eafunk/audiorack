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
 * dbd_sqlite.h: SQLite database support (using libsqlite) header file
 * Copyright (C) 2002-2007, Markus Hoenicka <mhoenicka@users.sourceforge.net>
 * http://libdbi.sourceforge.net
 * 
 * $Id: dbd_sqlite.h,v 1.5 2008/06/11 22:52:19 mhoenicka Exp $
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

#define SQLITE_RESERVED_WORDS { \
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

#define SQLITE_CUSTOM_FUNCTIONS { \
        "sqlite_open", \
        "sqlite_close", \
        "sqlite_exec", \
        "sqlite_last_insert_rowid", \
        "sqlite_changes", \
        "sqlite_get_table", \
        "sqlite_free_table", \
        "sqlite_interrupt", \
        "sqlite_complete", \
        "sqlite_busy_handler", \
        "sqlite_busy_timeout", \
        "sqlite_libversion", \
        "sqlite_encoding", \
        "sqlite_exec_printf", \
        "sqlite_exec_vprintf", \
        "sqlite_get_table_printf", \
        "sqlite_get_table_vprintf", \
        "sqlite_mprintf", \
        "sqlite_vmprintf", \
        "sqlite_freemem", \
        NULL}
