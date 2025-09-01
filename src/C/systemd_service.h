#ifndef SYSTEMD_SERVICE_H_UTILS
#define SYSTEMD_SERVICE_H_UTILS

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <limits.h>
#include <stddef.h>

int start_as_systemd_service(const char* service_name, int argc, char **argv, char **environment);

#endif /* SYSTEMD_SERVICE_H */
