#define _POSIX_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

//Compile string: gcc `pkg-config --cflags glib-2.0` `pkg-config --libs glib-2.0`  name_to_uid.c -o name_to_uid

//sudo apt-get install libglib2.0-dev

uid_t name_to_uid(char const *name) {
	static GHashTable *cache = 0;

	if (!cache) {
		cache=g_hash_table_new(g_str_hash, g_str_equal);
	}

	uid_t *uid = (uid_t*) g_hash_table_lookup(cache,name);
	if (uid) {
		return *uid;
	} else {
		struct passwd* p = getpwnam(name);
		uid_t *uid_malloc = malloc(sizeof(uid_t));
		if (p) {
			*uid_malloc = p->pw_uid;
		} else {
			csync_fatal("Could not lookup uid for user name %s\n",name);
		}
		char *name_malloc = malloc(strlen(name) + 1);
		strcpy(name_malloc,name);
		g_hash_table_insert(cache,name_malloc,uid_malloc);
		return *uid_malloc;
	}
}

char *uid_to_name(uid_t uid) {
	static GHashTable *cache = 0;

	if (!cache) {
		cache=g_hash_table_new(g_int_hash, g_int_equal);
	}

	char *name = (char*) g_hash_table_lookup(cache,&uid);
	if (name) {
		return name;
	} else {
		struct passwd* p = getpwuid(uid);
		uid_t *uid_malloc = malloc(sizeof(uid_t));
		*uid_malloc = uid;
		if (p) {
			name = p->pw_name;
		} else {
			csync_fatal("Could not lookup user name for uid %d\n",(int)uid);			
		}
			
		char *name_malloc = malloc(strlen(name) + 1);
		strcpy(name_malloc,name);
	
		g_hash_table_insert(cache,uid_malloc,name_malloc);
		return name_malloc;
	}
}

gid_t group_to_gid(char const *group) {
	static GHashTable *cache = 0;

	if (!cache) {
		cache=g_hash_table_new(g_str_hash, g_str_equal);
	}

	gid_t *gid = (gid_t*) g_hash_table_lookup(cache,group);
	if (gid) {
		return *gid;
	} else {
		struct group* p = getgrnam(group);
		gid_t *gid_malloc = malloc(sizeof(gid_t));
		if (p) {
			*gid_malloc = p->gr_gid;
		} else {
			csync_fatal("Could not lookup gid for group name %s\n",group);
		}
		char *group_malloc = malloc(strlen(group) + 1);
		strcpy(group_malloc,group);
		g_hash_table_insert(cache,group_malloc,gid_malloc);
		return *gid_malloc;
	}
}

char *gid_to_group(gid_t gid) {
	static GHashTable *cache = 0;

	if (!cache) {
		cache=g_hash_table_new(g_int_hash, g_int_equal);
	}

	char *name = (char*) g_hash_table_lookup(cache,&gid);
	if (name) {
		return name;
	} else {
		struct group* p = getgrgid(gid);
		gid_t *gid_malloc = malloc(sizeof(gid_t));
		*gid_malloc = gid;
		if (p) {
			name = p->gr_name;
		} else {
			csync_fatal("Could not lookup group name for gid %d\n",(int)gid);
		}
			
		char *name_malloc = malloc(strlen(name) + 1);
		strcpy(name_malloc,name);
	
		g_hash_table_insert(cache,gid_malloc,name_malloc);
		return name_malloc;
	}
}

/*
void main(int argc, char **argv)
{
  printf("name_to_uid %i\n", name_to_uid(argv[1]));
  printf("name_to_uid %i\n", name_to_uid(argv[1]));
  printf("uid_to_name %s\n", uid_to_name(atoi(argv[1])));
  printf("uid_to_name %s\n", uid_to_name(atoi(argv[1])));
  printf("group_to_gid %i\n", group_to_gid(argv[1]));
  printf("group_to_gid %i\n", group_to_gid(argv[1]));
  printf("gid_to_group %s\n", gid_to_group(atoi(argv[1])));
  printf("gid_to_group %s\n", gid_to_group(atoi(argv[1])));
}
*/
