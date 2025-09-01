namespace SystemdServiceUtils {
    [CCode (cheader_filename = "systemd_service_utils.h", cname = "signal_ready_if_needed", has_target = false)]
    public static void signal_ready([CCode (array_length_type = "size_t")] uint8[] socket_name);

    public static uint8[] make_abstract_socket_name(string name) {
        var result = new uint8[name.length + 1];
        result[0] = '\0';
        for (int i = 0; i < name.length; i++) {
            result[i + 1] = name.data[i];
        }
        return result;
    }

}
