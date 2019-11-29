/*
 * libdbi-drivers - database drivers for libdbi, the database
 * independent abstraction layer for C.

 * Copyright (C) 2001-2008, David Parker, Mark Tobenkin, Markus Hoenicka
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
 * $Id: test_dbi.c,v 1.72 2013/02/24 15:06:57 mhoenicka Exp $
 */

#include <stdio.h>
#include <string.h>
#include <dbi/dbi.h>
#include <dbi/dbi-dev.h> /* need this to access custom functions */
#include <time.h>
#include <unistd.h>
#include <stdlib.h> /* for strtol() */
#include <limits.h>
#include <cgreen/cgreen.h>

#ifdef __MINGW32__
#include <windows.h>
#define sleep(seconds) Sleep((seconds)*1000)
#endif

/* this is defined in configure.in, but unavailable if firebird isn't installed */
#ifndef FIREBIRD_ISQL
#define FIREBIRD_ISQL "cat"
#endif

#define QUERY_LEN 1024

/* this is defined by the Makefile and passed via -D */
/* #define DBDIR /usr/local/var/lib/libdbi */

/* the dbi instance for the recallable interface */
dbi_inst dbi_instance = NULL;
dbi_driver test_driver = NULL;
dbi_conn conn = NULL;

dbi_conn test_conn = NULL;

/* structure definitions */
struct CONNINFO {
   int  n_legacy;
   int  query_log;
   char driverdir[256];
   char drivername[64];
   int  numdrivers;
   char dbname[64];
   char initial_dbname[64];
   char dbdir[256];
   char username[64];
   char password[64];
   char hostname[256];
   char version[64];
   char createschema[QUERY_LEN + 1];
   char createsubschema[5][QUERY_LEN + 1];
   char dropsubschema[5][QUERY_LEN + 1];
   char query[QUERY_LEN + 1];
   char encoding[20];
};

struct TABLEINFO {
  int have_double;
  int have_longlong;
  int have_ulonglong;
  int have_datetime;
  int have_datetime_tz;
  int have_time_tz;
  int number_rows;
};

struct CONNINFO cinfo;
struct TABLEINFO tinfo;

/* switch for recallable (0) vs. legacy (!=0) interface */
int n_legacy = 0;

/* some test data */
char string_to_quote[] = "Can \'we\' \"quote\" this properly?";
char string_to_escape[] = "Can \'we\' \"escape\" this properly?";
char numstring[] = "-54321";
unsigned char binary_to_quote[] = {'A', 'B', '\0', 'C', '\'', 'D'};
unsigned char binary_to_escape[] = {'A', 'B', '\0', 'C', '\'', 'D'};
size_t binary_to_quote_length = 6;
size_t binary_to_escape_length = 6;

const char default_dbdir[] = DBDIR;

struct FIELDINFO {
   char name[32];
   unsigned short type;
   unsigned int attrib;
   unsigned short length;
   unsigned short figures;
   union {
      long long int int_val;
      unsigned long long int uint_val;
      double double_val;
      char string_val[48];
   } expect_val;
   long long expect_as_longlong;
   char expect_as_string[48];
};

/* these structures define the attributes, types, expected return
   values, return values as longlong, and return values as string for
   each supported column type of a particular database engine */
