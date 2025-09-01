#ifndef SYSTEMD_SCOPE_LOWLEVEL_H
#define SYSTEMD_SCOPE_LOWLEVEL_H

#include <unistd.h>

char* create_systemd_scope_lowlevel(const char* slice_value, const char* scope_name, pid_t pid);
int get_caller_systemd_slice(char **slice);
int get_pid_systemd_slice(pid_t pid, char **slice);

#endif
