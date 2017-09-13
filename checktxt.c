/*
 *  csync2 - cluster synchronization tool, 2nd generation
 *  LINBIT Information Technologies GmbH <http://www.linbit.com>
 *  Copyright (C) 2004, 2005  Clifford Wolf <clifford@clifford.at>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "csync2.h"
#include "name_group_id.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <assert.h>

/*
 * this csync_genchecktxt() function might not be nice or
 * optimal - but it is hackish and easy to read at the same
 * time....  ;-)
 */

char* strcatprintf( char* dest, char * format, ... )
{
	static char pbuf[256];
	char *src=&pbuf[0];
	
	va_list argptr;
    va_start(argptr, format);
	vsnprintf(src,256,format, argptr);
	va_end(argptr);
	
    while (*dest) dest++;
    while (*dest++ = *src++);
    return --dest;
}

int csync_checkvalue(const char *chk1, const char *chk2, const char *value) {
	char *c1 = strstr(chk1, value);
	char *c2 = strstr(chk2, value);
	
	if (c1 && c2) {
		// + 1 for the = char as we don't need to compare that
		int len = strlen(value) + 1;
		c1 += len;
		c2 += len;
		
		//Ensure all chars are equal until we find a ':' or a '0'
		while (*c1 == *c2 && *c1 != ':' && *c2 != ':' && *c1 && *c2) {
			c1++;
			c2++;
		}
		return (*c1 == *c2);
	} else {
		return 0;
	}
}

const char *csync_genchecktxt(const struct stat *st, const char *filename, int symbolic)
{
	static char buffer[4096];
	static char pbuf[256];
	char *b;
	char *elements[64];
	int elidx=0, len=1;
	int i, j, k;

	char *start= &buffer[0];
	*start = 0;

	/* version 1 of this check text */
	b = strcatprintf(start,"v1");

	if ( !S_ISLNK(st->st_mode) && !S_ISDIR(st->st_mode) )
		b = strcatprintf(b,":mtime=%Ld", (long long)st->st_mtime);

	if ( !csync_ignore_mod )
		b = strcatprintf(b,":mode=%d", (int)st->st_mode);

	if ( !csync_ignore_uid ) {
		//Also use symbolic to flag to we should compare via the text for the user name instead of the numeric id 
		if (symbolic) {
			b = strcatprintf(b,":uid=%s", uid_to_name(st->st_uid));
		} else {
			b = strcatprintf(b,":uid=%d", (int)st->st_uid);
		}
	}
	if ( !csync_ignore_gid ) {
		//Also use symbolic to flag to we should compare via the text for the group name instead of the numeric id
		if (symbolic) {
			b = strcatprintf(b,":gid=%s", gid_to_group(st->st_gid));
		} else {
			b = strcatprintf(b,":gid=%d", (int)st->st_gid);
		}
	}

	if ( S_ISREG(st->st_mode) )
		b = strcatprintf(b,":type=reg:size=%Ld", (long long)st->st_size);

	if ( S_ISDIR(st->st_mode) )
		b = strcatprintf(b,":type=dir");

	if ( S_ISCHR(st->st_mode) )
		b = strcatprintf(b,":type=chr:dev=%d", (int)st->st_rdev);

	if ( S_ISBLK(st->st_mode) )
		b = strcatprintf(b,":type=blk:dev=%d", (int)st->st_rdev);

	if ( S_ISFIFO(st->st_mode) )
		b = strcatprintf(b,":type=fifo");

	if ( S_ISLNK(st->st_mode) ) {
		char tmp[4096];
		int r = readlink(filename, tmp, 4095);
		tmp[ r >= 0 ? r : 0 ] = 0;
		b = strcatprintf(b,":type=lnk:target=%s", url_encode(tmp));
	}

	if ( S_ISSOCK(st->st_mode) )
		b = strcatprintf(b,":type=sock");

	return &buffer[0];
}

/* In future version of csync this might also convert
 * older checktxt strings to the new format.
 */
int csync_cmpchecktxt(const char *a, const char *b)
{
	return !strcmp(a, b);
}