struct FIELDINFO firebird_fieldinfo[] = {
      {"the_char", 1, 4, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE2 */
      {"the_uchar", 1, 4, 0, 0, .expect_val.uint_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE2 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.uint_val = 32767, 32767, "32767"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1,16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1,16, 0, 0, .expect_val.uint_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_float", 2, 2, 0, 0, .expect_val.double_val = 3.4E+37, -9223372036854775808, "3.400000e+37"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 0, .expect_val.double_val = 1.7E+307, -9223372036854775808, "1.700000e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_conn_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 6, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "!", 0, ""}, /* string TODO: should be NULL */
      {"the_binary_quoted_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* binary string */
      {"the_binary_escaped_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* binary string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_TIME|DATE */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"},/* DBI_DATETIME_TIME */
      {"_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can ''we'' \"escape\" this properly?", 0, "Can ''we'' \"escape\" this properly?"}, /* string */
      {"_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "'Can ''we'' \"quote\" this properly?'", 0, "'Can ''we'' \"quote\" this properly?'"}, /* string */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};

struct FIELDINFO freetds_fieldinfo[] = {
      /* name, index, type, attrib */
      {"the_char",  1, 2, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE1 */
      {"the_uchar", 1, 2, 0, 0, .expect_val.uint_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE1 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.uint_val = 32767, 32768, "32767"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1,16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1,16, 0, 0, .expect_val.uint_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1,32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1,32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 0, .expect_val.double_val = 3.402823466E+38, 3.402823466E+38, "3.40280E+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 0, .expect_val.double_val = 1.7E+307, 1.7E+307, "1.797693e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_conn_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 32, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 0, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_quoted_string", 4, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_escaped_string", 4, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_TIME|DATE */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};


struct FIELDINFO ingres_fieldinfo[] = {
      /* name, index, type,, attrib */
      {"the_char", 1, 2, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE1 */
      {"the_uchar", 1, 2, 0, 0, .expect_val.uint_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE1 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.uint_val = 32767, 32768, "32767"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1, 16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1, 16, 0, 0, .expect_val.uint_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1, 32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1, 32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 0, .expect_val.double_val = 3.402823466E+38, 3.402823466E+38, "3.40280E+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 0, .expect_val.double_val = 1.7E+307, 1.7E+307, "1.797693e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_decimal", 3, 0, 0, 0, .expect_val.double_val = 1234.5678, 1234.5678, "1234.5678"}, /* string */
      {"the_money", 3, 0, 0, 0, .expect_val.double_val = 567.89, 567.89, "$567.89"},  /* string */
      {"the_character", 3, 0, 0, 0, .expect_val.string_val = "char column", 0, "char column"}, /* string */
      {"the_byte", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */ /* TODO: insert useful value */
      {"the_conn_quoted_string", 3, 0, 0, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 0, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 0, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 0, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 0, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_quoted_string", 4, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_escaped_string", 4, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_TIME|DATE */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};

struct FIELDINFO msql_fieldinfo[] = {
      /* name, index, type,, attrib */
      {"the_char", 1, 4, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE2 */
      {"the_uchar", 1, 4, 0, 0, .expect_val.int_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE2 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.int_val = 32768, 32768, "32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1, 16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1, 16, 0, 0, .expect_val.int_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1, 32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1, 32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 0, .expect_val.double_val = 3.402823466E+38, 3.402823466E+38, "3.40280E+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_conn_quoted_string", 3, 0, 0, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 0, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 0, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 0, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 0, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"the_time_tz", 5, 2, 0, 0, .expect_val.uint_val = 122399, 122399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};


struct FIELDINFO mysql_fieldinfo[] = {
      /* name, index, type,, attrib */
      {"the_char", 1, 2, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE1 */
      {"the_uchar", 1, 2, 0, 0, .expect_val.int_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE1 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.int_val = 32767, 32767, "32767"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1,16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1,16, 0, 0, .expect_val.int_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1,32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1,32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 5, .expect_val.double_val = 3.402823466E+38, -9223372036854775808, "3.402820e+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 7, .expect_val.double_val = 1.797693E+307, -9223372036854775808, "1.797693e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_conn_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 6, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "!", 0, ""}, /* string */
      {"the_binary_quoted_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_escaped_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_datetime_tz", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"the_time_tz", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can \\'we\\' \\\"escape\\\" this properly?", 0, "Can \\'we\\' \\\"escape\\\" this properly?"}, /* string */
      {"_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "'Can \\'we\\' \\\"quote\\\" this properly?'", 0, "'Can \\'we\\' \\\"quote\\\" this properly?'"}, /* string */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};

struct FIELDINFO pgsql_fieldinfo[] = {
      /* name, index, type,, attrib */
      {"the_char", 1, 4 /* should be 2, but there is no char type */, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE1 */
      {"the_uchar", 1, 4, 0, 0, .expect_val.uint_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE1 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.int_val = 32767, 32767, "32767"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1,16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1,16, 0, 0, .expect_val.int_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1,32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1,32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 5, .expect_val.double_val = 3.402823466E+38, -9223372036854775808, "3.402820e+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 7, .expect_val.double_val =  1.797693e+307, -9223372036854775808, " 1.797693e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_conn_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 6, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "!", 0, ""}, /* string */
      {"the_binary_quoted_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_escaped_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_datetime_tz", 5, 3, 0, 0, .expect_val.uint_val = 1009879199, 1009879199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"the_time_tz", 5, 2, 0, 0, .expect_val.uint_val = 122399, 122399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can ''we'' \"escape\" this properly?", 0, "Can ''we'' \"escape\" this properly?"}, /* string */
      {"_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "'Can ''we'' \"quote\" this properly?'", 0, "'Can ''we'' \"quote\" this properly?'"}, /* string */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};

struct FIELDINFO db2_fieldinfo[] = {
      /* name, index, type,, attrib */
      {"the_char", 1, 2, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE1 */
      {"the_uchar", 1, 2, 0, 0, .expect_val.int_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE1 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.int_val = 32768, 32768, "32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1,16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1,16, 0, 0, .expect_val.int_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1,32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1,32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 7, .expect_val.double_val = 3.402823466E+38, 3.402823466E+38, "3.40280E+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 7, .expect_val.double_val = 1.7E+307, 1.7E+307, "1.797693e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_conn_quoted_string", 3, 0, 0, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 0, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 0, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 0, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 0, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_quoted_string", 4, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_escaped_string", 4, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_datetime_tz", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"the_time_tz", 5, 2, 0, 0, .expect_val.uint_val = 122399, 122399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};

struct FIELDINFO sqlite_fieldinfo[] = {
      /* name, index, type,, attrib */
      {"the_char", 1, 2, 0, 0, .expect_val.int_val = -127, -127, "-127"}, /* DBI_INTEGER_SIZE1 */
      {"the_uchar", 1, 2, 0, 0, .expect_val.int_val = 127, 127, "127"}, /* DBI_INTEGER_SIZE1 */
      {"the_short", 1, 4, 0, 0, .expect_val.int_val = -32768, -32768, "-32768"}, /* DBI_INTEGER_SIZE2 */
      {"the_ushort", 1, 4, 0, 0, .expect_val.int_val = 32767, 32767, "32767"}, /* DBI_INTEGER_SIZE2 */
      {"the_long", 1,16, 0, 0, .expect_val.int_val = -2147483648, -2147483648, "-2147483648"}, /* DBI_INTEGER_SIZE4 */
      {"the_ulong", 1,16, 0, 0, .expect_val.int_val = 2147483647, 2147483647, "2147483647"}, /* DBI_INTEGER_SIZE4 */
      {"the_longlong", 1,32, 0, 0, .expect_val.int_val = -9223372036854775807, -9223372036854775807, "-9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_ulonglong", 1,32, 0, 0, .expect_val.int_val = 9223372036854775807, 9223372036854775807, "9223372036854775807"}, /* DBI_INTEGER_SIZE8 */
      {"the_float", 2, 2, 0, 7, .expect_val.double_val = 3.402823466E+38, -9223372036854775808, "3.402823e+38"}, /* DBI_DECIMAL_SIZE4 */
      {"the_double", 2, 4, 0, 7, .expect_val.double_val = 1.797693e+307, -9223372036854775808, "1.797693e+307"}, /* DBI_DECIMAL_SIZE8 */
      {"the_conn_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_quoted_string_copy", 3, 0, 31, 0, .expect_val.string_val = "Can \'we\' \"quote\" this properly?", 0, "Can \'we\' \"quote\" this properly?"}, /* string */
      {"the_conn_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_conn_escaped_string_copy", 3, 0, 32, 0, .expect_val.string_val = "Can 'we' \"escape\" this properly?", 0, "Can 'we' \"escape\" this properly?"}, /* string */
      {"the_numstring", 3, 0, 6, 0, .expect_val.string_val = "-54321", -54321, "-54321"}, /* string */
      {"the_empty_string", 3, 0, 0, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_null_string", 3, 0, 0, 0, .expect_val.string_val = "!" , 0, ""}, /* string: ! is a null string */
      {"the_binary_quoted_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_binary_escaped_string", 4, 0, 6, 0, .expect_val.string_val = "", 0, ""}, /* string */
      {"the_datetime", 5, 3, 0, 0, .expect_val.uint_val = 1009843199, 1009843199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_datetime_tz", 5, 3, 0, 0, .expect_val.uint_val = 1009879199, 1009879199, "2001-12-31 23:59:59"}, /* DBI_DATETIME_DATE|TIME */
      {"the_date", 5, 1, 0, 0, .expect_val.uint_val = 1009756800, 1009756800, "2001-12-31 00:00:00"}, /* DBI_DATETIME_DATE */
      {"the_time", 5, 2, 0, 0, .expect_val.uint_val = 86399, 86399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"the_time_tz", 5, 2, 0, .expect_val.uint_val = 122399, 122399, "1970-01-01 23:59:59"}, /* DBI_DATETIME_TIME */
      {"_escaped_string", 3, 0, 32, 0, .expect_val.string_val = "Can ''we'' \"escape\" this properly?", 0, "Can ''we'' \"escape\" this properly?"}, /* string */
      {"_quoted_string", 3, 0, 31, 0, .expect_val.string_val = "'Can ''we'' \"quote\" this properly?'", 0, "'Can ''we'' \"quote\" this properly?'"}, /* string */
      {"", 0, 0, 0, 0, .expect_val.int_val = 0, 0, ""}
};


/* prototypes */
void init_tinfo(struct TABLEINFO* ptr_tinfo, struct CONNINFO* ptr_cinfo);

static void create_schema();
static void create_schema_five_rows();
void drop_schema();
int init_schema_tables(struct CONNINFO* prt_cinfo);
int ask_for_conninfo(struct CONNINFO* ptr_cinfo);
int set_driver_options(struct CONNINFO* ptr_cinfo, dbi_conn conn, const char* encoding);
int my_dbi_initialize(const char *driverdir, dbi_inst *Inst);
void my_dbi_shutdown(dbi_inst Inst);
dbi_driver my_dbi_driver_list(dbi_driver Current, dbi_inst Inst);
dbi_conn my_dbi_conn_new(const char *name, dbi_inst Inst);
static void usage();

static void open_database_driver();
static void close_database_driver();
static void open_test_database();
static void close_test_database();
static void create_database();
static void drop_database();

static struct FIELDINFO* get_fieldinfo(const char* drivername);
static int field_index_from_name(const char* fieldname, const char* drivername);
static const char* field_name_from_index(int index, const char* drivername);
static unsigned int field_attrib_from_name(const char* fieldname, const char* drivername);
static unsigned int field_attrib_from_index(int index, const char* drivername);
static unsigned short field_type_from_name(const char* fieldname, const char* drivername);
static unsigned short field_type_from_index(int index, const char* drivername);
static long long expect_longlong_from_name(const char* fieldname, const char* drivername);
static long long expect_longlong_from_index(int index, const char* drivername);
static unsigned long long expect_ulonglong_from_name(const char* fieldname, const char* drivername);
static unsigned long long expect_ulonglong_from_index(int index, const char* drivername);
static double expect_double_from_name(const char* fieldname, const char* drivername);
static unsigned long long expect_double_from_index(int index, const char* drivername);
static const char* expect_string_from_name(const char* fieldname, const char* drivername);
static const char* expect_string_from_index(int index, const char* drivername);
static long long expect_as_longlong_from_name(const char* fieldname, const char* drivername);
static long long expect_as_longlong_from_index(int index, const char* drivername);
static const char* expect_as_string_from_name(const char* fieldname, const char* drivername);
static const char* expect_as_string_from_index(int index, const char* drivername);

static int driver_has_field(const char* fieldname, const char* drivername);
static char* assemble_query_string(const char* drivername, int *numfields);

/* The following macro is an assert to the result.
 * The test case can use *errmsg and errnum
 * to report any problem inside of they. If result is null
 */
#define ASSERT_RESULT                                                                    \
		const char *errmsg;                                                              \
		int errnum;                                                                      \
		if(!result) {                                                                    \
			errnum = dbi_conn_error(conn, &errmsg);                                      \
			assert_not_equal_with_message(result, NULL,                                  \
					"Error '%d': '%s'", errnum, errmsg);                                 \
		}

#define QUERY_ASSERT_RESULT(res, query)                                                  \
		do {                                                                             \
			res = dbi_conn_query(conn, query);                                           \
			if(!res) {                                                                   \
				errnum = dbi_conn_error(conn, &errmsg);                                  \
				assert_not_equal_with_message(res, NULL,                                 \
						"Error '%d': '%s'", errnum, errmsg);                             \
			}                                                                            \
		} while (0);


/* Macro to open the database inside the test case.
 * It creates a test_conn handle
 */
#define OPEN_TEST_DATABASE                                                               \
		do {                                                                             \
			test_conn = my_dbi_conn_new(cinfo.drivername,                                \
					dbi_instance);                                                       \
					if (!test_conn) {                                                    \
						const char *errmsg;                                              \
						int errnum = dbi_conn_error(test_conn, &errmsg);                 \
						fprintf(stderr,"Error %d dbi_conn_new_i: %s\n",                  \
								errnum, errmsg);                                         \
								exit(1);                                                 \
					}                                                                    \
					if (set_driver_options(&cinfo, test_conn, "")) {                     \
						my_dbi_shutdown(dbi_instance);                                   \
						exit(1);                                                         \
					}                                                                    \
					dbi_conn_clear_option(test_conn, "dbname");                          \
					dbi_conn_set_option(test_conn, "dbname",                             \
							cinfo.dbname);                                               \
							if (dbi_conn_connect(test_conn) < 0) {                       \
								fprintf(stderr, "Could not connect to test"              \
										" database\n");                                  \
										exit(1);                                         \
							}                                                            \
		} while(0);                                                                      \

/* Macro to close the test database connection */
#define CLOSE_TEST_DATABASE                                                              \
		dbi_conn_close(test_conn);                                                       \
		test_conn = NULL;

/* setup fixture */
TestSuite *connection_fixture(TestSuite *suite);

/* tests cases */
TestSuite *test_libdbi();
TestSuite *test_database_infrastructure();
TestSuite *test_managing_queries();
TestSuite *test_transactions();
TestSuite *test_dbi_retrieving_fields_data_name();
TestSuite *test_dbi_retrieving_fields_data_idx();
TestSuite *test_dbi_retrieving_fields_meta_data();
TestSuite *test_dbi_retrieving_fields_as();
TestSuite *test_managing_results();
TestSuite *test_dbi_general_test_case();
TestSuite *test_dbi_misc();

int main(int argc, char **argv) {

   CDashInfo pinfo;
   int withcdashreport = 0;
   char *build = NULL;
   char *site_name = NULL;
   char *type = NULL;
   char *os_name = NULL;
   char *os_platform = NULL;
   char *os_release = NULL;
   char *os_version = NULL;
   char *hostname = NULL;

   int ch;
   int runsingletest = 0;
   static char *singletest = "";
   const char *errmsg;

#ifdef __FreeBSD__
   _malloc_options="J"; /* FreeBSDs little malloc debugging helper */
#endif

   while ((ch = getopt(argc, argv, "N:P:R:V:H:CT:B:S:s:h?")) != -1) {
      switch (ch) {
      case 'N':
         os_name = optarg;
         break;
      case 'P':
         os_platform = optarg;
         break;
      case 'R':
         os_release = optarg;
         break;
      case 'V':
         os_version = optarg;
         break;
      case 'H':
         hostname = optarg;
         break;
      case 's':
         runsingletest = 1;
         singletest = optarg;
         break;
      case 'C':
         withcdashreport = 1;
         break;
      case 'S':
         site_name = optarg;
         break;
      case 'B':
         build = optarg;
         break;
      case 'T':
         type = optarg;
         break;
      case 'h': /* fall through */
      case '?': /* fall through */
      default:
         usage();
      }
   }
   argc -= optind;
   argv += optind;

   if (withcdashreport) {
      if (build && type && site_name) {
         pinfo.build = build;
         pinfo.type = type;
         pinfo.name = site_name;
      } else {
         fprintf(stderr, "Please specify a build name (-B) and site name (-S)");
         usage();
      }

      /* inform to cdash about your environment, just a bit */
      if (os_name && os_release && os_platform && os_version && hostname) {
         pinfo.hostname = hostname;
         pinfo.os_name = os_name;
         pinfo.os_platform = os_platform;
         pinfo.os_release = os_release;
         pinfo.os_version = os_version;
      }
   }

   if (ask_for_conninfo(&cinfo)) {
      exit(1);
   }

   init_tinfo(&tinfo, &cinfo);

   if (init_schema_tables(&cinfo)) {
      exit(1);
   }

   fprintf(stderr, "\nConnection information:\n--------------------\n");
   fprintf(stderr,
         "\tLegacy mode:           %d\n"
         "\tLog query:             %d\n"
         "\tDriverdir:             %s\n"
         "\tDrivername:            %s\n"
         "\tDbdir:                 %s\n"
         "\tInitial Database:      %s\n"
         "\tDatabase:              %s\n"
         "\tUsername:              %s\n"
         "\tPassword:              %s\n"
         "\tHostname:              %s\n"
         "\tVersion:               %s\n"
         "\tInitial tables schema: %s\n"
         "\tInitial sub schema:\n"
         "\t  0: %s\n"
         "\t  1: %s\n"
         "\t  2: %s\n"
         "\t  3: %s\n"
         "\t  4: %s\n"
         "\tInitial data schema:   %s\n",
         cinfo.n_legacy,
         cinfo.query_log,
         cinfo.driverdir,
         cinfo.drivername,
         cinfo.dbdir,
         cinfo.initial_dbname,
         cinfo.dbname,
         cinfo.username,
         cinfo.password,
         cinfo.hostname,
         cinfo.version,
         cinfo.createschema,
         cinfo.createsubschema[0],
         cinfo.createsubschema[1],
         cinfo.createsubschema[2],
         cinfo.createsubschema[3],
         cinfo.createsubschema[4],
         cinfo.query);
   fprintf(stderr, "\nBegin tests:\n--------------------\n");

   /* choice the report  */
   if (withcdashreport && runsingletest) {
      return run_single_test(test_libdbi(), singletest, create_cdash_reporter(&pinfo));
   }
   else if (withcdashreport && !runsingletest) {
      return run_test_suite(test_libdbi(), create_cdash_reporter(&pinfo));
   }
   else if (runsingletest) {
      return run_single_test(test_libdbi(), singletest, create_text_reporter());
   }
   else {
      return run_test_suite(test_libdbi(), create_text_reporter());
   }
}

/* helper to obtain a pointer to the appropriate fieldinfo struct */
static struct FIELDINFO* get_fieldinfo(const char* drivername) {
   if (!strcmp(drivername, "firebird")) {
      return firebird_fieldinfo;
   }
   else if (!strcmp(drivername, "freetds")) {
      return freetds_fieldinfo;
   }
   else if (!strcmp(drivername, "ingres")) {
      return ingres_fieldinfo;
   }
   else if (!strcmp(drivername, "msql")) {
      return msql_fieldinfo;
   }
   else if (!strcmp(drivername, "mysql")) {
      return mysql_fieldinfo;
   }
   else if (!strcmp(drivername, "pgsql")) {
      return pgsql_fieldinfo;
   }
   else if (!strcmp(drivername, "db2")) {
      return db2_fieldinfo;
   }
   else if (!strcmp(drivername, "sqlite")
         ||!strcmp(drivername, "sqlite3")) {
      return sqlite_fieldinfo;
   }
   return NULL;
}

/* helper to translate field names into indexes (1-based) for the
 *_idx family of functions. Returns 0 if the field name or the
   driver name does not exist */
static int field_index_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return i+1;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into names for the
   non-*_idx family of functions */
static const char* field_name_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return NULL;
   }

   return ptr_fieldinfo[index-1].name;
}

/* helper to translate field names into attributes for the
 *_get_attrib* family of functions. Returns 0 if the field name does
   not exist */
static unsigned int field_attrib_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return ptr_fieldinfo[i].attrib;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into attributes for the
 *_get_attrib* family of functions. */
static unsigned int field_attrib_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].attrib;
}

/* helper to translate field names into types for the *_get_type* family
   of functions. Returns 0 if the field name does not exist */
static unsigned short field_type_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return ptr_fieldinfo[i].type;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into types for the
 *_get_attrib* family of functions. */
static unsigned short field_type_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].type;
}

/* helper to translate field names into types for the *_get_type* family
   of functions. Returns 0 if the field name does not exist */
static unsigned short field_length_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return ptr_fieldinfo[i].length;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into types for the
 *_get_attrib* family of functions. */
static unsigned short field_length_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].length;
}

/* helper to translate field names into expected long long
   values. Returns 0 if the field name does not exist */
static long long expect_longlong_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return ptr_fieldinfo[i].expect_val.int_val;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into expected long long values */
static long long expect_longlong_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].expect_val.int_val;
}

/* helper to translate field names into expected unsigned long long
   values. Returns 0 if the field name does not exist */
static unsigned long long expect_ulonglong_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return ptr_fieldinfo[i].expect_val.uint_val;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into expected unsigned
   long long values */
static unsigned long long expect_ulonglong_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].expect_val.uint_val;
}

/* helper to translate field names into expected double
   values. Returns 0 if the field name does not exist */
static double expect_double_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         significant_figures_for_assert_double_are(ptr_fieldinfo[i].figures);
         return ptr_fieldinfo[i].expect_val.double_val;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into expected double values */
static unsigned long long expect_double_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   significant_figures_for_assert_double_are(ptr_fieldinfo[index-1].figures);
   return ptr_fieldinfo[index-1].expect_val.double_val;
}

/* helper to translate field names into expected string
   values. Returns 0 if the field name does not exist */
static const char* expect_string_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         if (ptr_fieldinfo[i].expect_val.string_val[0] == '!')
            return NULL;

         return (const char*)ptr_fieldinfo[i].expect_val.string_val;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into expected string values */
static const char* expect_string_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].expect_val.string_val;
}

/* helper to translate field names into expected as_longlong
   values. Returns 0 if the field name does not exist */
static long long expect_as_longlong_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return ptr_fieldinfo[i].expect_as_longlong;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into expected as_longlong values */
static long long expect_as_longlong_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return ptr_fieldinfo[index-1].expect_as_longlong;
}

/* helper to translate field names into expected as_string
   values. Returns 0 if the field name does not exist */
static const char* expect_as_string_from_name(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return (const char*)ptr_fieldinfo[i].expect_as_string;
      }
      i++;
   }

   return 0;
}

/* helper to translate field indexes (1-based) into expected as_string values */
static const char* expect_as_string_from_index(int index, const char* drivername) {
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   return (const char*)ptr_fieldinfo[index-1].expect_as_string;
}

/* helper to assemble a query string from the table index hashes. The
   resulting query string retrieves all fields mentioned in the hash
   in the given order from test_datatypes. The returned string should
   be freed by the calling function */
static char* assemble_query_string(const char* drivername, int *numfields) {
   char* query_string = NULL;
   int i = 0;
   int j = 0;
   size_t query_string_len = 1024;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return NULL;
   }

   *numfields = 0;

   if ((query_string = malloc(query_string_len)) == NULL) {
      return NULL;
   }

   strcpy(query_string, "SELECT ");

   while(*(ptr_fieldinfo[i].name)) {
      if (ptr_fieldinfo[i].name[0] == '_') { /* special field,  don't go into schema */
         i++;
         j++;
         continue;
      }
      strcat(query_string, ptr_fieldinfo[i].name);
      strcat(query_string, ",");
      i++;
      if (i%30 == 0) { /* assume that 30 field names with 32 chars each will fit */
         query_string_len += 1024;
         if ((query_string = realloc(query_string, query_string_len)) == NULL) {
            return NULL;
         }
      }
   }

   query_string[strlen(query_string)-1] = '\0'; /* remove trailing comma */
   strcat(query_string, " from test_datatypes");
   *numfields = i-j; /* do not count special fields */
   return query_string;
}

/* fill tableinfo structure with values based on the hashes */
void init_tinfo(struct TABLEINFO* ptr_tinfo, struct CONNINFO* ptr_cinfo) {
   ptr_tinfo->have_double = driver_has_field("the_double", ptr_cinfo->drivername);
   ptr_tinfo->have_longlong = driver_has_field("the_longlong", ptr_cinfo->drivername);
   ptr_tinfo->have_ulonglong = driver_has_field("the_ulonglong", ptr_cinfo->drivername);
   ptr_tinfo->have_datetime = driver_has_field("the_datetime", ptr_cinfo->drivername);
   ptr_tinfo->have_datetime_tz = driver_has_field("the_datetime_tz", ptr_cinfo->drivername);
   ptr_tinfo->have_time_tz = driver_has_field("the_time_tz", ptr_cinfo->drivername);
   ptr_tinfo->number_rows = 1;
}

static int driver_has_field(const char* fieldname, const char* drivername) {
   int i = 0;
   struct FIELDINFO* ptr_fieldinfo;

   if ((ptr_fieldinfo = get_fieldinfo(drivername)) == NULL) {
      return 0;
   }

   while(*(ptr_fieldinfo[i].name)) {
      if (!strcmp(ptr_fieldinfo[i].name, fieldname)) {
         return 1;
      }
      i++;
   }

   return 0;
}

/*
 * Before begin a test, create a database and some tables to test
 */
static void create_schema_five_rows() {
   tinfo.number_rows = 5;
   create_schema();
}

/*
 * This is the setup function. We can change any option using functions
 * like create_schema_five_rows above
 */
static void create_schema() {
   dbi_result result = NULL;
   const char *errmsg;
   int errnum;
   int i;
   /*  int first_schema = 1;*/

   if (!conn) {
      errnum = dbi_conn_error(conn, &errmsg);
      printf("Error %d, create_schema, conn is null: %s\n", errnum, errmsg);
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }

   /* First we try to create the schema table. If this was bad
    * (i.e: table exists) we try drop table and create again.
    */
   while (!result) {
      if ((result = dbi_conn_query(conn, cinfo.createschema)) == NULL) {
         dbi_conn_error(conn, &errmsg);
         printf("First try, can't create table! %s\n", errmsg);

         if ((result = dbi_conn_query(conn, "DROP TABLE test_datatypes")) == NULL) {
            dbi_conn_error(conn, &errmsg);
            printf("Can't drop table! %s\n", errmsg);
            my_dbi_shutdown(dbi_instance);
            exit(1);
         }

         /*      first_schema = 0;*/

         dbi_result_free(result);

         if ((result = dbi_conn_query(conn, cinfo.createschema)) == NULL) {
            dbi_conn_error(conn, &errmsg);
            printf("Second try, can't create table! %s\n", errmsg);
            my_dbi_shutdown(dbi_instance);
            exit(1);
         }
      }
   }

   dbi_result_free(result);

   /*  if(first_schema) {*/
   for ( i = 0; strlen(cinfo.createsubschema[i]) != 0 ; i++ ) {
      if ((result = dbi_conn_query(conn, cinfo.createsubschema[i])) == NULL) {
         errnum = dbi_conn_error(conn, &errmsg);
         printf("Can't create sub schema data (%d)! %s\n", i, errmsg);
         my_dbi_shutdown(dbi_instance);
         exit(1);
      }
      dbi_result_free(result);
   }
   /*}*/

   for ( i = 1; i <= tinfo.number_rows; i++ ) {
      if ((result = dbi_conn_query(conn, cinfo.query)) == NULL) {
         errnum = dbi_conn_error(conn, &errmsg);
         printf("Can't insert data! %s\n", errmsg);
         my_dbi_shutdown(dbi_instance);
         exit(1);
      }
   }

   dbi_result_free(result);

}

/*
 * Always drop the table (and any other object) after any tests
 */
void drop_schema() {
   dbi_result result;
   const char *errmsg;
   int errnum;
   int i;

   if (!conn) {
      printf("\tError %d, drop_schema, conn is null: %s\n", dbi_conn_error(conn, &errmsg), errmsg);
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }

   if (!strcmp(cinfo.drivername, "firebird")) {
     /* firebird does not support DROP TABLE in regular SQL
	but offers it as an isql extension */
     char command[1024];

     for ( i = 0; strlen(cinfo.dropsubschema[i]) != 0 ; i++ ) {
       if (!*(cinfo.hostname)) {
         snprintf(command, 1024,
		  "echo \"CONNECT \'%s/%s\';%s;\""
		  "| %s -e -pas %s "
		  "-u %s -sql_dialect 3", cinfo.dbdir,
		  cinfo.dbname,
		  cinfo.dropsubschema[i],
		  FIREBIRD_ISQL,
		  cinfo.password, cinfo.username);
       }
       else { /* remote */
         snprintf(command, 1024,
		  "echo \"CONNECT \'%s:%s/%s\';%s;\""
		  "| %s -e -pas %s "
		  "-u %s -sql_dialect 3", cinfo.hostname, cinfo.dbdir,
		  cinfo.dbname,
		  cinfo.dropsubschema[i],
		  FIREBIRD_ISQL,
		  cinfo.password, cinfo.username);
       }
       if (system(command)) {
         fprintf(stderr,"\tAAH! Can't drop subschema %s<< connected to database %s! Error message: %s\n", cinfo.dropsubschema[i], cinfo.dbname, errmsg);
       }
     } /* end for */

     if (!*(cinfo.hostname)) {
       snprintf(command, 1024,
		"echo \"CONNECT \'%s/%s\';DROP TABLE test_datatypes;\""
		"| %s -e -pas %s "
		"-u %s -sql_dialect 3", cinfo.dbdir,
		cinfo.dbname,
		FIREBIRD_ISQL,
		cinfo.password, cinfo.username);
     }
     else { /* remote */
       snprintf(command, 1024,
		"echo \"CONNECT \'%s:%s/%s\';DROP TABLE test_datatypes;\""
		"| %s -e -pas %s "
		"-u %s -sql_dialect 3", cinfo.hostname, cinfo.dbdir,
		cinfo.dbname,
		FIREBIRD_ISQL,
		cinfo.password, cinfo.username);
     }
     if (system(command)) {
       fprintf(stderr,"\tAAH! Can't drop table test_datatypes<< connected to database %s! Error message: %s\n", cinfo.dbname, errmsg);
     }
   }
   else { /* not firebird */
     for ( i = 0; strlen(cinfo.dropsubschema[i]) != 0 ; i++ ) {
       if ((result = dbi_conn_query(conn, cinfo.dropsubschema[i])) == NULL) {
         errnum = dbi_conn_error(conn, &errmsg);
         printf("\tCan't drop sub schema data (%d)! %s\n", i, errmsg);
         my_dbi_shutdown(dbi_instance);
         exit(1);
       }
       dbi_result_free(result);
     }

     if ((result = dbi_conn_query(conn, "DROP TABLE test_datatypes")) == NULL) {
       errnum = dbi_conn_error(conn, &errmsg);
       printf("\tCan't drop table test_datatypes Error '%d' message: %s\n", errnum, errmsg);
     }

     dbi_result_free(result);
   }

}

/*
 * We'll need some tables to test, used by many tests
 */
int init_schema_tables(struct CONNINFO* ptr_cinfo) {

   /* ATTENTION: when changing the table definitions below, please
     update the field name vs. index hashes at the top of this file
     accordingly */

   if (!strcmp(ptr_cinfo->drivername, "firebird")) {
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char SMALLINT,"
            "the_uchar SMALLINT,"
            "the_short SMALLINT, "
            "the_ushort SMALLINT,"
            "the_long INTEGER,"
            "the_ulong INTEGER, "
            "the_float FLOAT,"
            "the_double DOUBLE PRECISION, "
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255), "
            "the_binary_quoted_string BLOB,"
            "the_binary_escaped_string BLOB,"
            "the_datetime TIMESTAMP, "
            "the_date DATE,"
            "the_time TIME,"
            "id INTEGER NOT NULL PRIMARY KEY);");
      snprintf(ptr_cinfo->createsubschema[0], QUERY_LEN, "CREATE GENERATOR gen_t1_id;");
      snprintf(ptr_cinfo->createsubschema[1], QUERY_LEN, "SET GENERATOR gen_t1_id TO 0;");
      snprintf(ptr_cinfo->createsubschema[2], QUERY_LEN,
            "CREATE TRIGGER T1_BI FOR TEST_DATATYPES ACTIVE BEFORE INSERT POSITION 0"
            " AS"
            " BEGIN"
            " if (NEW.ID is NULL) then NEW.ID = GEN_ID(GEN_T1_ID, 1);"
            " END");
      snprintf(ptr_cinfo->dropsubschema[0], QUERY_LEN, "DROP TRIGGER T1_BI");
      snprintf(ptr_cinfo->dropsubschema[1], QUERY_LEN, "DROP GENERATOR gen_t1_id");

      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes ("
            "the_char,"
            "the_uchar,"
            "the_short, "
            "the_ushort,"
            "the_long,"
            "the_ulong,"
            "the_float,"
            "the_double, "
            "the_conn_quoted_string,"
            "the_conn_quoted_string_copy,"
            "the_conn_escaped_string,"
            "the_conn_escaped_string_copy,"
            "the_numstring,"
            "the_empty_string,"
            "the_null_string,"
            "the_binary_quoted_string,  "
            "the_binary_escaped_string,  "
            "the_datetime,"
            "the_date,"
            "the_time"
            ") "
            "VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647, "
            "3.4e+37,"
            "1.7e+307,"
            "'Can ''we'' \"quote\" this properly?',"
            "'Can ''we'' \"quote\" this properly?',"
            "'Can ''we'' \"escape\" this properly?',"
            "'Can ''we'' \"escape\" this properly?',"
            "'%s',"
            "'',"
            "NULL,"
            "'\x01\x40\x41\xff\x42\x26\x43',"
            "'\x01\x40\x41\xff\x42\x26\x43',"
            "'2001-12-31 23:59:59',"
            "'2001-12-31',"
            "'23:59:59'"
            ");", numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "freetds")) {
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char TINYINT,"
            "the_uchar TINYINT,"
            "the_short SMALLINT,"
            "the_ushort SMALLINT,"
            "the_long INT,"
            "the_ulong INT,"
            "the_longlong BIGINT,"
            "the_ulonglong BIGINT,"
            "the_float REAL,"
            "the_double FLOAT,"
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_binary_quoted_string IMAGE,"
            "the_binary_escaped_string IMAGE,"
            "the_datetime DATETIME,"
            "the_date DATETIME,"
            "the_time DATETIME,"
            "id INT IDENTITY,"
            "CONSTRAINT tr_test_datatypes PRIMARY KEY (id))");
      /*
       * For test one byte data type use TINYINT
       * this is unsigned type and by insert replace
       * -127 to binary equivalent 129
       */
      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes ("
            "the_char,"
            "the_uchar,"
            "the_short,"
            "the_ushort,"
            "the_long,"
            "the_ulong,"
            "the_longlong,"
            "the_ulonglong,"
            "the_float,"
            "the_double,"
            "the_conn_quoted_string,"
            "the_conn_quoted_string_copy,"
            "the_conn_escaped_string,"
            "the_conn_escaped_string_copy,"
            "the_numstring,"
            "the_empty_string,"
            "the_null_string,"
            "the_binary_quoted_string,"
            "the_binary_escaped_string,"
            "the_datetime,"
            "the_date,"
            "the_time) VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647,"
            "-2147483648,"
            "2147483647, "
            "3.4e+37,"
            "1.7e+307,"
            "'Can \\'we\\' \"quote\" this properly?',"
            "'Can \\'we\\' \"quote\" this properly?',"
            "'Can \\'we\\' \"escape\" this properly?',"
            "'Can \\'we\\' \"escape\" this properly?',"
            "'%s',"
            "'',"
            "NULL,"
            "'\x01\x40\x41\xff\x42\x26\x43',"
            "'\x01\x40\x41\xff\x42\x26\x43',"
            "'2001-12-31 23:59:59',"
            "'2001-12-31',"
            "'23:59:59',"
            "1)",
            numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "ingres")) {
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char TINYINT,"
            "the_uchar TINYINT,"
            "the_short SMALLINT,"
            "the_ushort SMALLINT,"
            "the_long INT,"
            "the_ulong INT,"
            "the_longlong BIGINT,"
            "the_ulonglong BIGINT,"
            "the_float FLOAT4,"
            "the_double FLOAT,"
            "the_decimal DECIMAL(12,4),"
            "the_money MONEY,"
            "the_character CHAR(50),"
            "the_byte BYTE(50),"
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_binary_quoted_string BLOB,"
            "the_binary_escaped_string BLOB,"
            "the_datetime DATE,"
            "the_date DATE,"
            "the_time DATE,"
            "id INT NOT NULL CONSTRAINT id_key PRIMARY KEY)");

      strcat(ptr_cinfo->createschema, "; CREATE SEQUENCE test_datatypes_id_seq");
      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647, "
            "-9223372036854775807,"
            "9223372036854775807,"
            "3.4e+37,"
            "1.7e+307,"
            "'1234.5678',"
            "'$567.89',"
            "'char column',"
            "X'07ff656667',"
            "my_string_to_quote,"
            "quoted_string,"
            "'my_string_to_escape',"
            "'escaped_string,',"
            "'%s',"
            "'',"
            "NULL,"
            "quoted_binary,"
            "'escaped_binary',"
            "'31-dec-2001 23:59:59',"
            "'31-dec-2001',"
            "'23:59:59',"
            "NEXT VALUE FOR test_datatypes_id_seq)",
            numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "msql")) {
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char INT8,"
            "the_uchar UINT8,"
            "the_short INT16,"
            "the_ushort UINT16,"
            "the_long INT,"
            "the_ulong UINT,"
            "the_longlong INT64,"
            "the_ulonglong UINT64,"
            "the_float REAL,"
            "the_conn_quoted_string CHAR(255),"
            "the_conn_quoted_string_copy CHAR(255),"
            "the_conn_escaped_string CHAR(255),"
            "the_conn_escaped_string_copy CHAR(255),"
            "the_numstring CHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_date DATE,"
            "the_time TIME,"
            "the_time_tz TIME,"
            "id INT)");

      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes VALUES ("
            "-127,"
            "127,"
            "-32767,"
            "32767,"
            "-2147483647,"
            "2147483647,"
            "-9223372036854775807,"
            "9223372036854775807,"
            "3.402823466E+38,"
            "my_string_to_quote,"
            "quoted_string,"
            "'my_string_to_escape',"
            "'escaped_string',"
            "'%s',"
            "'',"
            "NULL,"
            "'11-jul-1977',"
            "'23:59:59',"
            "NULL)",
            numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "mysql")) {
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char TINYINT,"
            "the_uchar TINYINT,"
            "the_short SMALLINT,"
            "the_ushort SMALLINT,"
            "the_long INT,"
            "the_ulong INT,"
            "the_longlong BIGINT,"
            "the_ulonglong BIGINT,"
            "the_float FLOAT4,"
            "the_double FLOAT8,"
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_binary_quoted_string BLOB,"
            "the_binary_escaped_string BLOB,"
            "the_datetime DATETIME,"
            "the_datetime_tz DATETIME,"
            "the_date DATE,"
            "the_time TIME,"
            "the_time_tz TIME,"
            "id INT AUTO_INCREMENT,"
            "PRIMARY KEY (id)) "
	       "ENGINE=InnoDB");

      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes ("
            "the_char,"
            "the_uchar,"
            "the_short,"
            "the_ushort,"
            "the_long,"
            "the_ulong,"
            "the_longlong,"
            "the_ulonglong,"
            "the_float,"
            "the_double,"
            "the_conn_quoted_string,"
            "the_conn_quoted_string_copy,"
            "the_conn_escaped_string,"
            "the_conn_escaped_string_copy,"
            "the_numstring,"
            "the_empty_string,"
            "the_null_string,"
            "the_binary_quoted_string,"
            "the_binary_escaped_string,"
            "the_datetime,"
            "the_datetime_tz,"
            "the_date,"
            "the_time,"
            "the_time_tz) VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647,"
            "-9223372036854775807,"
            "9223372036854775807,"
            "3.402823466E+38,"
            "1.7976931348623157E+307,"
            "'Can ''we'' \"quote\" this properly?',"
            "'Can ''we'' \"quote\" this properly?',"
            "'Can ''we'' \"escape\" this properly?',"
            "'Can ''we'' \"escape\" this properly?',"
            "'%s',"
            "'',"
            "NULL,"
            "'AB\\0C\\\'D',"
            "'AB\\0C\\\'D',"
            "'2001-12-31 23:59:59',"
            "'2001-12-31 23:59:59 -10:00',"
            "'2001-12-31',"
            "'23:59:59',"
            "'23:59:59-10:00')",
            numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "pgsql")) {
      /* PostgreSQL does not have a 1-byte integer, use smallint
       instead. This will raise a warning when retrieving the value */
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char SMALLINT,"
            "the_uchar SMALLINT,"
            "the_short SMALLINT,"
            "the_ushort SMALLINT,"
            "the_long INT,"
            "the_ulong INT,"
            "the_longlong BIGINT,"
            "the_ulonglong BIGINT,"
            "the_float FLOAT4,"
            "the_double FLOAT8,"
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_binary_quoted_string BYTEA,"
            "the_binary_escaped_string BYTEA,"
            "the_datetime TIMESTAMP,"
            "the_datetime_tz TIMESTAMP WITH TIME ZONE,"
            "the_date DATE,"
            "the_time TIME,"
            "the_time_tz TIME WITH TIME ZONE,"
            "id SERIAL PRIMARY KEY)");

      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes ("
            "the_char,"
            "the_uchar,"
            "the_short,"
            "the_ushort,"
            "the_long,"
            "the_ulong,"
            "the_longlong,"
            "the_ulonglong,"
            "the_float,"
            "the_double,"
            "the_conn_quoted_string,"
            "the_conn_quoted_string_copy,"
            "the_conn_escaped_string,"
            "the_conn_escaped_string_copy,"
            "the_numstring,"
            "the_empty_string,"
            "the_null_string,"
            "the_binary_quoted_string,"
            "the_binary_escaped_string,"
            "the_datetime,"
            "the_datetime_tz,"
            "the_date,"
            "the_time,"
            "the_time_tz) VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647,"
            "-9223372036854775807,"
            "9223372036854775807,"
            "3.402823466E+38,"
            "1.7976931348623157E+307,"
            "'Can '\'we\'' \"quote\" this properly?',"
            "'Can '\'we\'' \"quote\" this properly?',"
            "'Can '\'we\'' \"escape\" this properly?',"
            "'Can '\'we\'' \"escape\" this properly?',"
            "'%s',"
            "'',"
            "NULL,"
            "'AB\\\\000C''D',"
            "'AB\\\\000C''D',"
            "'2001-12-31 23:59:59',"
            "'2001-12-31 23:59:59 -10:00',"
            "'2001-12-31',"
            "'23:59:59',"
            "'23:59:59-10:00')",
            numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "db2")) {
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char SMALLINT,"
            "the_uchar SMALLINT,"
            "the_short SMALLINT,"
            "the_ushort SMALLINT,"
            "the_long INT,"
            "the_ulong INT,"
            "the_longlong BIGINT,"
            "the_ulonglong BIGINT,"
            "the_float FLOAT,"
            "the_double DOUBLE,"
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_binary_quoted_string CLOB,"
            "the_binary_escaped_string CLOB,"
            "the_datetime TIMESTAMP,"
            "the_datetime_tz TIMESTAMP,"
            "the_date DATE,"
            "the_time TIME,"
            "the_time_tz TIME,"
            "id INT NOT NULL GENERATED ALWAYS AS IDENTITY"
            " (START WITH 1, INCREMENT BY 1, NO CACHE))");

      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes ("
            "the_char,"
            "the_uchar,"
            "the_short,"
            "the_ushort,"
            "the_long,"
            "the_ulong,"
            "the_longlong,"
            "the_ulonglong,"
            "the_float,"
            "the_double,"
            "the_conn_quoted_string,"
            "the_conn_quoted_string_copy,"
            "the_conn_escaped_string,"
            "the_conn_escaped_string_copy,"
            "the_numstring,"
            "the_empty_string,"
            "the_null_string,"
            "the_binary_quoted_string,"
            "the_binary_escaped_string,"
            "the_datetime,"
            "the_datetime_tz,"
            "the_date,"
            "the_time,"
            "the_time_tz) VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647,"
            "-9223372036854775807,"
            "9223372036854775807,"
            "3.402823466E+38,"
            "1.7976931348623157E+307,"
            "'Can '\'we\'' \"quote\" this properly?',"
            "'Can '\'we\'' \"quote\" this properly?',"
            "'Can '\'we\'' \"escape\" this properly?',"
            "'Can '\'we\'' \"escape\" this properly?',"
            "'%s',"
            "'',"
            "NULL,"
            "'AB\\\\000C''''D',"
            "'AB\\\\000C''''D',"
            "'2001-12-31-23.59.59',"
            "'2001-12-31-23.59.59',"
            "'2001-12-31',"
            "'23:59:59',"
            "'23:59:59')",
            numstring);
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite")
         || !strcmp(ptr_cinfo->drivername, "sqlite3")){
      snprintf(ptr_cinfo->createschema, QUERY_LEN, "CREATE TABLE test_datatypes ( "
            "the_char CHAR,"
            "the_uchar CHAR,"
            "the_short SMALLINT,"
            "the_ushort SMALLINT,"
            "the_long INT,"
            "the_ulong INT,"
            "the_longlong BIGINT,"
            "the_ulonglong BIGINT,"
            "the_float FLOAT4,"
            "the_double FLOAT8,"
            "the_driver_string VARCHAR(255),"
            "the_conn_quoted_string VARCHAR(255),"
            "the_conn_quoted_string_copy VARCHAR(255),"
            "the_conn_escaped_string VARCHAR(255),"
            "the_conn_escaped_string_copy VARCHAR(255),"
            "the_numstring VARCHAR(255),"
            "the_empty_string VARCHAR(255),"
            "the_null_string VARCHAR(255),"
            "the_binary_quoted_string BLOB,"
            "the_binary_escaped_string BLOB,"
            "the_datetime DATETIME,"
            "the_datetime_tz DATETIME,"
            "the_date DATE,"
            "the_time TIME,"
            "the_time_tz TIME,"
            "id INTEGER AUTO INCREMENT)");

      snprintf(ptr_cinfo->query, QUERY_LEN, "INSERT INTO test_datatypes ("
            "the_char,"
            "the_uchar,"
            "the_short,"
            "the_ushort,"
            "the_long,"
            "the_ulong,"
            "the_longlong,"
            "the_ulonglong,"
            "the_float,"
            "the_double,"
            "the_conn_quoted_string,"
            "the_conn_quoted_string_copy,"
            "the_conn_escaped_string,"
            "the_conn_escaped_string_copy,"
            "the_numstring,"
            "the_empty_string,"
            "the_null_string,"
            "the_binary_quoted_string,"
            "the_binary_escaped_string,"
            "the_datetime,"
            "the_datetime_tz,"
            "the_date,"
            "the_time,"
            "the_time_tz) VALUES ("
            "-127,"
            "127,"
            "-32768,"
            "32767,"
            "-2147483648,"
            "2147483647,"
            "-9223372036854775807,"
            "9223372036854775807,"
            "3.402823466E+38,"
            "1.7976931348623157E+307,"
            "'Can ''we'' \"quote\" this properly?',"
            "'Can ''we'' \"quote\" this properly?',"
            "'Can ''we'' \"escape\" this properly?',"
            "'Can ''we'' \"escape\" this properly?',"
            "'%s',"
            "'',"
            "NULL,"
            "'\x01\x40\x41\xff\x42\x26\x43',"
            "'\x01\x40\x41\xff\x42\x26\x43',"
            "'2001-12-31 23:59:59',"
            "'2001-12-31 23:59:59 -10:00',"
            "'2001-12-31',"
            "'23:59:59',"
            "'23:59:59-10:00')",
            numstring);
   }
   return 0;
}

