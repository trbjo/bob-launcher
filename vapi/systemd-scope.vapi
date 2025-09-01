[CCode (cheader_filename = "systemd-scope.h")]
namespace SystemdScope {
    [CCode (cname = "create_systemd_scope_lowlevel")]
    public static string? create_scope_lowlevel(string slice_value, string scope_name, Posix.pid_t pid);

    [CCode (cname = "get_caller_systemd_slice")]
    public extern int get_caller_systemd_slice(out string slice);

    [CCode (cname = "get_pid_systemd_slice")]
    public extern int get_pid_systemd_slice(Posix.pid_t pid, out string slice);

}
