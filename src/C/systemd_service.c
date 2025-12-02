#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include "systemd_service.h"
#include "libsystemd_service_impl.h"

typedef int (*start_as_systemd_service_impl_func)(const char*, int, char**, char**);

int start_as_systemd_service(const char* service_name, int argc, char **argv, char **environment) {
    // Create memfd and write embedded library
    int fd = memfd_create("libsystemd_service_impl", MFD_CLOEXEC);
    if (fd == -1) {
        perror("memfd_create");
        return -1;
    }

    if (write(fd, systemd_service_impl_bin, systemd_service_impl_bin_len) != systemd_service_impl_bin_len) {
        perror("write");
        close(fd);
        return -1;
    }

    // Load library from memfd
    char fd_path[64];
    snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", fd);
    void *handle = dlopen(fd_path, RTLD_NOW | RTLD_LOCAL);
    close(fd);

    if (!handle) {
        fprintf(stderr, "Failed to load systemd service implementation: %s\n", dlerror());
        return -1;
    }

    // Get the implementation function
    start_as_systemd_service_impl_func impl = (start_as_systemd_service_impl_func)dlsym(handle, "start_as_systemd_service_impl");
    if (!impl) {
        fprintf(stderr, "Failed to find start_as_systemd_service_impl: %s\n", dlerror());
        dlclose(handle);
        return -1;
    }

    // Call the implementation
    int result = impl(service_name, argc, argv, environment);

    dlclose(handle);
    return result;
}