/* returns 0 on success, 1 on error */
int ask_for_conninfo(struct CONNINFO* ptr_cinfo) {
   int numdrivers;
   char resp[16];

   fprintf(stderr, "\nlibdbi-drivers test program: $Id: test_dbi.c,v 1.72 2013/02/24 15:06:57 mhoenicka Exp $\n\n");

   fprintf(stderr, "test instance-based (i) or legacy (l) libdbi interface? [i] ");
   fgets(resp, 16, stdin);
   if (*resp == '\n' || *resp == 'i') {
      n_legacy = 0;
      ptr_cinfo->n_legacy = 0;
   }
   else {
      n_legacy = 1;
      ptr_cinfo->n_legacy = 1;
   }

   fprintf(stderr, "log query (y|n)? [n] ");
   fgets(resp, 16, stdin);
   if (*resp == '\n' || *resp == 'n') {
      ptr_cinfo->query_log = 0;
   }
   else {
      ptr_cinfo->query_log = 1;
   }

   fprintf(stderr, "libdbi driver directory? [%s] ", DBI_DRIVER_DIR);
   fgets(ptr_cinfo->driverdir, 256, stdin);
   if ((ptr_cinfo->driverdir)[0] == '\n') {
      strncpy(ptr_cinfo->driverdir, DBI_DRIVER_DIR, 255), (ptr_cinfo->driverdir)[255] = '\0';
   }
   else {
      (ptr_cinfo->driverdir)[strlen(ptr_cinfo->driverdir)-1] = '\0';
   }

   numdrivers = my_dbi_initialize(ptr_cinfo->driverdir, &dbi_instance);

   if (numdrivers < 0) {
      fprintf(stderr, "Unable to initialize libdbi! Make sure you specified a valid driver directory.\n");
      my_dbi_shutdown(dbi_instance);
      return 1;
   }
   else if (numdrivers == 0) {
      fprintf(stderr, "Initialized libdbi, but no drivers were found!\n");
      my_dbi_shutdown(dbi_instance);
      return 1;
   }

   test_driver = NULL;
   fprintf(stderr, "%d drivers available: ", numdrivers);
   while ((test_driver = my_dbi_driver_list(test_driver, dbi_instance)) != NULL) {
      fprintf(stderr, "%s ", dbi_driver_get_name(test_driver));
   }
   test_driver = NULL;

   (ptr_cinfo->drivername)[0] = '\n';

   while ((ptr_cinfo->drivername)[0] == '\n') {
      fprintf(stderr, "\ntest which driver? ");
      fgets(ptr_cinfo->drivername, 64, stdin);
   }
   (ptr_cinfo->drivername)[strlen(ptr_cinfo->drivername)-1] = '\0';

   if (!strcmp(ptr_cinfo->drivername, "mysql")
         || !strcmp(ptr_cinfo->drivername, "pgsql")
         || !strcmp(ptr_cinfo->drivername, "firebird")
         || !strcmp(ptr_cinfo->drivername, "freetds")
         || !strcmp(ptr_cinfo->drivername, "db2")) {
      fprintf(stderr, "\ndatabase administrator name? ");
      fgets(ptr_cinfo->username, 64, stdin);
      if (*(ptr_cinfo->username) == '\n') {
         *(ptr_cinfo->username) = '\0';
      }
      else {
         (ptr_cinfo->username)[strlen(ptr_cinfo->username)-1] = '\0';
      }

      fprintf(stderr, "\ndatabase administrator password? ");
      fgets(ptr_cinfo->password, 64, stdin);
      if (*(ptr_cinfo->password) == '\n') {
         *(ptr_cinfo->password) = '\0';
      }
      else {
         (ptr_cinfo->password)[strlen(ptr_cinfo->password)-1] = '\0';
      }
   }

   if(!strcmp(ptr_cinfo->drivername, "sqlite")
         || !strcmp(ptr_cinfo->drivername, "sqlite3")
         || !strcmp(ptr_cinfo->drivername, "firebird")) {
     if (!strcmp(ptr_cinfo->drivername, "firebird")) {
       /* we use the isql client to create and drop databases. This
	  tool is whacky in that it accepts only darn short arguments,
	  including the path of the database. Therefore do not suggest
	  the libdbi default, but /tmp */
       fprintf(stderr, "database directory? [/tmp] ");
     }
     else {
       fprintf(stderr, "database directory? [DEFAULT] ");
     }
     fgets(ptr_cinfo->dbdir, 256, stdin);
     if ((ptr_cinfo->dbdir)[0] == '\n') {
       (ptr_cinfo->dbdir)[0] = '\0';
     }
     else {
       (ptr_cinfo->dbdir)[strlen(ptr_cinfo->dbdir)-1] = '\0';
     }
     if (!strcmp(ptr_cinfo->drivername, "firebird")
	 && (ptr_cinfo->dbdir)[0] == '\0') {
       /* strcpy(ptr_cinfo->dbdir, default_dbdir); */
       /* strcat(ptr_cinfo->dbdir, "/firebird"); */
       strcpy(ptr_cinfo->dbdir, "/tmp");
     }
   }
   /*   else: other drivers do not use a database directory */

   if (!strcmp(ptr_cinfo->drivername, "firebird")
         || !strcmp(ptr_cinfo->drivername, "mysql")
         || !strcmp(ptr_cinfo->drivername, "pgsql")
         || !strcmp(ptr_cinfo->drivername, "freetds")
         || !strcmp(ptr_cinfo->drivername, "db2")) {
      fprintf(stderr, "\ndatabase hostname? [(blank for local socket if possible)] ");
      fgets(ptr_cinfo->hostname, 256, stdin);
      if (*(ptr_cinfo->hostname) == '\n') {
         if (!strcmp(ptr_cinfo->drivername, "pgsql")
               || !strcmp(ptr_cinfo->drivername, "firebird")
               || !strcmp(ptr_cinfo->drivername, "msql")) {
            *(ptr_cinfo->hostname) = '\0';
         }
         else {
            strcpy(ptr_cinfo->hostname, "localhost");
         }
      }
      else {
         (ptr_cinfo->hostname)[strlen(ptr_cinfo->hostname)-1] = '\0';
         if (!strcmp(ptr_cinfo->drivername, "pgsql")) {
            if (!strcmp(ptr_cinfo->hostname, "localhost")) {
               *(ptr_cinfo->hostname) = '\0';
            }
         }
      }
   }

   if (!strcmp(ptr_cinfo->drivername, "freetds")) {
      fprintf(stderr, "database version? ");
      fgets(ptr_cinfo->version, 64, stdin);
      (ptr_cinfo->version)[strlen(ptr_cinfo->version)-1] = '\0';
   }

   fprintf(stderr, "database name? [libdbitest] ");
   fgets(ptr_cinfo->dbname, 64, stdin);
   if ((ptr_cinfo->dbname)[0] == '\n') {
      strcpy(ptr_cinfo->dbname, "libdbitest");
   }
   else {
      (ptr_cinfo->dbname)[strlen(ptr_cinfo->dbname)-1] = '\0';
   }

   fprintf(stderr, "encoding? [] ");
   fgets(ptr_cinfo->encoding, 20, stdin);
   if ((ptr_cinfo->encoding)[0] == '\n') {
      *(ptr_cinfo->encoding) = '\0';
   }
   else {
      (ptr_cinfo->encoding)[strlen(ptr_cinfo->encoding)-1] = '\0';
   }

   if (!strcmp(ptr_cinfo->drivername, "mysql")) {
      strcpy(ptr_cinfo->initial_dbname, "mysql");
   }
   else if (!strcmp(ptr_cinfo->drivername, "pgsql")) {
      strcpy(ptr_cinfo->initial_dbname, "template1");
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite")
         || !strcmp(ptr_cinfo->drivername, "sqlite3")) {
      strcpy(ptr_cinfo->initial_dbname, ptr_cinfo->dbname);
   }
   else if (!strcmp(ptr_cinfo->drivername, "ingres")
         || !strcmp(ptr_cinfo->drivername, "firebird")){
      strcpy(ptr_cinfo->initial_dbname, ptr_cinfo->dbname);
   }
   else if (!strcmp(ptr_cinfo->drivername, "freetds")) {
      strcpy(ptr_cinfo->initial_dbname, "master");
   }
   else if (!strcmp(ptr_cinfo->drivername, "db2")) {
      strcpy(ptr_cinfo->initial_dbname, "toolsdb");
   }

   my_dbi_shutdown(dbi_instance);
   dbi_instance = NULL;
   return 0;
}

