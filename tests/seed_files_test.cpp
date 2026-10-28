// Checks seed file lines: the five numbers oiftrelax reads, and the note the viewer shows on hover.
#include "SeedFiles.h"

#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

} // namespace

int main()
{
    Seed s;
    check(parseSeedLine("243 250 111 2 2", s) && s.x == 243 && s.y == 250 && s.z == 111 &&
              s.label == 2 && s.internal == 2 && s.note.empty(),
          "plain line: numbers, no note");

    check(parseSeedLine("10 20 30 1 0   # LPA_04 | x=0.026367 y=0.008789 z=-0.004500 m  ", s) &&
              s.x == 10 && s.internal == 0 && s.note == "LPA_04 | x=0.026367 y=0.008789 z=-0.004500 m",
          "note after '#', trimmed");

    check(parseSeedLine("1 2 3 4 5 free text", s) && s.note == "free text", "note without '#'");
    check(parseSeedLine("1 2 3 4 5 #", s) && s.note.empty(), "bare '#' is no note");
    check(parseSeedLine("1 2 3 4 5", s) && s.note.empty(), "a reused Seed drops the old note");
    check(!parseSeedLine("1 2 3 4", s), "four numbers refused");
    check(!parseSeedLine("# 1 2 3 4 5", s), "comment-only line refused");
    check(!parseSeedLine("3025", s), "count line refused");

    return failures ? 1 : 0;
}
