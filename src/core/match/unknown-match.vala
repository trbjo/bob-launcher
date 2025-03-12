namespace BobLauncher {
    public class UnknownMatch : Match {
        private string query_string;

        public override string get_title() {
            return this.query_string;
        }

        public override string get_description() {
            return "";
        }

        public override string get_icon_name() {
            return "unknown";
        }

        public string get_mime_type() {
            return "application/x-unknown";
        }

        public UnknownMatch (string query_string) {
            Object();
            this.query_string = query_string;
        }
    }

}
