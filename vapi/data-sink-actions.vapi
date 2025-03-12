[Compact]
[CCode (cheader_filename = "data-sink-actions.h", cname = "ActionSet", has_type_id = false)]
public class ActionSet {
    [CCode (cname = "action_set_add_action")]
    public void add_action (BobLauncher.Action action);

    public bool query_empty;
    public BobLauncher.Match m;
}
