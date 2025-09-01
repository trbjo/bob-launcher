#include <dbus/dbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

char* create_systemd_scope_lowlevel(const char* slice_value, const char* scope_name, pid_t pid) {
    DBusError err;
    DBusConnection* conn;
    DBusMessage* msg;
    DBusMessage* reply;
    DBusMessageIter args, array, dict, variant, dict_entry;
    char* job_path = NULL;
    char* scope_object_path = NULL;
    dbus_uint32_t job_id = 0;

    dbus_error_init(&err);

    conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Connection Error (%s)\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }
    if (NULL == conn) {
        fprintf(stderr, "Connection Null\n");
        return NULL;
    }

    dbus_bus_add_match(conn,
        "type='signal',interface='org.freedesktop.systemd1.Manager',member='JobRemoved'",
        &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Add match error: %s\n", err.message);
        dbus_error_free(&err);
        dbus_connection_unref(conn);
        return NULL;
    }
    dbus_connection_flush(conn);

    msg = dbus_message_new_method_call("org.freedesktop.systemd1",
                                       "/org/freedesktop/systemd1",
                                       "org.freedesktop.systemd1.Manager",
                                       "StartTransientUnit");
    if (NULL == msg) {
        fprintf(stderr, "Message Null\n");
        dbus_connection_unref(conn);
        return NULL;
    }

    dbus_message_iter_init_append(msg, &args);

    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &scope_name)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    const char* mode = "fail";
    if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &mode)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sv)", &array)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, NULL, &dict_entry)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    const char* pids_key = "PIDs";
    if (!dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &pids_key)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "au", &variant)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    DBusMessageIter array_iter;
    if (!dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "u", &array_iter)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    dbus_uint32_t pid_uint = (dbus_uint32_t)pid;
    if (!dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_UINT32, &pid_uint)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&variant, &array_iter)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&dict_entry, &variant)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&array, &dict_entry)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, NULL, &dict_entry)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    const char* slice_key = "Slice";
    if (!dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &slice_key)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "s", &variant)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &slice_value)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&dict_entry, &variant)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&array, &dict_entry)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&array, DBUS_TYPE_STRUCT, NULL, &dict_entry)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    const char* collect_key = "CollectMode";
    if (!dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &collect_key)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "s", &variant)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    const char* collect_value = "inactive-or-failed";
    if (!dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &collect_value)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&dict_entry, &variant)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&array, &dict_entry)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    if (!dbus_message_iter_close_container(&args, &array)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    DBusMessageIter aux_array;
    if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "(sa(sv))", &aux_array)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }
    if (!dbus_message_iter_close_container(&args, &aux_array)) {
        fprintf(stderr, "Out Of Memory!\n");
        goto cleanup;
    }

    reply = dbus_connection_send_with_reply_and_block(conn, msg, -1, &err);

    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "Error sending message: %s\n", err.message);
        dbus_error_free(&err);
        goto cleanup;
    }

    if (NULL == reply) {
        fprintf(stderr, "Reply Null\n");
        goto cleanup;
    }

    DBusMessageIter reply_args;
    if (!dbus_message_iter_init(reply, &reply_args)) {
        fprintf(stderr, "Reply has no arguments\n");
        goto cleanup;
    } else if (DBUS_TYPE_OBJECT_PATH != dbus_message_iter_get_arg_type(&reply_args)) {
        fprintf(stderr, "Reply argument is not object path\n");
        goto cleanup;
    } else {
        char* path;
        dbus_message_iter_get_basic(&reply_args, &path);
        job_path = strdup(path);

        const char* job_prefix = "/org/freedesktop/systemd1/job/";
        if (strncmp(job_path, job_prefix, strlen(job_prefix)) == 0) {
            const char* job_id_str = job_path + strlen(job_prefix);
            job_id = (dbus_uint32_t)atoi(job_id_str);
        }
    }

    dbus_message_unref(reply);
    dbus_message_unref(msg);
    msg = NULL;

    if (job_path && job_id > 0) {
        while (1) {
            dbus_connection_read_write_dispatch(conn, -1);
            DBusMessage* signal = dbus_connection_pop_message(conn);

            if (signal == NULL) {
                continue;
            }

            if (dbus_message_is_signal(signal, "org.freedesktop.systemd1.Manager", "JobRemoved")) {
                DBusMessageIter sig_args;
                if (dbus_message_iter_init(signal, &sig_args)) {
                    dbus_uint32_t removed_job_id;
                    char* removed_job_path;
                    char* unit_path;
                    char* result_str;

                    if (dbus_message_iter_get_arg_type(&sig_args) == DBUS_TYPE_UINT32) {
                        dbus_message_iter_get_basic(&sig_args, &removed_job_id);

                        if (removed_job_id == job_id) {
                            dbus_message_iter_next(&sig_args);
                            dbus_message_iter_get_basic(&sig_args, &removed_job_path);
                            dbus_message_iter_next(&sig_args);
                            dbus_message_iter_get_basic(&sig_args, &unit_path);
                            dbus_message_iter_next(&sig_args);
                            dbus_message_iter_get_basic(&sig_args, &result_str);

                            if (strcmp(result_str, "done") == 0) {
                                scope_object_path = strdup(unit_path);
                            } else {
                                fprintf(stderr, "Job failed with result: %s\n", result_str);
                            }

                            dbus_message_unref(signal);
                            break;
                        }
                    }
                }
            }

            dbus_message_unref(signal);
        }
    }