/* always returns 0 */
int set_driver_options(struct CONNINFO* ptr_cinfo, dbi_conn conn, const char* encoding) {
   char *versionstring[100];

   if (!strcmp(ptr_cinfo->drivername, "mysql")
         || !strcmp(ptr_cinfo->drivername, "pgsql")
         || !strcmp(ptr_cinfo->drivername, "freetds")
         || !strcmp(ptr_cinfo->drivername, "db2")
   ) {
      dbi_conn_set_option(conn, "host", ptr_cinfo->hostname);
      dbi_conn_set_option(conn, "username", ptr_cinfo->username);
      dbi_conn_set_option(conn, "password", ptr_cinfo->password);

      if (!strcmp(ptr_cinfo->drivername, "freetds") && strlen(ptr_cinfo->version)){
         dbi_conn_set_option(conn, "freetds_version", ptr_cinfo->version);
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "msql")) {
      if( *(ptr_cinfo->hostname)) {
         dbi_conn_set_option(conn, "host", ptr_cinfo->hostname);
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite3")) {
      if (*(ptr_cinfo->dbdir)) {
         dbi_conn_set_option(conn, "sqlite3_dbdir", ptr_cinfo->dbdir);
      }
   }
   else  if (!strcmp(ptr_cinfo->drivername, "sqlite")){
      if (*(ptr_cinfo->dbdir)) {
         dbi_conn_set_option(conn, "sqlite_dbdir", ptr_cinfo->dbdir);
      }
   }
   else  if (!strcmp(ptr_cinfo->drivername, "firebird")){
      if (*(ptr_cinfo->dbdir)) {
         dbi_conn_set_option(conn, "firebird_dbdir", ptr_cinfo->dbdir);
      }
      dbi_conn_set_option(conn, "host", ptr_cinfo->hostname);
      dbi_conn_set_option(conn, "username", ptr_cinfo->username);
      dbi_conn_set_option(conn, "password", ptr_cinfo->password);
   }

   if (!strcmp(ptr_cinfo->drivername, "mysql")) {
      strcpy(ptr_cinfo->initial_dbname, "mysql");

      if (encoding && *encoding) {
         dbi_conn_set_option(conn, "encoding", encoding);
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "pgsql")) {
      strcpy(ptr_cinfo->initial_dbname, "template1");
      if (encoding && *encoding) {
         dbi_conn_set_option(conn, "encoding", encoding);
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite")
         || !strcmp(ptr_cinfo->drivername, "sqlite3")) {
      strcpy(ptr_cinfo->initial_dbname, ptr_cinfo->dbname);
      if (encoding && *encoding) {
         dbi_conn_set_option(conn, "encoding", encoding);
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "ingres")
         || !strcmp(ptr_cinfo->drivername, "firebird")){
      strcpy(ptr_cinfo->initial_dbname, ptr_cinfo->dbname);
   }
   else if (!strcmp(ptr_cinfo->drivername, "freetds")) {
      strcpy(ptr_cinfo->initial_dbname, "master");
      if (encoding && *encoding) {
         dbi_conn_set_option(conn, "encoding", encoding);
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "db2")) {
      strcpy(ptr_cinfo->initial_dbname, "toolsdb");
      if (encoding && *encoding) {
         dbi_conn_set_option(conn, "encoding", encoding);
      }
   }
   dbi_conn_set_option(conn, "dbname",  ptr_cinfo->dbname);

   dbi_conn_set_option_numeric(conn, "LogQueries", ptr_cinfo->query_log);

   return 0;
}

/* Open the driver, set options and connect */
static void open_database_driver() {
   const char *errmsg;
   int errnum;

   if (!dbi_instance)
      cinfo.numdrivers = my_dbi_initialize(cinfo.driverdir, &dbi_instance);

   if (cinfo.numdrivers < 0) {
      fprintf(stderr, "Unable to initialize libdbi! Make sure you specified a valid driver directory.\n");
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }
   else if (cinfo.numdrivers == 0) {
      fprintf(stderr, "Initialized libdbi, but no drivers were found!\n");
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }

   conn = my_dbi_conn_new(cinfo.drivername, dbi_instance);

   if (!conn) {
      errnum = dbi_conn_error(conn, &errmsg);
      fprintf(stderr,"Error %d dbi_conn_new_i: %s\n", errnum, errmsg);
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }

   if (set_driver_options(&cinfo, conn, "")) {
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }

}

static void create_database() {

   const char *errmsg;
   dbi_result result;
   char my_enc[32];
   char database_path[1024];

   char command[1024];

   if (!strcmp(cinfo.drivername, "firebird")) {
      snprintf(database_path, 1024, "%s/%s", cinfo.dbdir, cinfo.dbname);

      if (!*(cinfo.hostname)) {
         if (access(database_path, R_OK | W_OK | F_OK) == 0) {
            goto noop;
         }

         snprintf(command, 1024,
               "echo \"CREATE DATABASE \'localhost:%s/%s\';\""
               "| %s -e -pas %s "
               "-u %s -sql_dialect 3", cinfo.dbdir,
               cinfo.dbname,
               FIREBIRD_ISQL,
               cinfo.password, cinfo.username);
      }
      else { /* remote */
         snprintf(command, 1024,
               "echo \"CREATE DATABASE \'%s:%s/%s\';\""
               "| %s -e -pas %s "
               "-u %s -sql_dialect 3", cinfo.hostname, cinfo.dbdir,
               cinfo.dbname,
               FIREBIRD_ISQL,
               cinfo.password, cinfo.username);
      }

      if (system(command)) {
         fprintf(stderr, "Could not create initial database\n");
         goto error;
      }
      /*     snprintf(command, 1024, "sudo chmod 666 %s/%s", ptr_cinfo->dbdir, ptr_cinfo->dbname); */
      /*     if (system(command)) { */
      /*       fprintf(stderr, "Could not set database permissions\n"); */
      /*       return 1; */
      /*     } */
      goto noop;
   }
   else if (!strcmp(cinfo.drivername, "sqlite")
         || !strcmp(cinfo.drivername, "sqlite3")
         || !strcmp(cinfo.drivername, "msql")
         || !strcmp(cinfo.drivername, "ingres")
         || !strcmp(cinfo.drivername, "db2")
         || !strcmp(cinfo.drivername, "oracle")) {
      goto noop;
   }
   else {
      /* we need to connect to a libdbi instance */
      open_database_driver();

      /* change to initial database */
      dbi_conn_clear_option(conn, "dbname");
      dbi_conn_set_option(conn, "dbname",  cinfo.initial_dbname);

      if (dbi_conn_connect(conn) < 0) {
         printf("Could not connect to create the test database\n");
         my_dbi_shutdown(dbi_instance);
         exit(1);
      }

      /* now create a database */
      if (cinfo.encoding && *(cinfo.encoding)) {
	if (!strcmp(cinfo.drivername, "mysql")) {
	  result = dbi_conn_queryf(conn, "CREATE DATABASE %s CHARACTER SET %s", cinfo.dbname, (!strcmp(cinfo.encoding, "UTF-8")) ? "utf8":"latin1");
	}
	else if (!strcmp(cinfo.drivername, "pgsql")) {
	  /* the first SQL command used to work until PostgreSQL
	     8.3. Later versions need a matching locale setting along
	     with the encoding. Most modern installations use UNICODE
	     as default encoding, therefore creating such a database
	     should be safe. However, creating a LATIN1 database needs
	     extra care. Apparently PostgreSQL is not supposed to work
	     with databases whose encodings differ from that of the
	     cluster, as set with initdb */
	  unsigned int pgserver_version;

	  pgserver_version = dbi_conn_get_engine_version(conn);

	  if (pgserver_version < 80400) {
	    result = dbi_conn_queryf(conn, "CREATE DATABASE %s WITH ENCODING = '%s'", cinfo.dbname, (!strcmp(cinfo.encoding, "UTF-8")) ? "UNICODE":"LATIN1");
	  }
	  else {
	    if (!strcmp(cinfo.encoding, "UTF-8")) {
	      result = dbi_conn_queryf(conn, "CREATE DATABASE %s WITH ENCODING = '%s' LC_COLLATE='en_US.UTF8' LC_CTYPE='en_US.UTF8'", cinfo.dbname, "UTF8");
	    }
	    else {
	      result = dbi_conn_queryf(conn, "CREATE DATABASE %s WITH ENCODING = '%s'  LC_COLLATE='de_DE.ISO8859-1' LC_CTYPE='de_DE.ISO8859-1' TEMPLATE template0", cinfo.dbname, "LATIN1");
	    }
	  }
	}
      }
      else {
         if (!strcmp(cinfo.drivername, "mysql")) {
            result = dbi_conn_queryf(conn, "CREATE DATABASE %s", cinfo.dbname);
         }
         else if (!strcmp(cinfo.drivername, "pgsql")) {
            result = dbi_conn_queryf(conn, "CREATE DATABASE %s", cinfo.dbname);
         }
      }

      if (result == NULL) {
         dbi_conn_error(conn, &errmsg);
         fprintf(stderr, "\tDarn! Can't create database! Error message: %s\n", errmsg);
         goto error;
      }
      dbi_result_free(result);
   }

   if (conn) {
      dbi_conn_close(conn);
      conn = NULL;
   }

   noop:
   return;

   error:
   exit(1);

}

static void drop_database() {
   const char *errmsg;
   dbi_result result;

   if (!strcmp(cinfo.drivername, "sqlite")
         || !strcmp(cinfo.drivername, "sqlite3")) {
      char dbpath[_POSIX_PATH_MAX];

      /* need this break to grant some time for db unlocking */
      sleep(3);

      if (*(cinfo.dbdir)) {
         strcpy(dbpath, cinfo.dbdir);
      }
      else {
         if (!strcmp(cinfo.drivername, "sqlite")) {
            snprintf(dbpath, _POSIX_PATH_MAX, "%s/sqlite", default_dbdir);
         }
         else {
            snprintf(dbpath, _POSIX_PATH_MAX, "%s/sqlite3", default_dbdir);
         }
      }
      if (dbpath[strlen(dbpath)-1] != '/') {
         strcat(dbpath, "/");
      }
      strcat(dbpath, cinfo.dbname);

      if (unlink(dbpath)) {
         fprintf(stderr, "AAH! Can't delete database file!\n");
         goto error;
      }
   }
   else if (!strcmp(cinfo.drivername, "msql")
         || !strcmp(cinfo.drivername, "ingres")) {
      fprintf(stderr, "\tThis is a no-op with the mSQL/Ingres driver.\n");
   }
   else if (!strcmp(cinfo.drivername, "firebird")) {
      /* firebird does not support DROP DATABASE in regular SQL
	       but offers it as an isql extension. In order to get rid
	       of the test database, connect to it and drop it by
	       sending a string to isql */
      char command[1024];

      if (!*(cinfo.hostname)) {
         snprintf(command, 1024,
               "echo \"CONNECT \'%s/%s\';DROP DATABASE;\""
               "| %s -e -pas %s "
               "-u %s -sql_dialect 3", cinfo.dbdir,
               cinfo.dbname,
               FIREBIRD_ISQL,
               cinfo.password, cinfo.username);
      }
      else { /* remote */
         snprintf(command, 1024,
               "echo \"CONNECT \'%s:%s/%s\';DROP DATABASE;\""
               "| %s -e -pas %s "
               "-u %s -sql_dialect 3", cinfo.hostname, cinfo.dbdir,
               cinfo.dbname,
               FIREBIRD_ISQL,
               cinfo.password, cinfo.username);
      }
      if (system(command)) {
         fprintf(stderr,"\tAAH! Can't drop database %s<< connected to database %s! Error message: %s\n", cinfo.dbname, cinfo.dbname, errmsg);
         goto error;
      }
   }

   else {

      open_database_driver();

      /* change to initial database */
      dbi_conn_clear_option(conn, "dbname");
      dbi_conn_set_option(conn, "dbname",  cinfo.initial_dbname);

      if (dbi_conn_connect(conn) < 0) {
         fprintf(stderr,"Could not connect to drop the test database\n");
         my_dbi_shutdown(dbi_instance);
         goto error;
      }

      if ((result = dbi_conn_queryf(conn, "DROP DATABASE %s",
            cinfo.dbname)) == NULL) {
         dbi_conn_error(conn, &errmsg);
         fprintf(stderr,"\tAAH! Can't drop database %s<< connected to database %s! Error message: %s\n", cinfo.dbname, cinfo.initial_dbname, errmsg);
         goto error;
      }

      dbi_result_free(result);
   }

   /* don't forget to disconnect */
   dbi_conn_close(conn);
   conn = NULL;

   /* important: this functions is the last to be called
    * we need to shutdown libdbi
    */
   my_dbi_shutdown(dbi_instance);

   error:
   return;

}

static void open_test_database() {
   const char *errmsg;
   int errnum;

   open_database_driver();

   /* now we can change to test database and connect */
   dbi_conn_clear_option(conn, "dbname");
   dbi_conn_set_option(conn, "dbname",  cinfo.dbname);

   if (dbi_conn_connect(conn) < 0) {
      printf("Could not connect to test database\n");
      my_dbi_shutdown(dbi_instance);
      exit(1);
   }

}

static void close_database_driver() {
  if (conn) {
    dbi_conn_close(conn);
    conn = NULL;
  }
  my_dbi_shutdown(dbi_instance);
  dbi_instance = NULL;
  test_driver = NULL;
}

static void close_test_database() {
   dbi_conn_close(conn);
   conn = NULL;
}

/* convenience wrappers for recallable vs. legacy libdbi interface */
int my_dbi_initialize(const char *driverdir, dbi_inst *Inst) {
   if (n_legacy) {
      return dbi_initialize(driverdir);
   }
   else {
      return dbi_initialize_r(driverdir, Inst);
   }
}

void my_dbi_shutdown(dbi_inst Inst) {
   if (n_legacy) {
      dbi_shutdown();
   }
   else {
      dbi_shutdown_r(Inst);
   }
}

dbi_driver my_dbi_driver_list(dbi_driver Current, dbi_inst Inst) {
   if (n_legacy) {
      return dbi_driver_list(Current);
   }
   else {
      return dbi_driver_list_r(Current, Inst);
   }
}

dbi_conn my_dbi_conn_new(const char *name, dbi_inst Inst) {
   if (n_legacy) {
      return dbi_conn_new(name);
   }
   else {
      return dbi_conn_new_r(name, Inst);
   }
}

static void usage() {
   fprintf(stderr,
         "\nlibdbi-drivers test program: $Id: test_dbi.c,v 1.72 2013/02/24 15:06:57 mhoenicka Exp $\n\n"
         "Usage: test_dbi [options]\n"
         "       -B                Name of the build. Single submission to the dashboard\n"
         "       -C                Generate a XML test report to submit.\n"
         "                         for a specific project, environment and build type.\n"
         "       -H                Specify hosname in dashboard . Ex.: dbi0 \n"
         "       -h, -?            print this message\n\n"
         "       -N                Specify OS Name in dashboard. Ex.: Linux\n"
         "       -P                Specify OS Platform name in dashboard. Ex.: x86_64, i368\n"
         "       -R                Specify OS Release in dashboard Ex.: 2.6.28-15-server\n"
         "       -s                run a single test\n"
         "       -S                Name of the site submitting the build. Computer\n"
         "                         contributing builds to the dashboard. A site might\n"
         "                         belong to several projects and submit different build.\n"
         "       -T                Can be Nightly or Experimental or Continuous.\n"
	   "       -V                Specify OS Version. Ex.: Ubuntu SMP\n");
   exit(1);
}

/* returns 0 on success, 1 on error */
int test_custom_function(struct CONNINFO* ptr_cinfo, dbi_conn conn) {
   dbi_conn_t *myconn = conn;

   /* attempt to call a trivial function of the client library */
   if (!strcmp(ptr_cinfo->drivername, "firebird")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "freetds")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "ingres")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "msql")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "mysql")) {
      int protocol;
      unsigned int (*custom_function)(void*);

      if ((custom_function = dbi_driver_specific_function(dbi_conn_get_driver(conn), "mysql_get_proto_info")) != NULL) {
         protocol = custom_function(myconn->connection);
         printf("\tmysql_get_proto_info returned: %d\n", protocol);
         return 0;
      }
      else {
         printf("\tD'uh! Cannot run custom function\n");
         return 1;
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "oracle")) {
      fprintf(stderr, "not yet implemented\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "pgsql")) {
      int protocol;
      int (*custom_function)(void*);

      if ((custom_function = dbi_driver_specific_function(dbi_conn_get_driver(conn), "PQprotocolVersion")) != NULL) {
         protocol = custom_function(myconn->connection);
         printf("\tPQprotocolVersion returned: %d\n", protocol);
         return 0;
      }
      else {
         printf("\tD'uh! Cannot run custom function\n");
         return 1;
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite")) {
      const char* version;
      const char* (*custom_function)(void);

      if ((custom_function = dbi_driver_specific_function(dbi_conn_get_driver(conn), "sqlite_version")) != NULL) {
         version = custom_function();
         printf("\tsqlite_version returned: %s\n", version);
         return 0;
      }
      else {
         printf("\tD'uh! Cannot run custom function\n");
         return 1;
      }
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite3")) {
      const char* version;
      const char* (*custom_function)(void);

      if ((custom_function = dbi_driver_specific_function(dbi_conn_get_driver(conn), "sqlite3_libversion")) != NULL) {
         version = custom_function();
         printf("\tsqlite3_libversion returned: %s\n", version);
         return 0;
      }
      else {
         printf("\tD'uh! Cannot run custom function\n");
         return 1;
      }
   }
}

/* returns 0 on success, 1 on error */
int test_custom_function_parameters(struct CONNINFO* ptr_cinfo, dbi_conn conn) {
   dbi_conn_t *myconn = conn;

   /* attempt to call a trivial function of the client library */
   if (!strcmp(ptr_cinfo->drivername, "firebird")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "freetds")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "ingres")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "msql")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "mysql")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "oracle")) {
      fprintf(stderr, "not yet implemented\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "pgsql")) {
      int res;
      int count = 30;
      const char *error = NULL;
      const char *errmsg = NULL;
      int (*custom_function_copy)(void*, const char*, int) = NULL;
      int (*custom_function_end)(void*, const char*) = NULL;
      char* (*custom_function_error)(void*) = NULL;
      dbi_result result;

      const char *query = "COPY batch FROM STDIN";
      const char *query_data1 = "1\t2\t\"A little job\"\n";
      const char *query_data2 = "2\t3\t\"Other little job\"\n";
      const char *table_batch = "CREATE TABLE batch ( id int, jobid int, jobname varchar)";

      // Start copy

      if ((result = dbi_conn_query(conn, table_batch)) == NULL) {
         dbi_conn_error(conn, &errmsg);
         printf("\tAAH! Can't create table! Error message: %s\n", errmsg);
         return 1;
      } else {
         printf("\tOk.\n");
      }
      dbi_result_free(result);

      if ((result = dbi_conn_query(conn, query)) == NULL) {
         dbi_conn_error(conn, &errmsg);
         printf("\tAAH! Can't query! Error message: %s\n", errmsg);
         return 1;
      } else {
         printf("\tOk %s.\n", query);
      }
      dbi_result_free(result);

      // Start copy insert
      // Insert data two times
      if ((custom_function_copy = dbi_driver_specific_function(dbi_conn_get_driver(conn), "PQputCopyData")) != NULL) {

         printf("\tPQputCopyData %s\n", query_data1);
         res = custom_function_copy(myconn->connection, query_data1, strlen(query_data1));
         printf("\tPQputCopyData returned: %d\n", res);

         if (res <= 0) {
            printf("\tD'uh! PQputCopyData error\n");
            return 1;
         }

         printf("\tPQputCopyData %s\n", query_data2);
         res = custom_function_copy(myconn->connection, query_data2, strlen(query_data2));
         printf("\tPQputCopyData returned: %d\n", res);

         if (res <= 0) {
            printf("\tD'uh! PQputCopyData error\n");
            return 1;
         }
      }

      // End data
      if ((custom_function_end = dbi_driver_specific_function(dbi_conn_get_driver(conn), "PQputCopyEnd")) != NULL) {
         do {
            res = custom_function_end(myconn->connection, error);
         } while (res == 0 && --count > 0);
      }

      if (res <= 0) {
         printf("\tD'uh! PQputCopyEnd error\n");
         return 1;
      }
      printf("\tPQputCopyEnd returned: %d\n\tError: %d %s\n", res, dbi_conn_error(conn, &errmsg), errmsg);
      if ((custom_function_error = dbi_driver_specific_function(dbi_conn_get_driver(conn), "PQerrorMessage")) != NULL) {
         printf("\tPQerrorMessage returned %s\n", custom_function_error(myconn->connection));
      }

      if ((result = dbi_conn_query(conn, "SELECT * from batch")) == NULL) {
         dbi_conn_error(conn, &errmsg);
         printf("\tAAH! Can't get read data! Error message: %s\n", errmsg);
         return 1;
      }

      printf("\tGot result, %d rows, try to access rows\n", dbi_result_get_numrows(result));

      while (dbi_result_next_row(result)) {
         const char *errmsg = NULL;
         long the_long_one = 0;
         long the_long_two = 0;
         const char* the_string;

         dbi_error_flag errflag;

         /* first retrieve the values */
         the_long_one = dbi_result_get_int(result, "id");
         errflag = dbi_conn_error(dbi_result_get_conn(result), &errmsg);
         if (errflag) {
            printf("the_int_one errflag=%s\n", errmsg);
         }

         the_long_two = dbi_result_get_int(result, "jobid");
         errflag = dbi_conn_error(dbi_result_get_conn(result), &errmsg);
         if (errflag) {
            printf("the_int_two errflag=%s\n", errmsg);
         }

         the_string = dbi_result_get_string(result, "jobname");
         errflag = dbi_conn_error(dbi_result_get_conn(result), &errmsg);
         if (errflag) {
            printf("the_stringr errflag=%s\n", errmsg);
         }

         printf("\tResult: the_long_one: %d the_long_two: %d the_string: %s\n",
               the_long_one,
               the_long_two,
               the_string);
      }

      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else if (!strcmp(ptr_cinfo->drivername, "sqlite3")) {
      fprintf(stderr, "\tnot yet implemented for this driver\n");
      return 0;
   }
   else {
      printf("\tD'uh! Cannot run custom function\n");
      return 1;
   }
}

/*********** tests cases begin ***********/

/****** driver-specific tests ******/

Ensure test_another_encoding() {

   dbi_conn another_conn = NULL;
   dbi_result result;
   const char *errmsg;
   int status;
   const char *encode = NULL;

#define DROP_DB_ENCODE \
		do { \
			dbi_conn_select_db(another_conn, cinfo.initial_dbname); \
			result = dbi_conn_queryf(another_conn, "DROP DATABASE %s", "dbencode"); \
			if (result == NULL) { \
				dbi_conn_error(another_conn, &errmsg); \
				printf("\tAAH! Can't drop database connected to database Error message: %s\n", errmsg); \
				exit(1); \
			} \
			dbi_result_free(result); \
		} while(0);

   /* test begin */
   another_conn = my_dbi_conn_new(cinfo.drivername, dbi_instance);
   assert_not_equal(another_conn, NULL);

   set_driver_options(&cinfo, another_conn, "UTF-8");

   status = dbi_conn_connect(another_conn);
   assert_equal(status, 0);

   if (!strcmp(cinfo.drivername, "mysql")) {
      result = dbi_conn_queryf(another_conn, "CREATE DATABASE %s CHARACTER SET %s",
            "dbencode", "utf8");
   }
   else if (!strcmp(cinfo.drivername, "pgsql")) {
      result = dbi_conn_queryf(another_conn,  "CREATE DATABASE %s WITH ENCODING = '%s'",
            "dbencode", "UTF-8");
   }
   else {
      exit(1);
   }

   if (result == NULL)	{
      dbi_conn_error(another_conn, &errmsg);
      printf("\tAAH! Can't create database connected to database Error message: %s\n", errmsg);
      exit(1);
   }

   dbi_conn_select_db(another_conn, "dbencode");

   /* Test: get encoding of UTF-8 database */
   encode = dbi_conn_get_encoding(another_conn);

   assert_string_equal(encode, "UTF-8");

   /* drop database */
   DROP_DB_ENCODE

   /* we're done with this connection */
   dbi_conn_close(another_conn);

   another_conn = NULL;

   /* repeat test for latin1 encoding */
   another_conn = my_dbi_conn_new(cinfo.drivername, dbi_instance);
   assert_not_equal(another_conn, NULL);

   set_driver_options(&cinfo, another_conn, "ISO-8859-1");

   status = dbi_conn_connect(another_conn);
   assert_equal(status, 0);

   if (!strcmp(cinfo.drivername, "mysql")) {
      result = dbi_conn_queryf(another_conn, "CREATE DATABASE %s CHARACTER SET %s",
            "dbencode", "latin1");
   }
   else if (!strcmp(cinfo.drivername, "pgsql")) {
     unsigned int pgserver_version;

     pgserver_version = dbi_conn_get_engine_version(another_conn);

     if (pgserver_version < 80400) {
       result = dbi_conn_queryf(another_conn,  "CREATE DATABASE %s WITH ENCODING = '%s'",
				"dbencode", "LATIN1");
     }
     else {
       result = dbi_conn_queryf(another_conn,  "CREATE DATABASE %s WITH ENCODING = '%s'  LC_COLLATE='de_DE.ISO8859-1' LC_CTYPE='de_DE.ISO8859-1' TEMPLATE template0",
				"dbencode", "LATIN1");
     }
   }
   else {
      exit(1);
   }

   if (result == NULL)	{
      dbi_conn_error(another_conn, &errmsg);
      printf("\tAAH! Can't create database connected to database Error message: %s\n", errmsg);
      exit(1);
   }

   dbi_result_free(result);

   dbi_conn_select_db(another_conn, "dbencode");

   /* Test: get encoding of ISO-8859-1 database */
   encode = dbi_conn_get_encoding(another_conn);

   assert_string_equal(encode, "ISO-8859-1");

   /* we're done with this connection */
   dbi_conn_close(another_conn);

   another_conn = NULL;

   /* now make a connection to the existing database using a different encoding */
   another_conn = my_dbi_conn_new(cinfo.drivername, dbi_instance);
   assert_not_equal(another_conn, NULL);

   set_driver_options(&cinfo, another_conn, "UTF-8");

   status = dbi_conn_connect(another_conn);
   assert_equal(status, 0);

   dbi_conn_select_db(another_conn, "dbencode");

   /* Test: get encoding of UTF-8 database */
   encode = dbi_conn_get_encoding(another_conn);

   assert_string_equal(encode, "UTF-8");

   dbi_conn_close(another_conn);

   another_conn = NULL;

   /* now make a connection to the existing database using the "auto" encoding */
   another_conn = my_dbi_conn_new(cinfo.drivername, dbi_instance);
   assert_not_equal(another_conn, NULL);

   set_driver_options(&cinfo, another_conn, "auto");

   status = dbi_conn_connect(another_conn);
   assert_equal(status, 0);

   dbi_conn_select_db(another_conn, "dbencode");

   /* Test: get encoding of ISO-8859-1 database */
   encode = dbi_conn_get_encoding(another_conn);

   assert_string_equal(encode, "ISO-8859-1");

   /* Test: drop ISO-8859-1 database */
   DROP_DB_ENCODE

   dbi_conn_close(another_conn);

   another_conn = NULL;

}

Ensure test_dbi_conn_query() {
   const char *errmsg;
   int errnum;
   dbi_result result;

   if (!strcmp(cinfo.drivername, "firebird")) {
      QUERY_ASSERT_RESULT(result, "SELECT the_short FROM test_datatypes"
            " WHERE the_short = -32768");

   } else {
      QUERY_ASSERT_RESULT(result, "SELECT the_char, the_longlong FROM test_datatypes"
            " WHERE the_longlong = -9223372036854775807 and the_char = -127");
   }

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      long long the_longlong = 0;
      signed short the_short = 0;

      if(!strcmp(cinfo.drivername, "firebird")) {
         the_short = dbi_result_get_short(result, "the_short");
         assert_equal(the_short, expect_longlong_from_name("the_short", cinfo.drivername));
      }
      else {
         the_longlong = dbi_result_get_longlong(result, "the_longlong");
         assert_equal(the_longlong, expect_longlong_from_name("the_longlong", cinfo.drivername));
      }
   }

   dbi_result_free(result);
}

