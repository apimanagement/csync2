#include <sys/types.h>
#include <pwd.h>
#include <grp.h>

uid_t name_to_uid(char const *name);
char *uid_to_name(uid_t uid);
gid_t group_to_gid(char const *group);
char *gid_to_group(gid_t gid);