cleanup:
    if (job_path) free(job_path);
    if (msg) dbus_message_unref(msg);
    dbus_connection_unref(conn);

    return scope_object_path;
}


/* Get the cgroup path for a given PID */
static int get_pid_cgroup_path(pid_t pid, char **path) {
    char proc_path[256];
    FILE *f;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int found = 0;

    snprintf(proc_path, sizeof(proc_path), "/proc/%d/cgroup", pid);

    f = fopen(proc_path, "r");
    if (!f) {
        return -errno;
    }

    while ((read = getline(&line, &len, f)) != -1) {
        char *colon1, *colon2;

        /* Find first colon */
        colon1 = strchr(line, ':');
        if (!colon1) continue;

        /* Find second colon */
        colon2 = strchr(colon1 + 1, ':');
        if (!colon2) continue;

        /* For unified cgroup (v2), the line starts with "0::" */
        if (strncmp(line, "0::", 3) == 0) {
            char *cgroup_path = colon2 + 1;
            /* Remove newline */
            char *newline = strchr(cgroup_path, '\n');
            if (newline) *newline = '\0';

            *path = strdup(cgroup_path);
            found = 1;
            break;
        }
    }

    free(line);
    fclose(f);

    return found ? 0 : -ENOENT;
}

/* Extract the user slice from a cgroup path */
static int extract_user_slice(const char *cgroup_path, char **slice) {
    const char *p = cgroup_path;
    char *result = NULL;

    /* Skip leading slashes */
    while (*p == '/') p++;

    /* Skip until we find user@*.service */
    const char *user_service = strstr(p, "user@");
    if (!user_service) {
        return -ENOENT;
    }

    /* Find the .service suffix */
    const char *service_end = strstr(user_service, ".service");
    if (!service_end) {
        return -ENOENT;
    }

    /* Move past the .service/ part */
    p = service_end + 8; /* strlen(".service") */
    while (*p == '/') p++;

    /* Now we should be at the user slice (e.g., "session.slice", "app.slice", etc.) */
    if (*p == '\0') {
        /* We're directly in user@*.service */
        *slice = strdup("");
        return 0;
    }

    /* Find the next slash or end of string */
    const char *slice_end = strchr(p, '/');
    if (!slice_end) {
        /* The rest of the string is the slice */
        result = strdup(p);
    } else {
        /* Extract just the slice part */
        size_t len = slice_end - p;
        result = strndup(p, len);
    }

    /* Verify it ends with .slice */
    if (result && strstr(result, ".slice")) {
        *slice = result;
        return 0;
    }

    free(result);
    return -ENOENT;
}

/* Get the full cgroup path relative to the user instance */
static int extract_user_cgroup_path(const char *cgroup_path, char **user_path) {
    const char *p = cgroup_path;

    /* Skip leading slashes */
    while (*p == '/') p++;

    /* Skip until we find user@*.service */
    const char *user_service = strstr(p, "user@");
    if (!user_service) {
        return -ENOENT;
    }

    /* Find the .service suffix */
    const char *service_end = strstr(user_service, ".service");
    if (!service_end) {
        return -ENOENT;
    }

    /* Move past the .service/ part */
    p = service_end + 8; /* strlen(".service") */
    while (*p == '/') p++;

    if (*p == '\0') {
        *user_path = strdup("/");
    } else {
        *user_path = strdup(p);
    }

    return 0;
}

/* Get the systemd user slice for the calling process */
int get_caller_systemd_slice(char **slice) {
    char *cgroup_path = NULL;
    int ret;

    /* Get cgroup path for current process */
    ret = get_pid_cgroup_path(getpid(), &cgroup_path);
    if (ret < 0) {
        return ret;
    }

    /* Extract user slice */
    ret = extract_user_slice(cgroup_path, slice);

    free(cgroup_path);
    return ret;
}

/* Get the systemd user slice for a specific PID */
int get_pid_systemd_slice(pid_t pid, char **slice) {
    char *cgroup_path = NULL;
    int ret;

    /* Get cgroup path for the specified process */
    ret = get_pid_cgroup_path(pid, &cgroup_path);
    if (ret < 0) {
        return ret;
    }

    /* Extract user slice */
    ret = extract_user_slice(cgroup_path, slice);

    free(cgroup_path);
    return ret;
}