Ensure test_dbi_conn_queryf() {

   const char table_test_datatypes[] = "test_datatypes";
   long long the_longlong_number = -9223372036854775807;
   signed char the_char_number = -127;
   signed short the_short_number = -32768;


   dbi_result result;

   if (!strcmp(cinfo.drivername, "firebird")) {
      result = dbi_conn_queryf(conn, "SELECT the_short FROM %s"
            " WHERE the_short = '%d'", table_test_datatypes, the_short_number);
   } else {
      result = dbi_conn_queryf(conn, "SELECT the_char, the_longlong FROM %s"
            " WHERE the_longlong = '%lld' and the_char = '%d'", table_test_datatypes,
            the_longlong_number, the_char_number);
   }

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      long long the_longlong = 0;
      signed char the_char = 0;
      signed short the_short = 0;

      if(!strcmp(cinfo.drivername, "msql")) {
         the_longlong = dbi_result_get_longlong(result, "the_longlong");
         assert_equal(the_longlong, -9223372036854775807);
      }
      else if (!strcmp(cinfo.drivername, "firebird")) {
         the_short = dbi_result_get_short(result, "the_short");
         assert_equal(the_short, -32768);
      }
      else {
         the_longlong = dbi_result_get_longlong(result, "the_longlong");
         assert_equal(the_longlong, -9223372036854775807);
      }
   }

   dbi_result_free(result);
}

// TODO: test_dbi_conn_query_null()
Ensure test_dbi_conn_query_null() {

}

// TODO: test_dbi_conn_ping()
Ensure test_dbi_conn_ping() {

}

Ensure test_dbi_conn_sequence_last() {

   unsigned long long n_last_id = 0;

   if (!strcmp(cinfo.drivername, "pgsql")
         || !strcmp(cinfo.drivername, "ingres")) {
      n_last_id = dbi_conn_sequence_last(conn, "test_datatypes_id_seq");
   } else if (!strcmp(cinfo.drivername, "firebird")) {
      n_last_id = dbi_conn_sequence_last(conn, "GEN_T1_ID");
   }
   else {
      n_last_id = dbi_conn_sequence_last(conn, NULL);
   }

   assert_equal(n_last_id, 1);

}

Ensure test_dbi_conn_sequence_next() {

   unsigned long long n_next_id = 0;

   if (!strcmp(cinfo.drivername, "pgsql") ||
         !strcmp(cinfo.drivername, "ingres")) {
      n_next_id = dbi_conn_sequence_next(conn, "test_datatypes_id_seq");
      assert_equal(n_next_id, 2);
   }
   else if (!strcmp(cinfo.drivername, "firebird")) {
      n_next_id = dbi_conn_sequence_next(conn, "GEN_T1_ID");
      assert_equal(n_next_id, 2);
   }
   else if (!strcmp(cinfo.drivername, "mysql")) {
      n_next_id = dbi_conn_sequence_next(conn, NULL);
      assert_equal(n_next_id, 0);
   }
   else {
      n_next_id = dbi_conn_sequence_next(conn, NULL);
      assert_equal(n_next_id, 0);
   }

}

Ensure test_dbi_conn_quote_binary_copy() {
   dbi_result result;
   unsigned char* quoted_binary = NULL;
   size_t quoted_binary_length;

   quoted_binary_length = dbi_conn_quote_binary_copy(conn, binary_to_quote, binary_to_quote_length, &quoted_binary);

   if(!strcmp(cinfo.drivername, "mysql")) {
      assert_equal(39, quoted_binary[0]);
      assert_equal(65, quoted_binary[1]);
      assert_equal(66, quoted_binary[2]);
      assert_equal(92, quoted_binary[3]);
      assert_equal(48, quoted_binary[4]);
      assert_equal(67, quoted_binary[5]);
      assert_equal(92, quoted_binary[6]);
      assert_equal(39, quoted_binary[7]);
      assert_equal(68, quoted_binary[8]);
      assert_equal(39, quoted_binary[9]);
   } else if(!strcmp(cinfo.drivername, "pgsql")) {
     unsigned int pgserver_version;

     pgserver_version = dbi_conn_get_engine_version(conn);

     if (pgserver_version < 90000) {
       /* server uses old binary format by default */
       assert_equal(39, quoted_binary[0]);
       assert_equal(65, quoted_binary[1]);
       assert_equal(66, quoted_binary[2]);
       assert_equal(92, quoted_binary[3]);
       assert_equal(92, quoted_binary[4]);
       assert_equal(48, quoted_binary[5]);
       assert_equal(48, quoted_binary[6]);
       assert_equal(48, quoted_binary[7]);
       assert_equal(67, quoted_binary[8]);
       assert_equal(39, quoted_binary[9]);
       assert_equal(39, quoted_binary[10]);
       assert_equal(68, quoted_binary[11]);
       assert_equal(39, quoted_binary[12]);     }
     else {
       /* server uses hex format by default */
       assert_equal(39, quoted_binary[0]);
       assert_equal(92, quoted_binary[1]);
       assert_equal(120, quoted_binary[2]);
       assert_equal(52, quoted_binary[3]);
       assert_equal(49, quoted_binary[4]);
       assert_equal(52, quoted_binary[5]);
       assert_equal(50, quoted_binary[6]);
       assert_equal(48, quoted_binary[7]);
       assert_equal(48, quoted_binary[8]);
       assert_equal(52, quoted_binary[9]);
       assert_equal(51, quoted_binary[10]);
       assert_equal(50, quoted_binary[11]);
       assert_equal(55, quoted_binary[12]);
       assert_equal(52, quoted_binary[13]);
       assert_equal(52, quoted_binary[14]);
       assert_equal(39, quoted_binary[15]);
     }
   } else if(!strcmp(cinfo.drivername, "sqlite") || !strcmp(cinfo.drivername, "sqlite3") ||
         !strcmp(cinfo.drivername, "firebird")) {
      assert_equal(39, quoted_binary[0]);
      assert_equal(1,  quoted_binary[1]);
      assert_equal(64, quoted_binary[2]);
      assert_equal(65, quoted_binary[3]);
      assert_equal(255, quoted_binary[4]);
      assert_equal(66, quoted_binary[5]);
      assert_equal(38, quoted_binary[6]);
      assert_equal(67, quoted_binary[7]);
      assert_equal(39, quoted_binary[8]);
   }
}

Ensure test_dbi_conn_escape_binary_copy() {
   dbi_result result;
   unsigned char* escaped_binary = NULL;
   size_t escaped_binary_length;

   escaped_binary_length = dbi_conn_escape_binary_copy(conn, binary_to_escape, binary_to_escape_length, &escaped_binary);

   if(!strcmp(cinfo.drivername, "mysql")) {
      assert_equal(65, escaped_binary[0]);
      assert_equal(66, escaped_binary[1]);
      assert_equal(92, escaped_binary[2]);
      assert_equal(48, escaped_binary[3]);
      assert_equal(67, escaped_binary[4]);
      assert_equal(92, escaped_binary[5]);
      assert_equal(39, escaped_binary[6]);
      assert_equal(68, escaped_binary[7]);
   } else if(!strcmp(cinfo.drivername, "pgsql")) {
     unsigned int pgserver_version;

     pgserver_version = dbi_conn_get_engine_version(conn);

     if (pgserver_version < 90000) {
       /* server uses old binary format by default */
       assert_equal(65, escaped_binary[0]);
       assert_equal(66, escaped_binary[1]);
       assert_equal(92, escaped_binary[2]);
       assert_equal(92, escaped_binary[3]);
       assert_equal(48, escaped_binary[4]);
       assert_equal(48, escaped_binary[5]);
       assert_equal(48, escaped_binary[6]);
       assert_equal(67, escaped_binary[7]);
       assert_equal(39, escaped_binary[8]);
       assert_equal(39, escaped_binary[9]);
       assert_equal(68, escaped_binary[10]);     }
     else {
       /* server uses hex format by default */
       assert_equal(92, escaped_binary[0]);
       assert_equal(120, escaped_binary[1]);
       assert_equal(52, escaped_binary[2]);
       assert_equal(49, escaped_binary[3]);
       assert_equal(52, escaped_binary[4]);
       assert_equal(50, escaped_binary[5]);
       assert_equal(48, escaped_binary[6]);
       assert_equal(48, escaped_binary[7]);
       assert_equal(52, escaped_binary[8]);
       assert_equal(51, escaped_binary[9]);
       assert_equal(50, escaped_binary[10]);
       assert_equal(55, escaped_binary[11]);
       assert_equal(52, escaped_binary[12]);
       assert_equal(52, escaped_binary[13]);
     }
   } else if(!strcmp(cinfo.drivername, "sqlite") || !strcmp(cinfo.drivername, "sqlite3") ||
         !strcmp(cinfo.drivername, "firebird")) {
      assert_equal(1,  escaped_binary[0]);
      assert_equal(64, escaped_binary[1]);
      assert_equal(65, escaped_binary[2]);
      assert_equal(255, escaped_binary[3]);
      assert_equal(66, escaped_binary[4]);
      assert_equal(38, escaped_binary[5]);
      assert_equal(67, escaped_binary[6]);
   }
}

Ensure test_retrieve_zero_rows() {
   dbi_result result;
   int numfields;

   result = dbi_conn_query(conn, "SELECT * from test_datatypes WHERE '0'='1'");
   ASSERT_RESULT

   numfields = dbi_result_get_numfields(result);

   assert_not_equal(numfields, DBI_FIELD_ERROR);

   if (!strcmp(cinfo.drivername, "mysql")) {
      assert_equal(numfields, 25);
   }
   else if (!strcmp(cinfo.drivername, "pgsql")) {
      assert_equal(numfields, 25);
   }
   else if (!strcmp(cinfo.drivername, "sqlite3")) {
      assert_equal(numfields, 26);
   }
   else if (!strcmp(cinfo.drivername, "firebird")) {
      assert_equal(numfields, 21);
   } else { /* this includes sqlite */
      assert_equal(numfields, 0);
   }

   dbi_result_free(result);
}

Ensure test_dbi_conn_select_db() {
   const char *errmsg;

   if (!strcmp(cinfo.drivername, "firebird")) {
      /* TODO: firebird can't handle this? */
   }
   else if (!strcmp(cinfo.drivername, "pgsql")) {
     /* TODO: connecting to initial_dbname first, then to dbname causes a SIGPIPE although both asserts succeed */
      /* assert_not_equal(dbi_conn_select_db(conn, cinfo.initial_dbname), -1); */
      /* printf("about to select previous db\n"); */
      /* assert_not_equal(dbi_conn_select_db(conn, cinfo.dbname), -1); */
      /* printf("done selecting previous db\n"); */
   }
   else {
      assert_not_equal(dbi_conn_select_db(conn, cinfo.initial_dbname), -1);
      dbi_conn_select_db(conn, cinfo.dbname);
   }
}

/* transactions */
Ensure test_dbi_conn_transaction_commit() {
   const char *errmsg;
   dbi_result result;
   int int_result;
   int errnum;

   if (!dbi_conn_cap_get(conn, "transaction_support")) {
     /* test not applicable */
     return;
   }

   /* check initial value */

   QUERY_ASSERT_RESULT(result, "SELECT the_long FROM test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_long = 0;

      the_long = dbi_result_get_int_idx(result, 1);
      assert_equal(the_long, -2147483648);
   }

   dbi_result_free(result);

   /* start transaction */
   int_result = dbi_conn_transaction_begin(conn);
   assert_equal(int_result, 0);

   /* update value */
   QUERY_ASSERT_RESULT(result, "UPDATE test_datatypes SET the_long=0");


   /* commit transaction */
   int_result = dbi_conn_transaction_commit(conn);
   assert_equal(int_result, 0);


   /* check value again */
   QUERY_ASSERT_RESULT(result, "SELECT the_long FROM test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_long = 0;

      the_long = dbi_result_get_int_idx(result, 1);
      assert_equal(the_long, 0);
   }

   dbi_result_free(result);

}

Ensure test_dbi_conn_transaction_rollback() {
   const char *errmsg;
   dbi_result result;
   int int_result;
   int errnum;

   if (!dbi_conn_cap_get(conn, "transaction_support")) {
     /* test not applicable */
     return;
   }

   /* check initial value */

   QUERY_ASSERT_RESULT(result, "SELECT the_long FROM test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_long = 0;

      the_long = dbi_result_get_int_idx(result, 1);
      assert_equal(the_long, -2147483648);
   }

   dbi_result_free(result);

   /* start transaction */
   int_result = dbi_conn_transaction_begin(conn);
   assert_equal(int_result, 0);

   /* update value */
   QUERY_ASSERT_RESULT(result, "UPDATE test_datatypes SET the_long=0");


   /* rollback transaction */
   int_result = dbi_conn_transaction_rollback(conn);
   assert_equal(int_result, 0);


   /* check value again */
   QUERY_ASSERT_RESULT(result, "SELECT the_long FROM test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_long = 0;

      the_long = dbi_result_get_int_idx(result, 1);
      assert_equal(the_long, -2147483648);
   }

   dbi_result_free(result);

}

Ensure test_dbi_conn_rollback_to_savepoint() {
   const char *errmsg;
   dbi_result result;
   int int_result;
   int errnum;

   if (!dbi_conn_cap_get(conn, "savepoint_support")) {
     /* test not applicable */
     return;
   }

   /* check initial value */

   QUERY_ASSERT_RESULT(result, "SELECT the_long FROM test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_long = 0;

      the_long = dbi_result_get_int_idx(result, 1);
      assert_equal(the_long, -2147483648);
   }

   dbi_result_free(result);

   /* start transaction */
   int_result = dbi_conn_transaction_begin(conn);
   assert_equal(int_result, 0);

   /* set savepoint */
   int_result = dbi_conn_savepoint(conn, "my_savepoint");
   assert_equal(int_result, 0);

   /* update value */
   QUERY_ASSERT_RESULT(result, "UPDATE test_datatypes SET the_long=0");

   /* rollback to savepoint */
   int_result = dbi_conn_rollback_to_savepoint(conn, "my_savepoint");
   assert_equal(int_result, 0);

   /* commit transaction */
   int_result = dbi_conn_transaction_commit(conn);
   assert_equal(int_result, 0);

   /* check value again */
   QUERY_ASSERT_RESULT(result, "SELECT the_long FROM test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_long = 0;

      the_long = dbi_result_get_int_idx(result, 1);
      assert_equal(the_long, -2147483648);
   }

   dbi_result_free(result);

}

Ensure test_dbi_conn_release_savepoint() {
   const char *errmsg;
   dbi_result result;
   int int_result;
   int errnum;

   if (!dbi_conn_cap_get(conn, "savepoint_support")) {
     /* test not applicable */
     return;
   }

   /* start transaction */
   int_result = dbi_conn_transaction_begin(conn);
   assert_equal(int_result, 0);

   /* set savepoint */
   int_result = dbi_conn_savepoint(conn, "my_savepoint");
   assert_equal(int_result, 0);

   /* release savepoint */
   int_result = dbi_conn_release_savepoint(conn, "my_savepoint");
   assert_equal(int_result, 0);

   /* rollback to savepoint */
   int_result = dbi_conn_rollback_to_savepoint(conn, "my_savepoint");
   assert_equal(int_result, 1);

   /* commit transaction */
   int_result = dbi_conn_transaction_commit(conn);
   assert_equal(int_result, 0);

}

