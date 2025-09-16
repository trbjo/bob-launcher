[CCode (cheader_filename = "constants.h")]
namespace MatchScore {
    // Application constants
    [CCode (cname = "BOB_LAUNCHER_APP_ID")]
    public const string BOB_LAUNCHER_APP_ID;

    [CCode (cname = "BOB_LAUNCHER_OBJECT_PATH")]
    public const string BOB_LAUNCHER_OBJECT_PATH;

    // Score modification constants
    [CCode (cname = "SCORE_LOWEST")]
    public const double LOWEST;

    [CCode (cname = "SCORE_NONE")]
    public const double NONE;

    [CCode (cname = "SCORE_TIEBREAKER")]
    public const double TIEBREAKER;

    [CCode (cname = "SCORE_DECREMENT_MINOR")]
    public const double DECREMENT_MINOR;

    [CCode (cname = "SCORE_DECREMENT_MEDIUM")]
    public const double DECREMENT_MEDIUM;

    [CCode (cname = "SCORE_DECREMENT_MAJOR")]
    public const double DECREMENT_MAJOR;

    [CCode (cname = "SCORE_INCREMENT_MINOR")]
    public const double INCREMENT_MINOR;

    [CCode (cname = "SCORE_INCREMENT_SMALL")]
    public const double INCREMENT_SMALL;

    [CCode (cname = "SCORE_INCREMENT_MEDIUM")]
    public const double INCREMENT_MEDIUM;

    [CCode (cname = "SCORE_INCREMENT_LARGE")]
    public const double INCREMENT_LARGE;

    [CCode (cname = "SCORE_INCREMENT_HUGE")]
    public const double INCREMENT_HUGE;

    // Threshold and scoring constants
    [CCode (cname = "SCORE_BELOW_THRESHOLD")]
    public const double BELOW_THRESHOLD;

    [CCode (cname = "SCORE_MATCH_SCORE_THRESHOLD")]
    public const int THRESHOLD;

    [CCode (cname = "SCORE_ABOVE_THRESHOLD")]
    public const double ABOVE_THRESHOLD;

    [CCode (cname = "SCORE_BELOW_AVERAGE")]
    public const int BELOW_AVERAGE;

    [CCode (cname = "SCORE_AVERAGE")]
    public const int AVERAGE;

    [CCode (cname = "SCORE_ABOVE_AVERAGE")]
    public const int ABOVE_AVERAGE;

    [CCode (cname = "SCORE_GOOD")]
    public const int GOOD;

    [CCode (cname = "SCORE_VERY_GOOD")]
    public const double VERY_GOOD;

    [CCode (cname = "SCORE_EXCELLENT")]
    public const double EXCELLENT;

    [CCode (cname = "SCORE_PRETTY_HIGH")]
    public const double PRETTY_HIGH;

    [CCode (cname = "SCORE_HIGHEST")]
    public const double HIGHEST;
}
