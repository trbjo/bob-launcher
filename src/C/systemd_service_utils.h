#ifndef SYSTEMD_SERVICE_UTILS_H
#define SYSTEMD_SERVICE_UTILS_H

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>
#include <stddef.h>

int connect_abstract_blocking_with_timeout(const char *socket_name, size_t socket_name_len, int timeout_seconds);

int create_sync_socket(const char* sync_socket_name, size_t socket_name_len);
int wait_for_ready_signal(int sync_sock);
void signal_ready_if_needed(const char* sync_socket_name, size_t sync_socket_name_len);

char **create_env_array(const char *first_var, ...);
void free_env_array(char **env_array);

#endif /* SYSTEMD_SERVICE_UTILS_H */