// TODO: make tests to specific functions

/* Postgresql specific funtions */

Ensure test_pgsql_copy() {

}

/****** no driver-specific tests ******/

Ensure test_create_another_connection() {
   dbi_conn another_conn = NULL;
   dbi_result result;

   int status;

   another_conn = my_dbi_conn_new(cinfo.drivername, dbi_instance);
   assert_not_equal(another_conn, NULL);

   set_driver_options(&cinfo, another_conn, "");

   status = dbi_conn_connect(another_conn);
   assert_equal(status, 0);

   result = dbi_conn_query(another_conn, "SELECT the_conn_quoted_string from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      const char* the_quoted_string;

      the_quoted_string = dbi_result_get_string(result, "the_conn_quoted_string");
      assert_string_equal(the_quoted_string,  expect_as_string_from_name("the_conn_quoted_string", cinfo.drivername));

   }

   dbi_result_free(result);

   dbi_conn_close(another_conn);

   another_conn = NULL;
}

Ensure test_dbi_conn_quote_string() {
   dbi_result result;
   char *my_string_to_quote = NULL;

   my_string_to_quote = strdup(string_to_quote);

   dbi_conn_quote_string(conn, &my_string_to_quote);

   assert_string_equal(my_string_to_quote, expect_as_string_from_name("_quoted_string", cinfo.drivername));

   free(my_string_to_quote);

}

Ensure test_dbi_conn_quote_string_copy() {
   dbi_result result;
   char *quoted_string = NULL;
   size_t quoted_string_length;

   quoted_string_length = dbi_conn_quote_string_copy(conn, string_to_quote, &quoted_string);

   assert_string_equal(quoted_string, expect_as_string_from_name("_quoted_string", cinfo.drivername));

}

Ensure test_dbi_conn_escape_string() {
   dbi_result result;
   char *my_string_to_escape = NULL;

   my_string_to_escape = strdup(string_to_escape);

   dbi_conn_escape_string(conn, &my_string_to_escape);

   assert_string_equal(my_string_to_escape, expect_as_string_from_name("_escaped_string", cinfo.drivername));

   free(my_string_to_escape);
}

Ensure test_dbi_conn_escape_string_copy() {
   dbi_result result;
   char *escaped_string = NULL;
   size_t escaped_string_length;

   escaped_string_length = dbi_conn_escape_string_copy(conn, string_to_escape, &escaped_string);

   assert_string_equal(escaped_string, expect_as_string_from_name("_escaped_string", cinfo.drivername));

}

Ensure test_dbi_result_get_conn() {
   dbi_result result;
   dbi_conn getcon;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");

   ASSERT_RESULT

   getcon = dbi_result_get_conn(result);

   assert_not_equal_with_message(getcon, NULL, "Value: '%d' Error %d: %s",
         getcon, dbi_conn_error(conn, &errmsg), errmsg);

   dbi_result_free(result);

}

Ensure test_dbi_result_free() {

}

Ensure test_dbi_result_seek_row() {
   dbi_result result;
   int seekrow;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   seekrow = dbi_result_seek_row(result, 3);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(seekrow, 1, "Value '%d' Error: %s",
         seekrow, errmsg);

   dbi_result_free(result);

}

