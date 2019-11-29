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
 * dbd_oracle.h: Oracle database support
 * Copyright (C) 2003-2004, Christian M. Stamgren <christian@stamgren.com>
 * http://libdbi-drivers.sourceforge.net
 *
 */

#ifndef __DBD_ORACLE_H__
#define __DBD_ORACLE_H__
 
typedef struct Oraconn_ 
{
	OCIEnv           *env;
	OCIError         *err;
	OCISvcCtx        *svc;

} Oraconn;


#define ORACLE_RESERVED_WORDS { \
  "ALL", \
  "ALTER", \
  "AND", \
  "ANY", \
  "ARRAY", \
  "AS", \
  "ASC", \
  "AT", \
  "AUTHID", \
  "AVG", \
  "BEGIN", \
  "BETWEEN", \
  "BINARY_INTEGER", \
  "BODY", \
  "BOOLEAN", \
  "BULK", \
  "BY", \
  "CASE", \
  "CHAR", \
  "CHAR_BASE", \
  "CHECK", \
  "CLOSE", \
  "CLUSTER", \
  "COALESCE", \
  "COLLECT", \
  "COMMENT", \
  "COMMIT", \
  "COMPRESS", \
  "CONNECT", \
  "CONSTANT", \
  "CREATE", \
  "CURRENT", \
  "CURRVAL", \
  "CURSOR", \
  "DATE", \
  "DAY", \
  "DECLARE", \
  "DECIMAL", \
  "DEFAULT", \
  "DELETE", \
  "DESC", \
  "DISTINCT", \
  "DO", \
  "DROP", \
  "ELSE", \
  "ELSIF", \
  "END", \
  "EXCEPTION", \
  "EXCLUSIVE", \
  "EXECUTE", \
  "EXISTS", \
  "EXIT", \
  "EXTENDS", \
  "EXTRACT", \
  "FALSE", \
  "FETCH", \
  "FLOAT", \
  "FOR", \
  "FORALL", \
  "FROM", \
  "FUNCTION", \
  "GOTO", \
  "GROUP", \
  "HAVING", \
  "HEAP", \
  "HOUR", \
  "IF", \
  "IMMEDIATE", \
  "IN", \
  "INDEX", \
  "INDICATOR", \
  "INSERT", \
  "INTEGER", \
  "INTERFACE", \
  "INTERSECT", \
  "INTERVAL", \
  "INTO", \
  "IS", \
  "ISOLATION", \
  "JAVA", \
  "LEVEL", \
  "LIKE", \
  "LIMITED", \
  "LOCK", \
  "LONG", \
  "LOOP", \
  "MAX", \
  "MIN", \
  "MINUS", \
  "MINUTE", \
  "MLSLABEL", \
  "MOD", \
  "MODE", \
  "MONTH", \
  "NATURAL", \
  "NATURALN", \
  "NEW", \
  "NEXTVAL", \
  "NOCOPY", \
  "NOT", \
  "NOWAIT", \
  "NULL", \
  "NULLIF", \
  "NUMBER", \
  "NUMBER_BASE", \
  "OCIROWID", \
  "OF", \
  "ON", \
  "OPAQUE", \
  "OPEN", \
  "OPERATOR", \
  "OPTION", \
  "OR", \
  "ORDER", \
  "ORGANIZATION", \
  "OTHERS", \
  "OUT", \
  "PACKAGE", \
  "PARTITION", \
  "PLS_INTEGER", \
  "POSITIVE", \
  "POSITIVEN", \
  "PRAGMA", \
  "PRIOR", \
  "PRIVATE", \
  "PROCEDURE", \
  "PUBLIC", \
  "RAISE", \
  "RANGE", \
  "RAW", \
  "REAL", \
  "RECORD", \
  "REF", \
  "RELEASE", \
  "RETURN", \
  "REVERSE", \
  "ROLLBACK", \
  "ROW", \
  "ROWNUM", \
  "ROWTYPE", \
  "SAVEPOINT", \
  "SECOND", \
  "SELECT", \
  "SEPARATE", \
  "SET", \
  "SHARE", \
  "SMALLINT", \
  "SPACE", \
  "SQL", \
  "SQLCODE", \
  "SQLERRM", \
  "START", \
  "STDDEV", \
  "SUBTYPE", \
  "SUCCESSFUL", \
  "SUM", \
  "SYNONYM", \
  "SYSDATE", \
  "TABLE", \
  "THEN", \
  "TIME", \
  "TIMESTAMP", \
  "TIMEZONE_REGION", \
  "TIMEZONE_ABBR", \
  "TIMEZONE_MINUTE", \
  "TIMEZONE_HOUR", \
  "TO", \
  "TRIGGER", \
  "TRUE", \
  "TYPE", \
  "UI", \
  "AS", \
  "ASC", \
  "AUDIT", \
  "AUTHORIZATION", \
  "AVG", \
  "BETWEEN", \
  "ACCESS", \
  "BY", \
  "ADD", \
  "ALL", \
  "ALTER", \
  "CHAR", \
  "ANY", \
  "CHECK", \
  "DATE", \
  "CLUSTER", \
  "COLUMN", \
  "COMMENT", \
  "DECIMAL", \
  "DEFAULT", \
  "COMPRESS", \
  "DELETE", \
  "CONNECT", \
  "DESC", \
  "DISTINCT", \
  "DROP", \
  "CREATE", \
  "CURRENT", \
  "ELSE", \
  "CURSOR", \
  "ESTIMATE", \
  "GRANT", \
  "GROUP", \
  "EXCLUSIVE", \
  "EXISTS", \
  "HAVING", \
  "IDENTIFIED", \
  "IMMEDIATE", \
  "NOCOMPRESS", \
  "MAXEXTENTS", \
  "NOT", \
  "NOTFOUND", \
  "NOWAIT", \
  "NULL", \
  "NUMBER", \
  "PCTFREE", \
  "REVOKE", \
  "SMALLINT", \
  "ROW", \
  "ROWID", \
  "ROWLABEL", \
  "ROWNUM", \
  "SQLBUF", \
  "ROWS", \
  "START", \
  "STATEMENT_ID", \
   NULL }



#ifndef OCI_ATTR_CURRENT_POSITION
#define MY_OCI_ATTR_CURRENT_POSITION              164 
#define MY_OCI_STMT_SCROLLABLE_READONLY           0x08  
#define OCIStmtFetch2(stmt,err,h,orientation,i,j) OCIStmtFetch(stmt,err,i,orientation,j)
#else
#define MY_OCI_ATTR_CURRENT_POSITION              OCI_ATTR_CURRENT_POSITION
#define MY_OCI_STMT_SCROLLABLE_READONLY           OCI_STMT_SCROLLABLE_READONLY
#endif


#endif	/* __DBD_ORACLE_H__ */
