[CCode (cheader_filename = "constants.h")]
namespace MatchScore {
    // Application constants
    [CCode (cname = "BOB_LAUNCHER_APP_ID")]
    public const string BOB_LAUNCHER_APP_ID;

    [CCode (cname = "BOB_LAUNCHER_OBJECT_PATH")]
    public const string BOB_LAUNCHER_OBJECT_PATH;

    // Score modification constants
    [CCode (cname = "LOWEST")]
    public const double LOWEST;

    [CCode (cname = "NONE")]
    public const double NONE;

    [CCode (cname = "TIEBREAKER")]
    public const double TIEBREAKER;

    [CCode (cname = "DECREMENT_MINOR")]
    public const double DECREMENT_MINOR;

    [CCode (cname = "DECREMENT_MEDIUM")]
    public const double DECREMENT_MEDIUM;

    [CCode (cname = "DECREMENT_MAJOR")]
    public const double DECREMENT_MAJOR;

    [CCode (cname = "INCREMENT_MINOR")]
    public const double INCREMENT_MINOR;

    [CCode (cname = "INCREMENT_SMALL")]
    public const double INCREMENT_SMALL;

    [CCode (cname = "INCREMENT_MEDIUM")]
    public const double INCREMENT_MEDIUM;

    [CCode (cname = "INCREMENT_LARGE")]
    public const double INCREMENT_LARGE;

    [CCode (cname = "INCREMENT_HUGE")]
    public const double INCREMENT_HUGE;

    // Threshold and scoring constants
    [CCode (cname = "BELOW_THRESHOLD")]
    public const double BELOW_THRESHOLD;

    [CCode (cname = "MATCH_SCORE_THRESHOLD")]
    public const int THRESHOLD;

    [CCode (cname = "ABOVE_THRESHOLD")]
    public const double ABOVE_THRESHOLD;

    [CCode (cname = "BELOW_AVERAGE")]
    public const int BELOW_AVERAGE;

    [CCode (cname = "AVERAGE")]
    public const int AVERAGE;

    [CCode (cname = "ABOVE_AVERAGE")]
    public const int ABOVE_AVERAGE;

    [CCode (cname = "GOOD")]
    public const int GOOD;

    [CCode (cname = "VERY_GOOD")]
    public const double VERY_GOOD;

    [CCode (cname = "EXCELLENT")]
    public const double EXCELLENT;

    [CCode (cname = "PRETTY_HIGH")]
    public const double PRETTY_HIGH;

    [CCode (cname = "HIGHEST")]
    public const double HIGHEST;
}
