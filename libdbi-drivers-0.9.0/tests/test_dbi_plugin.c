/*
    test_dbi_plugin  - lodable test library that uses drivers

    Copyright (C) 2010 Canonical, Ltd. All Rights Reserved
    Author: Clint Byrum <clint@ubuntu.com>

    This library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser Public License as published by
    the Free Software Foundation, either version 2.1 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser Public License for more details.

    You should have received a copy of the GNU Lesser Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <dbi/dbi.h>

int init_db(char *dir);

int init_db(char *dir)
{
    return dbi_initialize(dir);
}
