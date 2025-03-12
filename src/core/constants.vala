namespace MatchScore {
    public const double LOWEST = -100.0;
    public const double NONE = 0.0;
    public const double TIEBREAKER = 0.01;
    public const double DECREMENT_MINOR = 0.8;
    public const double DECREMENT_MEDIUM = 0.5;
    public const double DECREMENT_MAJOR = 0.3;
    public const double INCREMENT_MINOR = 1.2;
    public const double INCREMENT_SMALL = 2.5;
    public const double INCREMENT_MEDIUM = 3.5;
    public const double INCREMENT_LARGE = 4.5;
    public const double INCREMENT_HUGE = 5.5;

    public const double BELOW_THRESHOLD = -100.0;
    public const double THRESHOLD = 0;
    public const double ABOVE_THRESHOLD = 5.1;
    public const double BELOW_AVERAGE = 6;
    public const double AVERAGE = 7;
    public const double ABOVE_AVERAGE = 8;
    public const double GOOD = 9;
    public const double VERY_GOOD = 12.0;
    public const double EXCELLENT = 15.0;
    public const double PRETTY_HIGH = 1000.0;

    public const double HIGHEST = 10000.0;
}


namespace BobLauncher {
    public struct Score : double { }

    public const string BOB_LAUNCHER_APP_ID = "io.github.trbjo.bob.launcher";
    public const string BOB_LAUNCHER_OBJECT_PATH = "/io/github/trbjo/bob/launcher";
}
