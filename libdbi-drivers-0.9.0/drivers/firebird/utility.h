#ifndef __UTILITY_H
#define __UTILITY_H

#include <dbi/dbi.h>
#include <dbi/dbi-dev.h>
#include <dbi/dbd.h>

void _get_firebird_version( void *ptr, char *version );
int _dbd_real_connect(dbi_conn_t *conn, char *enc);
char *_firebird_populate_db_string( dbi_conn_t *conn, const char *dbname, char *db_fullpath );

void _translate_firebird_type(int field_type,  unsigned short *type, unsigned int *attribs);
void _get_field_info(dbi_result_t *result);
int _get_row_data(dbi_result_t *result, dbi_row_t *row, unsigned long long rowidx);
unsigned long long return_generator_value(dbi_conn_t *conn, const char *sequence, int type);

void dealocate_iconn( ibase_conn_t *iconn );

#endif /* __UTILITY_H */
