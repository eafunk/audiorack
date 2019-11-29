/*
    test_dbi_dlopen - test program for dlopening and then loading drivers

    Copyright (C) 2010 Canonical, Ltd. All Rights Reserved
    Author: Clint Byrum <clint@ubuntu.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser Public License as published by
    the Free Software Foundation, either version 2.1 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser Public License for more details.

    You should have received a copy of the GNU Lesser Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <dlfcn.h>

void usage(char *prog);

void usage(char *prog)
{
    fprintf(stderr, "usage: %s G(for global)|N(for noglobal) /path/to/lib/dbd\n",prog);
}

int main(int argc, char **argv)
{
    void *handle;
    if (argc < 3)
    {
        usage(argv[0]);
        return 1;
    }
    if (argv[1][0] == 'G')
    {
        handle = dlopen("./.libs/libtest_dbi_plugin.so", RTLD_NOW|RTLD_GLOBAL);
    }
    else if (argv[1][0] == 'N')
    {
        handle = dlopen("./.libs/libtest_dbi_plugin.so", RTLD_NOW);
    } else {
        usage(argv[0]);
        return 1;
    }
        
    if (!handle)
    {
        printf("Failed to load test_dbi_plugin.so");
        return 1;
    }

    int (*init_db)(char *);
    char *error;

    //return test_main(argc,argv);
    *(void **) (&init_db) = dlsym(handle, "init_db");

    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }

    int ndrivers = (*init_db)(argv[2]);

    if (ndrivers < 1)
    {
        fprintf(stderr, "Either you have no drivers in %s , or there was a problem loading them.\n",argv[2]);
        return 1;
    }

    fprintf(stdout,"num drivers = %d\n",  (*init_db)(argv[2]));
    return 0;
}
