[CCode (cheader_filename = "constants.h")]
namespace MatchScore {
    public struct Score : int16 { }

    // Application constants
    [CCode (cname = "BOB_LAUNCHER_APP_ID")]
    public const string BOB_LAUNCHER_APP_ID;

    [CCode (cname = "BOB_LAUNCHER_OBJECT_PATH")]
    public const string BOB_LAUNCHER_OBJECT_PATH;

    // Score modification constants
    [CCode (cname = "SCORE_LOWEST")]
    public const Score LOWEST;

    [CCode (cname = "SCORE_DECREMENT_MINOR")]
    public const Score DECREMENT_MINOR;

    [CCode (cname = "SCORE_DECREMENT_MEDIUM")]
    public const Score DECREMENT_MEDIUM;

    [CCode (cname = "SCORE_DECREMENT_MAJOR")]
    public const Score DECREMENT_MAJOR;

    [CCode (cname = "SCORE_INCREMENT_MINOR")]
    public const Score INCREMENT_MINOR;

    [CCode (cname = "SCORE_INCREMENT_SMALL")]
    public const Score INCREMENT_SMALL;

    [CCode (cname = "SCORE_INCREMENT_MEDIUM")]
    public const Score INCREMENT_MEDIUM;

    [CCode (cname = "SCORE_INCREMENT_LARGE")]
    public const Score INCREMENT_LARGE;

    [CCode (cname = "SCORE_INCREMENT_HUGE")]
    public const Score INCREMENT_HUGE;

    [CCode (cname = "SCORE_BELOW_THRESHOLD")]
    public const Score BELOW_THRESHOLD;

    [CCode (cname = "SCORE_ABOVE_THRESHOLD")]
    public const Score ABOVE_THRESHOLD;

    [CCode (cname = "SCORE_BELOW_AVERAGE")]
    public const Score BELOW_AVERAGE;

    [CCode (cname = "SCORE_AVERAGE")]
    public const Score AVERAGE;

    [CCode (cname = "SCORE_ABOVE_AVERAGE")]
    public const Score ABOVE_AVERAGE;

    [CCode (cname = "SCORE_GOOD")]
    public const Score GOOD;

    [CCode (cname = "SCORE_VERY_GOOD")]
    public const Score VERY_GOOD;

    [CCode (cname = "SCORE_EXCELLENT")]
    public const Score EXCELLENT;

    [CCode (cname = "SCORE_PRETTY_HIGH")]
    public const Score PRETTY_HIGH;

    [CCode (cname = "SCORE_HIGHEST")]
    public const Score HIGHEST;
}