Ensure test_dbi_result_first_row() {
   dbi_result result;
   int firstrow;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   firstrow = dbi_result_first_row(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(firstrow, 1, "Value: '%d' Error: %s",
         firstrow, errmsg);

   dbi_result_free(result);

}

Ensure test_dbi_result_last_row() {
   dbi_result result;
   int lastrow;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   lastrow = dbi_result_last_row(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(lastrow, 1, "Value: '%d' Error: %s",
         lastrow, errmsg);

   dbi_result_free(result);

}

Ensure test_dbi_result_prev_row() {
   dbi_result result;
   int prevrow;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   dbi_result_next_row(result);
   dbi_result_next_row(result);

   prevrow = dbi_result_prev_row(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(prevrow, 1, "Value: '%d' Error: %s",
         prevrow, errmsg);

   dbi_result_free(result);

}

Ensure test_dbi_result_next_row() {
   dbi_result result;
   int nextrow;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   nextrow = dbi_result_next_row(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(nextrow, 1, "Value: '%d' Error: %s",
         nextrow, errmsg);

   dbi_result_free(result);

}

Ensure test_dbi_result_get_currow() {
   dbi_result result;
   unsigned long long currow;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   dbi_result_next_row(result);
   dbi_result_next_row(result);

   currow = dbi_result_get_currow(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(currow, 2, "Value: '%d' Error: %s",
         currow, errmsg);

   dbi_result_free(result);
}

Ensure test_dbi_result_get_numrows() {
   dbi_result result;
   unsigned long long numrows;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   numrows = dbi_result_get_numrows(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(numrows, 5, "Value: '%d' Error: %s",
         numrows, errmsg);

   dbi_result_free(result);
}

Ensure test_dbi_result_get_numrows_affected() {
   dbi_result result;
   unsigned long long numrows;

   result = dbi_conn_query(conn, "UPDATE test_datatypes set the_char=-126 where the_char=-127 ");
   ASSERT_RESULT

   numrows = dbi_result_get_numrows_affected(result);

   dbi_conn_error(conn, &errmsg);
   assert_equal_with_message(numrows, 5, "Value: '%d' Error: %s",
         numrows, errmsg);

   dbi_result_free(result);
}

Ensure test_dummy() {
   dbi_result result;
   const char *errmsg;
   int numfields;
   char smt[1024];
   int i;

   result = dbi_conn_query(conn, "CREATE TABLE test0 (the_char SMALLINT, the_long INTEGER)");
   dbi_result_free(result);

   for(i = 0; i < 30; i++) {
      sprintf(smt, "INSERT INTO test0 VALUES (%d, %d)", i, i );
      result = dbi_conn_query(conn, smt);
      dbi_result_free(result);
   }

   result = dbi_conn_query(conn, "SELECT the_long from test0");
   assert_not_equal_with_message( result, NULL, "Value: '%p' Error %d: '%s'",
         result, dbi_conn_error(conn, &errmsg), errmsg);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      long long the_long = 0;

      the_long = dbi_result_get_as_longlong(result, "the_long");

   }

   dbi_result_free(result);

   result = dbi_conn_query(conn, "DROP TABLE test0");

   dbi_result_free(result);
}

Ensure test_dbi_result_get_as_longlong() {
   dbi_result result;
   char *query_string;
   int numfields;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);
   ASSERT_RESULT

   free(query_string);

   while (dbi_result_next_row(result)) {
      const char *errmsg = NULL;
      long long the_char_as_ll = 0;
      long long the_uchar_as_ll = 0;
      long long the_short_as_ll = 0;
      long long the_ushort_as_ll = 0;
      long long the_long_as_ll = 0;
      long long the_ulong_as_ll = 0;
      long long the_longlong_as_ll = 0;
      long long the_ulonglong_as_ll = 0;
      long long the_float_as_ll = 0;
      long long the_double_as_ll = 0;
      long long the_string_as_ll = 0;
      long long the_numstring_as_ll = 0;
      long long the_empty_string_as_ll = 0;
      long long the_null_string_as_ll = 0;
      long long the_binary_as_ll = 0;
      long long the_date_as_ll = 0;
      long long the_time_as_ll = 0;
      long long the_datetime_as_ll = 0;

      the_char_as_ll = dbi_result_get_as_longlong(result, "the_char");
      the_uchar_as_ll = dbi_result_get_as_longlong(result, "the_uchar");
      the_short_as_ll = dbi_result_get_as_longlong(result, "the_short");
      the_ushort_as_ll = dbi_result_get_as_longlong(result, "the_ushort");
      the_long_as_ll = dbi_result_get_as_longlong(result, "the_long");
      the_ulong_as_ll = dbi_result_get_as_longlong(result, "the_ulong");

      if (tinfo.have_longlong) {
         the_longlong_as_ll = dbi_result_get_as_longlong(result, "the_longlong");
      }
      if (tinfo.have_ulonglong) {
         the_ulonglong_as_ll = dbi_result_get_as_longlong(result, "the_ulonglong");
      }
      the_float_as_ll = dbi_result_get_as_longlong(result, "the_float");
      if(tinfo.have_double) {
         the_double_as_ll = dbi_result_get_as_longlong(result, "the_double");
      }

      the_string_as_ll = dbi_result_get_as_longlong(result, "the_conn_quoted_string_copy");
      the_numstring_as_ll = dbi_result_get_as_longlong(result, "the_numstring");
      the_empty_string_as_ll = dbi_result_get_as_longlong(result, "the_empty_string");
      the_null_string_as_ll = dbi_result_get_as_longlong(result, "the_null_string");
      the_binary_as_ll = dbi_result_get_as_longlong(result, "the_binary_quoted_string");

      if(tinfo.have_datetime) {
         the_datetime_as_ll = dbi_result_get_as_longlong(result, "the_datetime");
      }

      the_date_as_ll = dbi_result_get_as_longlong(result, "the_date");
      the_time_as_ll = dbi_result_get_as_longlong(result, "the_time");

      assert_equal(the_char_as_ll, expect_as_longlong_from_name("the_char", cinfo.drivername));
      assert_equal(the_uchar_as_ll, expect_as_longlong_from_name("the_uchar", cinfo.drivername));
      assert_equal(the_short_as_ll, expect_as_longlong_from_name("the_short", cinfo.drivername));
      assert_equal(the_ushort_as_ll, expect_as_longlong_from_name("the_ushort", cinfo.drivername));
      assert_equal(the_long_as_ll, expect_as_longlong_from_name("the_long", cinfo.drivername));
      assert_equal(the_ulong_as_ll, expect_as_longlong_from_name("the_ulong", cinfo.drivername));
      assert_equal(the_longlong_as_ll, expect_as_longlong_from_name("the_longlong", cinfo.drivername));
      assert_equal(the_ulonglong_as_ll, expect_as_longlong_from_name("the_ulonglong", cinfo.drivername));
      assert_equal(the_float_as_ll, expect_as_longlong_from_name("the_float", cinfo.drivername));
      assert_equal(the_double_as_ll, expect_as_longlong_from_name("the_double", cinfo.drivername));
      assert_equal(the_string_as_ll, expect_as_longlong_from_name("the_conn_quoted_string_copy", cinfo.drivername));
      assert_equal(the_numstring_as_ll, expect_as_longlong_from_name("the_numstring", cinfo.drivername));
      assert_equal(the_empty_string_as_ll, expect_as_longlong_from_name("the_empty_string", cinfo.drivername));
      assert_equal(the_null_string_as_ll, expect_as_longlong_from_name("the_null_string", cinfo.drivername));
      assert_equal(the_datetime_as_ll, expect_as_longlong_from_name("the_datetime", cinfo.drivername));
      assert_equal(the_date_as_ll, expect_as_longlong_from_name("the_date", cinfo.drivername));
      assert_equal(the_time_as_ll, expect_as_longlong_from_name("the_time", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_as_string() {
   dbi_result result;
   char *query_string;
   int numfields;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);
   ASSERT_RESULT

   free(query_string);

   while (dbi_result_next_row(result)) {
      const char *errmsg = NULL;
      char* the_char_as_string = NULL;
      char* the_uchar_as_string = NULL;
      char* the_short_as_string = NULL;
      char* the_ushort_as_string = NULL;
      char* the_long_as_string = NULL;
      char* the_ulong_as_string = NULL;
      char* the_longlong_as_string = NULL;
      char* the_ulonglong_as_string = NULL;
      char* the_float_as_string = NULL;
      char* the_double_as_string = NULL;
      char* the_string_as_string = NULL;
      char* the_numstring_as_string = NULL;
      char* the_empty_string_as_string = NULL;
      char* the_null_string_as_string = NULL;
      char* the_binary_as_string = NULL;
      char* the_date_as_string = NULL;
      char* the_time_as_string = NULL;
      char* the_datetime_as_string = NULL;

      /* get data */
      the_char_as_string = dbi_result_get_as_string_copy(result, "the_char");
      the_uchar_as_string = dbi_result_get_as_string_copy(result, "the_uchar");
      the_short_as_string = dbi_result_get_as_string_copy(result, "the_short");
      the_ushort_as_string = dbi_result_get_as_string_copy(result, "the_ushort");
      the_long_as_string = dbi_result_get_as_string_copy(result, "the_long");
      the_ulong_as_string = dbi_result_get_as_string_copy(result, "the_ulong");
      if (tinfo.have_longlong) {
         the_longlong_as_string = dbi_result_get_as_string_copy(result, "the_longlong");
      }
      if (tinfo.have_ulonglong) {
         the_ulonglong_as_string = dbi_result_get_as_string_copy(result, "the_ulonglong");
      }
      the_float_as_string = dbi_result_get_as_string_copy(result, "the_float");
      if(tinfo.have_double) {
         the_double_as_string = dbi_result_get_as_string_copy(result, "the_double");
      }
      the_string_as_string = dbi_result_get_as_string_copy(result, "the_conn_quoted_string_copy");
      the_numstring_as_string = dbi_result_get_as_string_copy(result, "the_numstring");
      the_empty_string_as_string = dbi_result_get_as_string_copy(result, "the_empty_string");
      the_null_string_as_string = dbi_result_get_as_string_copy(result, "the_null_string");
      the_binary_as_string = dbi_result_get_as_string_copy(result, "the_binary_quoted_string");
      if(tinfo.have_datetime) {
         the_datetime_as_string = dbi_result_get_as_string_copy(result, "the_datetime");
      }

      if(!strcmp(cinfo.drivername, "msql")) {
         the_date_as_string = dbi_result_get_as_string_copy(result, "the_date");
         the_time_as_string = dbi_result_get_as_string_copy(result, "the_time");
      } else { //not msql
         the_date_as_string = dbi_result_get_as_string_copy(result, "the_date");
         the_time_as_string = dbi_result_get_as_string_copy(result, "the_time");
      }

      /* do asserts */
      assert_string_equal(the_char_as_string, expect_as_string_from_name("the_char", cinfo.drivername));
      assert_string_equal(the_uchar_as_string, expect_as_string_from_name("the_uchar", cinfo.drivername));
      assert_string_equal(the_short_as_string, expect_as_string_from_name("the_short", cinfo.drivername));
      assert_string_equal(the_ushort_as_string, expect_as_string_from_name("the_ushort", cinfo.drivername));
      assert_string_equal(the_long_as_string, expect_as_string_from_name("the_long", cinfo.drivername));
      assert_string_equal(the_ulong_as_string, expect_as_string_from_name("the_ulong", cinfo.drivername));
      assert_string_equal(the_longlong_as_string, expect_as_string_from_name("the_longlong", cinfo.drivername));
      assert_string_equal(the_ulonglong_as_string, expect_as_string_from_name("the_ulonglong", cinfo.drivername));
      assert_string_equal(the_float_as_string, expect_as_string_from_name("the_float", cinfo.drivername));
      assert_string_equal(the_string_as_string, expect_as_string_from_name("the_conn_quoted_string_copy", cinfo.drivername));
      assert_string_equal(the_numstring_as_string, expect_as_string_from_name("the_numstring", cinfo.drivername));
      assert_string_equal(the_empty_string_as_string, expect_as_string_from_name("the_empty_string", cinfo.drivername));
      assert_string_equal(the_null_string_as_string, expect_as_string_from_name("the_null_string", cinfo.drivername));
      assert_string_equal(the_datetime_as_string, expect_as_string_from_name("the_datetime", cinfo.drivername));
      assert_string_equal(the_date_as_string, expect_as_string_from_name("the_date", cinfo.drivername));
      assert_string_equal(the_time_as_string, expect_as_string_from_name("the_time", cinfo.drivername));

      if (the_char_as_string) {
         free(the_char_as_string);
      }
      if (the_uchar_as_string) {
         free(the_uchar_as_string);
      }
      if (the_short_as_string) {
         free(the_short_as_string);
      }
      if (the_ushort_as_string) {
         free(the_ushort_as_string);
      }
      if (the_long_as_string) {
         free(the_long_as_string);
      }
      if (the_ulong_as_string) {
         free(the_ulong_as_string);
      }
      if (the_longlong_as_string) {
         free(the_longlong_as_string);
      }
      if (the_ulonglong_as_string) {
         free(the_ulonglong_as_string);
      }
      if (the_float_as_string) {
         free(the_float_as_string);
      }
      if (the_double_as_string) {
         free(the_double_as_string);
      }
      if (the_string_as_string) {
         free(the_string_as_string);
      }
      if (the_numstring_as_string) {
         free(the_numstring_as_string);
      }
      if (the_empty_string_as_string) {
         free(the_empty_string_as_string);
      }
      if (the_null_string_as_string) {
         free(the_null_string_as_string);
      }
      if (the_binary_as_string) {
         free(the_binary_as_string);
      }
      if (the_date_as_string) {
         free(the_date_as_string);
      }
      if (the_time_as_string) {
         free(the_time_as_string);
      }
      if (the_datetime_as_string) {
         free(the_datetime_as_string);
      }
   }
   dbi_result_free(result);
}

Ensure test_dbi_result_get_char_idx() {
   dbi_result result;

   unsigned short type_expected = 0;

   type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);

   if (type_expected == 2) { /* skip test for db engines which do not support a true one-byte char type */
     result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");

     ASSERT_RESULT

       while (dbi_result_next_row(result)) {
	 errmsg = NULL;
	 signed char the_char = 0;

	 the_char = dbi_result_get_char_idx(result, 1);

	 assert_equal(the_char, expect_longlong_from_name("the_char", cinfo.drivername));

       }

     dbi_result_free(result);
   }
}

Ensure test_dbi_result_get_uchar_idx() {
   dbi_result result;
   unsigned short type_expected = 0;

   type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);

   if (type_expected == 2) { /* skip test for db engines which do not support a true one-byte char type */

     result = dbi_conn_query(conn, "SELECT the_uchar from test_datatypes");

     ASSERT_RESULT

       while (dbi_result_next_row(result)) {
	 errmsg = NULL;
	 unsigned char the_uchar = 0;

	 the_uchar = dbi_result_get_uchar_idx(result, 1);

	 assert_equal(the_uchar, expect_ulonglong_from_name("the_uchar", cinfo.drivername));

       }

     dbi_result_free(result);
   }
}

Ensure test_dbi_result_get_short_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_short from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      short the_short = 0;

      the_short = dbi_result_get_short_idx(result, 1);

      assert_equal(the_short, expect_longlong_from_name("the_short", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_ushort_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_ushort from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned short the_ushort = 0;

      the_ushort = dbi_result_get_ushort_idx(result, 1);

      assert_equal(the_ushort, expect_ulonglong_from_name("the_ushort", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_int_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_long from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_int = 0;

      the_int = dbi_result_get_int_idx(result, 1);

      assert_equal(the_int, expect_longlong_from_name("the_long", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_uint_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_ulong from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int the_uint = 0;

      the_uint = dbi_result_get_uint_idx(result, 1);

      assert_equal(the_uint, expect_ulonglong_from_name("the_ulong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_longlong_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_longlong from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      long long the_longlong = 0;

      the_longlong = dbi_result_get_longlong_idx(result, 1);


      assert_equal(the_longlong, expect_longlong_from_name("the_longlong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_ulonglong_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_ulonglong from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned long long the_ulonglong = 0;

      the_ulonglong = dbi_result_get_ulonglong_idx(result, 1);

      assert_equal(the_ulonglong, expect_ulonglong_from_name("the_ulonglong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_float_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_float from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      float the_float = 0;

      the_float = dbi_result_get_float_idx(result, 1);

      assert_double_equal(the_float, expect_double_from_name("the_float", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_double_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_double from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      double the_double = 0;

      the_double = dbi_result_get_double_idx(result, 1);

      assert_double_equal(the_double, expect_double_from_name("the_double", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_string_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_null_string, the_conn_escaped_string, the_empty_string,"
         " the_conn_quoted_string from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const char* the_quoted_string;
      const char* the_escaped_string;
      const char* the_null_string;
      const char* the_empty_string;

      the_quoted_string = dbi_result_get_string_idx(result, 4);
      assert_string_equal(the_quoted_string,  expect_as_string_from_name("the_conn_quoted_string", cinfo.drivername));

      the_escaped_string = dbi_result_get_string_idx(result, 2);
      assert_string_equal(the_escaped_string, expect_as_string_from_name("the_conn_escaped_string", cinfo.drivername));

      the_null_string = dbi_result_get_string_idx(result, 1);
      assert_string_equal(the_null_string, expect_string_from_name("the_null_string", cinfo.drivername));

      the_empty_string = dbi_result_get_string_idx(result, 3);
      assert_string_equal(the_empty_string, expect_as_string_from_name("the_empty_string", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_string_copy_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_conn_quoted_string, the_conn_escaped_string_copy"
         " from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const char* the_quoted_string_copy;
      const char* the_escaped_string_copy;

      the_quoted_string_copy = dbi_result_get_string_copy_idx(result, 1);
      assert_string_equal(the_quoted_string_copy, expect_as_string_from_name("the_conn_quoted_string", cinfo.drivername));

      the_escaped_string_copy = dbi_result_get_string_copy_idx(result, 2);
      assert_string_equal(the_escaped_string_copy, expect_as_string_from_name("the_conn_escaped_string_copy", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_binary_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_binary_quoted_string from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const unsigned char* the_binary;

      the_binary = dbi_result_get_binary_idx(result, 1);

      assert_equal(65, the_binary[0]);
      assert_equal(66, the_binary[1]);
      assert_equal(0,  the_binary[2]);
      assert_equal(67, the_binary[3]);
      assert_equal(39, the_binary[4]);
      assert_equal(68, the_binary[5]);
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_binary_copy_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_binary_escaped_string from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned char* the_binary_copy = NULL;

      the_binary_copy = dbi_result_get_binary_copy_idx(result, 1);

      assert_equal(65, the_binary_copy[0]);
      assert_equal(66, the_binary_copy[1]);
      assert_equal(0, the_binary_copy[2]);
      assert_equal(67, the_binary_copy[3]);
      assert_equal(39, the_binary_copy[4]);
      assert_equal(68, the_binary_copy[5]);

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_datetime_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_datetime, the_date, the_time from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      time_t the_datetime = 0;
      time_t the_date = 0;
      time_t the_time = 0;

      the_datetime = dbi_result_get_datetime_idx(result, 1);
      assert_equal( the_datetime, expect_longlong_from_name("the_datetime", cinfo.drivername));

      the_date = dbi_result_get_datetime_idx(result, 2);
      assert_equal(the_date, expect_longlong_from_name("the_date", cinfo.drivername));

      the_time = dbi_result_get_datetime_idx(result, 3);
      assert_equal(the_time, expect_longlong_from_name("the_time", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_datetime_tz_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_datetime_tz from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      time_t the_datetime_tz = 0;

      the_datetime_tz = dbi_result_get_datetime_idx(result, 1);

      assert_equal( the_datetime_tz, expect_longlong_from_name("the_datetime_tz", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_datetime_time_tz_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_datetime_tz, the_time_tz from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      time_t the_datetime_tz = 0;
      time_t the_time_dt_tz = 0;

      the_datetime_tz = dbi_result_get_datetime_idx(result, 1);
      the_time_dt_tz = dbi_result_get_datetime_idx(result, 2);

      assert_equal( the_datetime_tz, expect_longlong_from_name("the_datetime_tz", cinfo.drivername));
      assert_equal( the_time_dt_tz, expect_longlong_from_name("the_time_tz", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_char() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      signed char the_char = 0;
      unsigned short type_expected = 0;

      type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);
      if (type_expected == 2) {
	the_char = dbi_result_get_char(result, "the_char");
      }
      else { /* does not support one-byte char type */
	the_char = (signed char)dbi_result_get_short(result, "the_char");
      }

      assert_equal(the_char, expect_longlong_from_name("the_char", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_uchar() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_uchar from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned char the_uchar = 0;
      unsigned short type_expected = 0;

      type_expected = field_attrib_from_index(1 /* the_uchar */, cinfo.drivername);
      if (type_expected == 2) {
	the_uchar = dbi_result_get_uchar(result, "the_uchar");
      }
      else { /* does not support one-byte uchar type */
	the_uchar = dbi_result_get_ushort(result, "the_uchar");
      }

      assert_equal(the_uchar, expect_ulonglong_from_name("the_uchar", cinfo.drivername));
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_short() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_short from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      short the_short = 0;

      the_short = dbi_result_get_short(result, "the_short");

      assert_equal(the_short, expect_longlong_from_name("the_short", cinfo.drivername));
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_ushort() {
   const char *errmsg;
   int errnum;
   dbi_result result;

   QUERY_ASSERT_RESULT(result, "SELECT the_ushort from test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned short the_ushort = 0;

      the_ushort = dbi_result_get_ushort(result, "the_ushort");

      assert_equal(the_ushort, expect_longlong_from_name("the_ushort", cinfo.drivername));
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_int() {
   dbi_result result;
   const char *errmsg;
   int errnum;

   QUERY_ASSERT_RESULT(result, "SELECT the_long from test_datatypes");

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int the_int = 0;

      the_int = dbi_result_get_int(result, "the_long");

      assert_equal(the_int, expect_longlong_from_name("the_long", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_uint() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_ulong from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int the_uint = 0;

      the_uint = dbi_result_get_uint(result, "the_ulong");

      assert_equal(the_uint, expect_ulonglong_from_name("the_ulong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_longlong() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_longlong from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      long long the_longlong = 0;

      the_longlong = dbi_result_get_longlong(result, "the_longlong");

      assert_equal(the_longlong, expect_longlong_from_name("the_longlong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_ulonglong() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_ulonglong from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned long long the_ulonglong = 0;

      the_ulonglong = dbi_result_get_ulonglong(result, "the_ulonglong");

      assert_equal(the_ulonglong, expect_ulonglong_from_name("the_ulonglong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_float() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_float from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      float the_float = 0;

      the_float = dbi_result_get_float(result, "the_float");

      assert_equal(the_float, expect_double_from_name("the_float", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_double() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_double from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      double the_double = 0;

      the_double = dbi_result_get_double(result, "the_double");

      assert_equal(the_double, expect_double_from_name("the_double", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_string() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_conn_quoted_string, the_conn_escaped_string,"
         "the_null_string, the_empty_string from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const char* the_quoted_string;
      const char* the_escaped_string;
      const char* the_null_string;
      const char* the_empty_string;

      the_quoted_string = dbi_result_get_string(result, "the_conn_quoted_string");
      assert_string_equal(the_quoted_string, expect_as_string_from_name("the_conn_quoted_string", cinfo.drivername));

      the_escaped_string = dbi_result_get_string(result, "the_conn_escaped_string");
      assert_string_equal(the_escaped_string, expect_as_string_from_name("the_conn_escaped_string", cinfo.drivername));

      the_null_string = dbi_result_get_string(result, "the_null_string");
      assert_string_equal(the_null_string, expect_string_from_name("the_null_string", cinfo.drivername));

      the_empty_string = dbi_result_get_string(result, "the_empty_string");
      assert_string_equal(the_empty_string, expect_as_string_from_name("the_empty_string", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_string_copy() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_conn_quoted_string_copy, the_conn_escaped_string_copy"
         " from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const char* the_quoted_string_copy;
      const char* the_escaped_string_copy;

      the_quoted_string_copy = dbi_result_get_string_copy(result, "the_conn_quoted_string_copy");
      assert_string_equal(the_quoted_string_copy, expect_as_string_from_name("the_conn_quoted_string_copy", cinfo.drivername));

      the_escaped_string_copy = dbi_result_get_string_copy(result, "the_conn_escaped_string_copy");
      assert_string_equal(the_escaped_string_copy, expect_as_string_from_name("the_conn_escaped_string_copy", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_binary() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_binary_quoted_string from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const unsigned char* the_binary;

      the_binary = dbi_result_get_binary(result, "the_binary_quoted_string");

      assert_equal(65, the_binary[0]);
      assert_equal(66, the_binary[1]);
      assert_equal(0,  the_binary[2]);
      assert_equal(67, the_binary[3]);
      assert_equal(39, the_binary[4]);
      assert_equal(68, the_binary[5]);
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_binary_copy() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_binary_escaped_string from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned char* the_binary_copy = NULL;

      the_binary_copy = dbi_result_get_binary_copy(result, "the_binary_escaped_string");

      assert_equal(65, the_binary_copy[0]);
      assert_equal(66, the_binary_copy[1]);
      assert_equal(0, the_binary_copy[2]);
      assert_equal(67, the_binary_copy[3]);
      assert_equal(39, the_binary_copy[4]);
      assert_equal(68, the_binary_copy[5]);

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_datetime() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_datetime, the_date, the_time from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      time_t the_datetime = 0;
      time_t the_date = 0;
      time_t the_time = 0;

      the_datetime = dbi_result_get_datetime(result, "the_datetime");
      assert_equal( the_datetime, expect_longlong_from_name("the_datetime", cinfo.drivername));

      if(!strcmp(cinfo.drivername, "msql")) {
         the_date = dbi_result_get_string(result, "the_date");
         assert_string_equal(the_date, "11-jul-1977");

         the_time = dbi_result_get_string(result, "the_time");
         assert_string_equal(the_time, "23:59:59");
      }
      else {
         the_date = dbi_result_get_datetime(result, "the_date");
         assert_equal(the_date, expect_longlong_from_name("the_date", cinfo.drivername));

         the_time = dbi_result_get_datetime(result, "the_time");
         assert_equal(the_time, expect_longlong_from_name("the_time", cinfo.drivername));
      }
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_datetime_tz() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_datetime_tz from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      time_t the_datetime_tz = 0;

      the_datetime_tz = dbi_result_get_datetime(result, "the_datetime_tz");

      assert_equal( the_datetime_tz, expect_longlong_from_name("the_datetime_tz", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_datetime_time_tz() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_datetime_tz, the_time_tz from test_datatypes");
   ASSERT_RESULT


   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      time_t the_datetime_tz = 0;
      time_t the_time_dt_tz = 0;

      the_datetime_tz = dbi_result_get_datetime(result, "the_datetime_tz");
      the_time_dt_tz = dbi_result_get_datetime(result, "the_time_tz");

      assert_equal( the_datetime_tz, expect_longlong_from_name("the_datetime_tz", cinfo.drivername));
      assert_equal( the_time_dt_tz, expect_longlong_from_name("the_time_tz", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_field_type_mismatch() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_long from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      dbi_error_flag errflag;

      dbi_result_get_string(result, "the_long");
      errflag = dbi_conn_error(dbi_result_get_conn(result), &errmsg);
      assert_equal( errflag, DBI_ERROR_BADTYPE);

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_field_bad_name() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_nonexistent from test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      dbi_error_flag errflag;

      dbi_result_get_string(result, "the_nonexistent");
      errflag = dbi_conn_error(dbi_result_get_conn(result), &errmsg);
      assert_equal( errflag, DBI_ERROR_BADNAME);

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_fields() {
   dbi_result result;
   char *query_string = NULL;
   int numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int num;
      signed char the_char = 0;
      unsigned char the_uchar = 0;
      signed short the_short = 0;
      unsigned short the_ushort = 0;
      signed int the_long = 0;
      unsigned int the_ulong = 0;
      signed long long the_longlong = 0;
      unsigned long long the_ulonglong = 0;
      float the_float = 0;
      double the_double = 0;
      const char* the_conn_quoted_string = NULL;
      unsigned short type_expected = 0;

      type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);

      num = dbi_result_get_fields(result,
				  "the_char.%c the_uchar.%uc "
				  "the_short.%h the_ushort.%uh "
				  "the_long.%l the_ulong.%ul "
				  "the_longlong.%L the_ulonglong.%uL "
				  "the_float.%f the_double.%d "
				  "the_conn_quoted_string.%s",
				  &the_char, &the_uchar,
				  &the_short, &the_ushort,
				  &the_long, &the_ulong,
				  &the_longlong, &the_ulonglong,
				  &the_float, &the_double,
				  &the_conn_quoted_string);


      if (type_expected == 2) { /* skip test for db engines which do not support a true one-byte char type */
	assert_equal(the_char, expect_longlong_from_name("the_char", cinfo.drivername));
	assert_equal(the_uchar, expect_ulonglong_from_name("the_uchar", cinfo.drivername));
      }

      assert_equal(the_short, expect_longlong_from_name("the_short", cinfo.drivername));
      assert_equal(the_ushort, expect_ulonglong_from_name("the_ushort", cinfo.drivername));

      assert_equal(the_long, expect_longlong_from_name("the_long", cinfo.drivername));
      assert_equal(the_ulong, expect_ulonglong_from_name("the_ulong", cinfo.drivername));

      assert_equal(the_longlong, expect_longlong_from_name("the_longlong", cinfo.drivername));
      assert_equal(the_ulonglong, expect_ulonglong_from_name("the_ulonglong", cinfo.drivername));

      assert_double_equal(the_float, expect_double_from_name("the_float", cinfo.drivername));

      assert_double_equal(the_double, expect_double_from_name("the_double", cinfo.drivername));

      assert_string_equal(the_conn_quoted_string,  expect_string_from_name("the_conn_quoted_string", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_bind_char() {
   dbi_result result;
   signed char the_char;
   int bind;
   unsigned short type_expected = 0;

   type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);

   if (type_expected == 2) { /* skip test for db engines which do not support a true one-byte char type */
     result = dbi_conn_query(conn, "SELECT the_char from test_datatypes");
     ASSERT_RESULT

       bind = dbi_result_bind_char(result, "the_char", &the_char);
     assert_equal(bind, 0);

     while (dbi_result_next_row(result)) {
       errmsg = NULL;

       assert_equal(the_char, expect_longlong_from_name("the_char", cinfo.drivername));

     }

     dbi_result_free(result);
   }
}

Ensure test_dbi_result_bind_uchar() {
   dbi_result result;
   unsigned char the_uchar;
   int bind;
   unsigned short type_expected = 0;

   type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);

   if (type_expected == 2) { /* skip test for db engines which do not support a true one-byte char type */
     result = dbi_conn_query(conn, "SELECT the_uchar from test_datatypes");
     ASSERT_RESULT

       bind = dbi_result_bind_uchar(result, "the_uchar", &the_uchar);
     assert_equal(bind, 0);

     while (dbi_result_next_row(result)) {
       errmsg = NULL;

       assert_equal(the_uchar, expect_ulonglong_from_name("the_uchar", cinfo.drivername));

     }

     dbi_result_free(result);
   }
}

Ensure test_dbi_result_bind_short() {
   dbi_result result;
   short the_short;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_short from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_short(result, "the_short", &the_short);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_short, expect_longlong_from_name("the_short", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_ushort() {
   dbi_result result;
   unsigned short the_ushort;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_ushort from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_ushort(result, "the_ushort", &the_ushort);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_ushort, expect_ulonglong_from_name("the_ushort", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_int() {
   dbi_result result;
   int the_int;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_long from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_int(result, "the_long", &the_int);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_int, expect_longlong_from_name("the_long", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_uint() {
   dbi_result result;
   unsigned int the_uint;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_ulong from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_uint(result, "the_ulong", &the_uint);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_uint, expect_ulonglong_from_name("the_ulong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_longlong() {
   dbi_result result;
   long long the_longlong;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_longlong from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_longlong(result, "the_longlong", &the_longlong);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_longlong, expect_longlong_from_name("the_longlong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_ulonglong() {
   dbi_result result;
   unsigned long long the_ulonglong;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_ulonglong from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_ulonglong(result, "the_ulonglong", &the_ulonglong);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_ulonglong, expect_ulonglong_from_name("the_ulonglong", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_float() {
   dbi_result result;
   float the_float;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_float from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_float(result, "the_float", &the_float);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_double_equal(the_float, expect_double_from_name("the_float", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_double() {
   dbi_result result;
   double the_double;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_double from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_double(result, "the_double", &the_double);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_double_equal(the_double, expect_double_from_name("the_double", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_string() {
   dbi_result result;
   const char* the_quoted_string;
   const char* the_null_string;
   const char* the_escaped_string;
   const char* the_empty_string;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_null_string, the_conn_escaped_string, the_empty_string,"
         " the_conn_quoted_string from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_string(result, "the_conn_quoted_string", &the_quoted_string);
   assert_equal(bind, 0);

   bind = dbi_result_bind_string(result, "the_null_string", &the_null_string);
   assert_equal(bind, 0);

   bind = dbi_result_bind_string(result, "the_conn_escaped_string", &the_escaped_string);
   assert_equal(bind, 0);

   bind = dbi_result_bind_string(result, "the_empty_string", &the_empty_string);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_string_equal(the_quoted_string, expect_string_from_name("the_conn_quoted_string", cinfo.drivername));

      assert_string_equal(the_null_string, expect_string_from_name("the_null_string", cinfo.drivername));

      assert_string_equal(the_escaped_string, expect_string_from_name("the_conn_escaped_string", cinfo.drivername));

      assert_string_equal(the_empty_string, expect_string_from_name("the_empty_string", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_string_copy() {
   dbi_result result;
   char* the_quoted_string_copy;
   char* the_escaped_string_copy;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_conn_quoted_string_copy, the_conn_escaped_string_copy"
         " from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_string_copy(result, "the_conn_quoted_string_copy", &the_quoted_string_copy);
   assert_equal(bind, 0);

   bind = dbi_result_bind_string_copy(result, "the_conn_escaped_string_copy", &the_escaped_string_copy);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_string_equal(the_quoted_string_copy, expect_string_from_name("the_conn_quoted_string_copy", cinfo.drivername));

      assert_string_equal(the_escaped_string_copy, expect_string_from_name("the_conn_escaped_string_copy", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_binary() {
   dbi_result result;
   const unsigned char* the_quoted_binary;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_binary_quoted_string from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_binary(result, "the_binary_quoted_string", &the_quoted_binary);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(65, the_quoted_binary[0]);
      assert_equal(66, the_quoted_binary[1]);
      assert_equal(0,  the_quoted_binary[2]);
      assert_equal(67, the_quoted_binary[3]);
      assert_equal(39, the_quoted_binary[4]);
      assert_equal(68, the_quoted_binary[5]);
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_binary_copy() {
   dbi_result result;
   unsigned char* the_escaped_binary_copy;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_binary_escaped_string from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_binary_copy(result, "the_binary_escaped_string", &the_escaped_binary_copy);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(65, the_escaped_binary_copy[0]);
      assert_equal(66, the_escaped_binary_copy[1]);
      assert_equal(0,  the_escaped_binary_copy[2]);
      assert_equal(67, the_escaped_binary_copy[3]);
      assert_equal(39, the_escaped_binary_copy[4]);
      assert_equal(68, the_escaped_binary_copy[5]);
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_datetime() {
   dbi_result result;
   time_t the_datetime = 0;
   time_t the_date_dt = 0;
   time_t the_time_dt = 0;
   const char *the_date;
   const char *the_time;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_datetime, the_date, the_time from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_datetime(result, "the_datetime", &the_datetime);
   assert_equal(bind, 0);

   bind = dbi_result_bind_datetime(result, "the_date", &the_date_dt);
   assert_equal(bind, 0);
   bind = dbi_result_bind_datetime(result, "the_time", &the_time_dt);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_datetime, expect_longlong_from_name("the_datetime", cinfo.drivername));

      if(!strcmp(cinfo.drivername, "msql")) {

         assert_string_equal(the_date, "11-jul-1977");

         assert_string_equal(the_time, "23:59:59");
      }
      else {

         assert_equal(the_date_dt, expect_longlong_from_name("the_date", cinfo.drivername));

         assert_equal(the_time_dt, expect_longlong_from_name("the_time", cinfo.drivername));
      }
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_bind_datetime_tz() {
   dbi_result result;
   time_t the_datetime_tz = 0;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_datetime_tz from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_datetime(result, "the_datetime_tz", &the_datetime_tz);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_datetime_tz, expect_longlong_from_name("the_datetime_tz", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_bind_datetime_time_tz() {
   dbi_result result;
   time_t the_datetime_tz = 0;
   time_t the_time_dt_tz = 0;
   int bind;

   result = dbi_conn_query(conn, "SELECT the_datetime_tz, the_time_tz from test_datatypes");
   ASSERT_RESULT

   bind = dbi_result_bind_datetime(result, "the_datetime_tz", &the_datetime_tz);
   assert_equal(bind, 0);

   bind = dbi_result_bind_datetime(result, "the_time_tz", &the_time_dt_tz);
   assert_equal(bind, 0);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;

      assert_equal(the_datetime_tz, expect_longlong_from_name("the_datetime_tz", cinfo.drivername));
      assert_equal(the_time_dt_tz, expect_longlong_from_name("the_time_tz", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_bind_fields() {
   dbi_result result;
   char *query_string;
   int bind;
   int numfields;
   unsigned int num;
   signed char the_char = 0;
   unsigned char the_uchar = 0;
   signed short the_short = 0;
   unsigned short the_ushort = 0;
   signed int the_long = 0;
   unsigned int the_ulong = 0;
   signed long long the_longlong = 0;
   unsigned long long the_ulonglong = 0;
   float the_float = 0;
   double the_double = 0;
   const char* the_conn_quoted_string = NULL;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);

   ASSERT_RESULT

   free(query_string);

   bind = dbi_result_bind_fields(result, "the_char.%c the_uchar.%uc the_short.%h the_ushort.%uh the_long.%l the_ulong.%ul "
         "the_longlong.%L the_ulonglong.%uL the_float.%f the_double.%d the_conn_quoted_string.%s",
         &the_char, &the_uchar, &the_short, &the_ushort, &the_long, &the_ulong, &the_longlong, &the_ulonglong,
         &the_float, &the_double, &the_conn_quoted_string);
   assert_equal(bind, 11);

   while (dbi_result_next_row(result)) {
     unsigned short type_expected = 0;

     type_expected = field_attrib_from_index(0 /* the_char */, cinfo.drivername);

     if (type_expected == 2) { /* skip test for db engines which do not support a true one-byte char type */
       assert_equal(the_char, expect_longlong_from_name("the_char", cinfo.drivername));

       assert_equal(the_uchar, expect_ulonglong_from_name("the_uchar", cinfo.drivername));
     }

      assert_equal(the_short, expect_longlong_from_name("the_short", cinfo.drivername));

      assert_equal(the_long, expect_longlong_from_name("the_long", cinfo.drivername));

      assert_equal(the_ulong, expect_ulonglong_from_name("the_ulong", cinfo.drivername));

      assert_equal(the_longlong, expect_longlong_from_name("the_longlong", cinfo.drivername));

      assert_equal(the_ulonglong, expect_ulonglong_from_name("the_ulonglong", cinfo.drivername));

      assert_double_equal(the_float, expect_double_from_name("the_float", cinfo.drivername));

      assert_string_equal(the_conn_quoted_string,  expect_as_string_from_name("the_conn_quoted_string", cinfo.drivername));

   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_field_length() {
   dbi_result result;
   char *query_string;
   int numfields;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);

   ASSERT_RESULT

   free(query_string);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int length = 0;

      length = dbi_result_get_field_length(result, "the_conn_quoted_string");
      assert_equal(length, field_length_from_name("the_conn_quoted_string", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_conn_quoted_string_copy");
      assert_equal(length, field_length_from_name("the_conn_quoted_string_copy", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_conn_escaped_string");
      assert_equal(length, field_length_from_name("the_conn_escaped_string", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_conn_escaped_string_copy");
      assert_equal(length, field_length_from_name("the_conn_escaped_string_copy", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_null_string");
      assert_equal(length, field_length_from_name("the_null_string", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_numstring");
      assert_equal(length, field_length_from_name("the_numstring", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_empty_string");
      assert_equal(length, field_length_from_name("the_empty_string", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_binary_quoted_string");
      assert_equal(length, field_length_from_name("the_binary_quoted_string", cinfo.drivername));

      length = dbi_result_get_field_length(result, "the_binary_escaped_string");
      assert_equal(length, field_length_from_name("the_binary_escaped_string", cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_field_length_idx() {
   dbi_result result;
   char *query_string;
   int numfields;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      unsigned int length = 0;

      length = dbi_result_get_field_length_idx(result, 1);
      assert_equal(length, field_length_from_index(1, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 2);
      assert_equal(length, field_length_from_index(2, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 3);
      assert_equal(length, field_length_from_index(3, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 4);
      assert_equal(length, field_length_from_index(4, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 5);
      assert_equal(length, field_length_from_index(5, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 6);
      assert_equal(length, field_length_from_index(6, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 7);
      assert_equal(length, field_length_from_index(7, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 8);
      assert_equal(length, field_length_from_index(8, cinfo.drivername));

      length = dbi_result_get_field_length_idx(result, 9);
      assert_equal(length, field_length_from_index(9, cinfo.drivername));

   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_field_idx() {
   dbi_result result;
   char *query_string;
   int numfields;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);

   ASSERT_RESULT

   free(query_string);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int idx = 0;

      idx = dbi_result_get_field_idx(result, "the_numstring");
      assert_equal(idx, field_index_from_name("the_numstring", cinfo.drivername));
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_field_name() {
   dbi_result result;
   char *query_string;
   int numfields;

   query_string = assemble_query_string(cinfo.drivername, &numfields);

   result = dbi_conn_query(conn, query_string);

   ASSERT_RESULT

   free(query_string);

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      const char *name;

      name = dbi_result_get_field_name(result, 15);

      if(!strcmp(cinfo.drivername, "db2")) {
         assert_string_equal(name, "THE_NUMSTRING");
      }
      else if (!strcmp(cinfo.drivername, "firebird")) {
         /* firebird uses fewer fields, hence field 15 is something else */
         assert_string_equal(name, "THE_NULL_STRING");
      }
      else {
         assert_string_equal(name, "the_numstring");
      }
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_numfields() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_conn_quoted_string, the_conn_quoted_string_copy,"
         " the_conn_escaped_string, the_conn_escaped_string_copy, the_numstring,"
         " the_empty_string, the_null_string, the_binary_quoted_string, the_binary_escaped_string"
         " from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int num;

      num = dbi_result_get_numfields(result);
      assert_equal(num, 9);
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_field_type() {
   dbi_result result;
   char *query_string = NULL;
   int i, numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);
   free(query_string);
   query_string = NULL;

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned short type_found, type_expected;

      for (i = 1; i <= numfields; i++) {
         type_found = dbi_result_get_field_type(result, field_name_from_index(i, cinfo.drivername));
         type_expected = field_type_from_index(i, cinfo.drivername);
         assert_equal_with_message(type_found, type_expected, "[%d] should match [%d] for field name [%s]", type_found, type_expected, field_name_from_index(i, cinfo.drivername));
      }
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_field_type_idx() {
   dbi_result result;
   char *query_string = NULL;
   int i, numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);
   free(query_string);
   query_string = NULL;

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned short type_found, type_expected;

      for (i = 1; i <= numfields; i++) {
         type_found = dbi_result_get_field_type_idx(result, i);
         type_expected = field_type_from_index(i, cinfo.drivername);
         assert_equal_with_message(type_found, type_expected, "[%d] should match [%d] for field index [%d]", type_found, type_expected, i);
      }
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_field_attrib() {
   dbi_result result;
   char *query_string = NULL;
   int i, numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);
   free(query_string);
   query_string = NULL;

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int type_found, type_expected;

      for (i = 1; i <= numfields; i++) {
         type_found = dbi_result_get_field_attrib(result, field_name_from_index(i, cinfo.drivername), DBI_INTEGER_UNSIGNED, DBI_INTEGER_SIZE8);
         type_expected = field_attrib_from_index(i, cinfo.drivername);
         assert_equal_with_message(type_found, type_expected, "[%d] should match [%d] for field name [%s]", type_found, type_expected, field_name_from_index(i, cinfo.drivername));
      }
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_field_attrib_idx() {
   dbi_result result;
   char *query_string = NULL;
   int i, numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);
   free(query_string);
   query_string = NULL;

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int type_found, type_expected;

      for (i = 1; i <= numfields; i++) {
         type_found = dbi_result_get_field_attrib_idx(result, i, DBI_INTEGER_UNSIGNED, DBI_INTEGER_SIZE8);
         type_expected = field_attrib_from_index(i, cinfo.drivername);
         assert_equal_with_message(type_found, type_expected, "[%d] should match [%d] for field index [%d]", type_found, type_expected, i);
      }
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_get_field_attribs() {
   dbi_result result;
   char *query_string = NULL;
   int i, numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);
   free(query_string);
   query_string = NULL;

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int type_found, type_expected;

      for (i = 1; i <= numfields; i++) {
         type_found = dbi_result_get_field_attribs(result, field_name_from_index(i, cinfo.drivername));
         type_expected = field_attrib_from_index(i, cinfo.drivername);
         assert_equal_with_message(type_found, type_expected, "[%d] should match [%d] for field name [%s]", type_found, type_expected, field_name_from_index(i, cinfo.drivername));
      }
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_get_field_attribs_idx() {
   dbi_result result;
   char *query_string = NULL;
   int i, numfields;

   if ((query_string = assemble_query_string(cinfo.drivername, &numfields)) == NULL) {
      /* todo: make noise */
      return;
   }

   result = dbi_conn_query(conn, query_string);
   free(query_string);
   query_string = NULL;

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      unsigned int type_found, type_expected;

      for (i = 1; i <= numfields; i++) {
         type_found = dbi_result_get_field_attribs_idx(result, i);
         type_expected = field_attrib_from_index(i, cinfo.drivername);
         assert_equal_with_message(type_found, type_expected, "[%d] should match [%d] for field index [%d]", type_found, type_expected, i);
      }
   }

   dbi_result_free(result);
}

Ensure test_dbi_result_field_is_null() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_null_string from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int isnull = 0;

      isnull = dbi_result_field_is_null(result, "the_null_string");
      assert_equal(isnull, 1);
   }

   dbi_result_free(result);

}

Ensure test_dbi_result_field_is_null_idx() {
   dbi_result result;

   result = dbi_conn_query(conn, "SELECT the_null_string from test_datatypes");

   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      errmsg = NULL;
      int isnull = 0;

      isnull = dbi_result_field_is_null_idx(result, 1);
      assert_equal(isnull, 1);
   }

   dbi_result_free(result);

}

Ensure test_dbi_conn_get_table_list() {
   dbi_result result;

   result = dbi_conn_get_table_list(conn, cinfo.dbname, "test_datatypes");
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      const char *tablename = NULL;
      tablename = dbi_result_get_string_idx(result, 1);
      assert_string_equal("test_datatypes", tablename);
   }

   assert_equal(dbi_result_free(result), 0);
}

Ensure test_dbi_conn_get_table_list_no_pattern() {
   dbi_result result;

   result = dbi_conn_get_table_list(conn, cinfo.dbname, NULL);
   ASSERT_RESULT

   while (dbi_result_next_row(result)) {
      const char *tablename = NULL;
      tablename = dbi_result_get_string_idx(result, 1);

      assert_string_equal("test_datatypes", tablename);

   }

   assert_equal(dbi_result_free(result), 0);
}

/****** TestSuite declarations ******/

TestSuite *test_libdbi() {

   TestSuite *suite = create_named_test_suite(cinfo.drivername);

   add_suite(suite, test_dbi_general_test_case());

   add_suite(suite, test_database_infrastructure());
   add_suite(suite, test_managing_results());
   add_suite(suite, test_transactions());

   add_suite(suite, test_managing_queries());
   add_suite(suite, test_dbi_retrieving_fields_meta_data());

   add_suite(suite, test_dbi_retrieving_fields_data_name());
   add_suite(suite, test_dbi_retrieving_fields_data_idx());
   add_suite(suite, test_dbi_retrieving_fields_as());
   add_suite(suite, test_dbi_misc());

   /* todo: Specific tests for the databases functions */
   if (!strcmp(cinfo.drivername, "firebird")) {

   } else if (!strcmp(cinfo.drivername, "freetds")) {

   } else if (!strcmp(cinfo.drivername, "ingres")) {

   } else if (!strcmp(cinfo.drivername, "msql")) {

   } else if (!strcmp(cinfo.drivername, "mysql")) {
      /* add_suite(suite, test_mysql_specific_functions()); */
   } else if (!strcmp(cinfo.drivername, "pgsql")) {
      /* add_suite(suite, test_pgsql_specific_functions()); */
   } else if (!strcmp(cinfo.drivername, "sqlite") || !strcmp(cinfo.drivername,
         "sqlite3")) {

   }

   TestSuite *database_fixture = create_named_test_suite("libdbi framework test");
   add_suite(database_fixture, suite);
   setup(database_fixture, create_database);
   teardown(database_fixture, drop_database);
   return database_fixture;
}

TestSuite *test_pgsql_specific_functions() {
   TestSuite *suite = create_named_test_suite("Postgresql specific functions");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_pgsql_copy);
   connection_fixture(suite);
}

TestSuite *test_mysql_specific_functions() {
   TestSuite *suite = create_named_test_suite("Mysql specific functions");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   //add_test(suite, test_dbi_conn_escape_string);
   connection_fixture(suite);
}

TestSuite *test_dbi_instance_infrastructure() {
   TestSuite *suite = create_named_test_suite("Instance infrastructure fields as");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_create_another_connection);
   return connection_fixture(suite);
}

TestSuite *test_dbi_general_test_case() {
   TestSuite *suite = create_named_test_suite("Test DBI general test cases");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_create_another_connection);
   if (!strcmp(cinfo.drivername, "mysql") ||
         !strcmp(cinfo.drivername, "pgsql")) {
      add_test(suite, test_another_encoding);
   }
   return connection_fixture(suite);
}

TestSuite *test_managing_queries() {
   TestSuite *suite = create_named_test_suite("Managing Queries");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_conn_query);
   add_test(suite, test_dbi_conn_queryf);
   add_test(suite, test_dbi_conn_sequence_last);
   add_test(suite, test_dbi_conn_sequence_next);
   add_test(suite, test_dbi_conn_quote_string);
   add_test(suite, test_dbi_conn_quote_string_copy);
   add_test(suite, test_dbi_conn_quote_binary_copy);
   add_test(suite, test_dbi_conn_escape_string);
   add_test(suite, test_dbi_conn_escape_string_copy);
   add_test(suite, test_dbi_conn_escape_binary_copy);
   connection_fixture(suite);
}

TestSuite *test_transactions() {
   TestSuite *suite = create_named_test_suite("Managing Transactions");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_conn_transaction_commit);
   add_test(suite, test_dbi_conn_transaction_rollback);
   add_test(suite, test_dbi_conn_rollback_to_savepoint);
   add_test(suite, test_dbi_conn_release_savepoint);
   connection_fixture(suite);
}

TestSuite *test_managing_results() {
   TestSuite *suite = create_named_test_suite("Managing results");
   // set the number of rows we need, 5 rows is good.
   // call function to setup the number of rows
   setup(suite, create_schema_five_rows);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_result_get_conn);
   add_test(suite, test_dbi_result_free);
   add_test(suite, test_dbi_result_seek_row);
   add_test(suite, test_dbi_result_first_row);
   add_test(suite, test_dbi_result_last_row);
   add_test(suite, test_dbi_result_prev_row);
   add_test(suite, test_dbi_result_next_row);
   add_test(suite, test_dbi_result_get_currow);
   add_test(suite, test_dbi_result_get_numrows);
   add_test(suite, test_dbi_result_get_numrows_affected);
   return connection_fixture(suite);
}

TestSuite *test_dbi_misc() {
   TestSuite *suite = create_named_test_suite("Test select cases");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_retrieve_zero_rows);
   add_test(suite, test_dummy);
   return connection_fixture(suite);
}

TestSuite *test_dbi_retrieving_fields_as() {
   TestSuite *suite = create_named_test_suite("Retrieving fields as");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_as_string);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_as_longlong);
   return connection_fixture(suite);
}

TestSuite *test_dbi_retrieving_fields_data_idx() {
   TestSuite *suite = create_named_test_suite("Retrieving fields data by index");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_result_get_char_idx);
   add_test(suite, test_dbi_result_get_uchar_idx);
   add_test(suite, test_dbi_result_get_short_idx);
   add_test(suite, test_dbi_result_get_ushort_idx);
   add_test(suite, test_dbi_result_get_int_idx);
   add_test(suite, test_dbi_result_get_uint_idx);
   if (tinfo.have_longlong) {
      add_test(suite, test_dbi_result_get_longlong_idx);
   }
   if (tinfo.have_ulonglong) {
      add_test(suite, test_dbi_result_get_ulonglong_idx);
   }
   add_test(suite, test_dbi_result_get_float_idx);
   if(tinfo.have_double) {
      add_test(suite, test_dbi_result_get_double_idx);
   }
   add_test(suite, test_dbi_result_get_string_idx);
   add_test(suite, test_dbi_result_get_string_copy_idx);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_binary_idx);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_binary_copy_idx);
   if(tinfo.have_datetime) {
      add_test(suite, test_dbi_result_get_datetime_idx);
   }
   if(tinfo.have_datetime_tz) {
      add_test(suite, test_dbi_result_get_datetime_tz_idx);
   }
   if (tinfo.have_time_tz) {
      add_test(suite, test_dbi_result_get_datetime_time_tz_idx);
   }
   connection_fixture(suite);
}

TestSuite *test_dbi_retrieving_fields_data_name() {
   TestSuite *suite = create_named_test_suite("Retrieving fields data by name");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_result_get_char);
   add_test(suite, test_dbi_result_get_uchar);
   add_test(suite, test_dbi_result_get_short);
   add_test(suite, test_dbi_result_get_ushort);
   add_test(suite, test_dbi_result_get_int);
   add_test(suite, test_dbi_result_get_uint);
   if (tinfo.have_longlong) {
      add_test(suite, test_dbi_result_get_longlong);
   }
   if (tinfo.have_ulonglong) {
      add_test(suite, test_dbi_result_get_ulonglong);
   }
   add_test(suite, test_dbi_result_get_float);
   if(tinfo.have_double) {
      add_test(suite, test_dbi_result_get_double);
   }
   add_test(suite, test_dbi_result_get_string);
   add_test(suite, test_dbi_result_get_string_copy);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_binary);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_binary_copy);
   if(tinfo.have_datetime) {
      add_test(suite, test_dbi_result_get_datetime);
   }
   if(tinfo.have_datetime_tz) {
      add_test(suite, test_dbi_result_get_datetime_tz);
   }
   if (tinfo.have_time_tz) {
      add_test(suite, test_dbi_result_get_datetime_time_tz);
   }
   add_test(suite, test_dbi_result_get_field_type_mismatch);
   //add_test(suite, test_dbi_result_get_field_bad_name);
   // todo bug firebird
   add_test(suite, test_dbi_result_get_fields);
   add_test(suite, test_dbi_result_bind_char);
   add_test(suite, test_dbi_result_bind_uchar);
   add_test(suite, test_dbi_result_bind_short);
   add_test(suite, test_dbi_result_bind_ushort);
   add_test(suite, test_dbi_result_bind_int);
   add_test(suite, test_dbi_result_bind_uint);
   if (tinfo.have_longlong) {
      add_test(suite, test_dbi_result_bind_longlong);
   }
   if (tinfo.have_ulonglong) {
      add_test(suite, test_dbi_result_bind_ulonglong);
   }
   add_test(suite, test_dbi_result_bind_float);
   if(tinfo.have_double) {
      add_test(suite, test_dbi_result_bind_double);
   }
   add_test(suite, test_dbi_result_bind_string);
   // todo bug firebird
   add_test(suite, test_dbi_result_bind_binary);
   add_test(suite, test_dbi_result_bind_string_copy);
   // todo bug firebird
   add_test(suite, test_dbi_result_bind_binary_copy);
   if(tinfo.have_datetime) {
      add_test(suite, test_dbi_result_bind_datetime);
   }
   if(tinfo.have_datetime_tz) {
      add_test(suite, test_dbi_result_bind_datetime_tz);
   }
   if (tinfo.have_time_tz) {
      add_test(suite, test_dbi_result_bind_datetime_time_tz);
   }
   // todo bug firebird
   add_test(suite, test_dbi_result_bind_fields);

   connection_fixture(suite);
}

TestSuite *test_dbi_retrieving_fields_meta_data() {
   TestSuite *suite = create_named_test_suite("Retrieving fields meta-data");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_result_get_field_length);
   add_test(suite, test_dbi_result_get_field_length_idx);
   add_test(suite, test_dbi_result_get_field_idx);
   add_test(suite, test_dbi_result_get_field_name);
   add_test(suite, test_dbi_result_get_numfields);
   add_test(suite, test_dbi_result_get_field_type);
   add_test(suite, test_dbi_result_get_field_type_idx);
   add_test(suite, test_dbi_result_get_field_attrib);
   add_test(suite, test_dbi_result_get_field_attrib_idx);
   add_test(suite, test_dbi_result_get_field_attribs);
   add_test(suite, test_dbi_result_get_field_attribs_idx);
   add_test(suite, test_dbi_result_field_is_null);
   add_test(suite, test_dbi_result_field_is_null_idx);
   return connection_fixture(suite);
}

TestSuite *test_database_infrastructure() {
   TestSuite *suite = create_named_test_suite("Database Infrastructure");
   setup(suite, create_schema);
   teardown(suite, drop_schema);
   add_test(suite, test_dbi_conn_get_table_list);
   //  add_test(suite, test_dbi_conn_get_table_list_no_pattern);
   /* the test test_dbi_conn_select_db need to be called by last because
    * some database engines reconnect to the database. So the fixture
    * below close the database for us.
    */
   add_test(suite, test_dbi_conn_select_db);
   connection_fixture(suite);
}

/* helper TestSuite */

TestSuite *connection_fixture(TestSuite *suite) {
   TestSuite *fixture = create_named_test_suite("libdbi connection");
   add_suite(fixture, suite);
   setup(fixture, open_test_database);
   teardown(fixture, close_test_database);
   return fixture;
}
