// Host unit tests for scheduler.c and civil.h.
// Run: test/run_tests.sh

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "civil.h"
#include "scheduler.h"

static int s_failures;

#define CHECK(cond) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); s_failures++; } \
} while (0)

// Local time (TZ = UTC+6) to Unix seconds.
static time_t local(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d,
                    .tm_hour = h, .tm_min = mi, .tm_sec = s, .tm_isdst = -1 };
    return mktime(&t);
}

static schedule_t sched(uint8_t id, int h, int m, uint16_t secs, uint8_t days)
{
    return (schedule_t){ .id = id, .days = days, .enabled = 1, .minute = h * 60 + m, .seconds = secs };
}

static void test_civil(void)
{
    CHECK(days_from_civil(1970, 1, 1) == 0);
    CHECK(days_from_civil(2000, 3, 1) == 11017);
    CHECK(weekday_from_days(0) == 3);                              // Thursday
    CHECK(weekday_from_days(days_from_civil(2026, 10, 19)) == 0);  // Monday

    for (int64_t z = -1000; z < 30000; z += 7) {
        int y, m, d;
        civil_from_days(z, &y, &m, &d);
        CHECK(days_from_civil(y, m, d) == z);
    }
}

static void test_next_run(void)
{
    schedule_t s[] = {
        sched(1, 6, 0, 30, DAYS_ALL),
        sched(2, 14, 0, 30, DAYS_ALL),
        sched(3, 22, 0, 30, DAYS_ALL),
    };

    // 2026-10-19 is a Monday.
    CHECK(sched_next_run(s, 3, local(2026, 10, 19, 5, 0, 0)) == local(2026, 10, 19, 6, 0, 0));
    CHECK(sched_next_run(s, 3, local(2026, 10, 19, 6, 0, 0)) == local(2026, 10, 19, 14, 0, 0));
    CHECK(sched_next_run(s, 3, local(2026, 10, 19, 23, 0, 0)) == local(2026, 10, 20, 6, 0, 0));

    // Disabled schedules are ignored.
    s[1].enabled = 0;
    CHECK(sched_next_run(s, 3, local(2026, 10, 19, 7, 0, 0)) == local(2026, 10, 19, 22, 0, 0));

    // Weekday filter: Saturday only (bit 5).
    schedule_t sat = sched(1, 8, 30, 60, 1u << 5);
    CHECK(sched_next_run(&sat, 1, local(2026, 10, 19, 12, 0, 0)) == local(2026, 10, 24, 8, 30, 0));

    CHECK(sched_next_run(s, 0, local(2026, 10, 19, 12, 0, 0)) == 0);
}

static void test_latest_between(void)
{
    schedule_t s[] = {
        sched(1, 6, 0, 30, DAYS_ALL),
        sched(2, 14, 0, 45, DAYS_ALL),
    };
    uint16_t secs = 0;

    time_t to = local(2026, 10, 19, 15, 0, 0);
    CHECK(sched_latest_between(s, 2, to - 4 * 3600, to, &secs) == local(2026, 10, 19, 14, 0, 0));
    CHECK(secs == 45);

    // Nothing in window.
    CHECK(sched_latest_between(s, 2, local(2026, 10, 19, 14, 0, 0), to, &secs) == 0);

    // Crosses midnight.
    to = local(2026, 10, 20, 1, 0, 0);
    CHECK(sched_latest_between(s, 2, 0, to, &secs) == local(2026, 10, 19, 14, 0, 0));

    // Bounds are exclusive.
    time_t six = local(2026, 10, 19, 6, 0, 0);
    CHECK(sched_latest_between(s, 2, six, six + 60, &secs) == 0);
    CHECK(sched_latest_between(s, 2, six - 1, six + 1, &secs) == six);
    CHECK(sched_latest_between(s, 2, six - 1, six, &secs) == 0);
}

static void test_due_at(void)
{
    schedule_t s[] = {
        sched(1, 6, 0, 30, DAYS_ALL),
        sched(2, 6, 0, 30, 1u << 6),   // Sunday only
        sched(3, 6, 1, 30, DAYS_ALL),
    };
    int out[3];

    CHECK(sched_due_at(s, 3, local(2026, 10, 19, 6, 0, 0), out) == 1 && out[0] == 0);
    CHECK(sched_due_at(s, 3, local(2026, 10, 19, 6, 0, 59), out) == 1);
    CHECK(sched_due_at(s, 3, local(2026, 10, 25, 6, 0, 0), out) == 2);   // Sunday
    CHECK(sched_due_at(s, 3, local(2026, 10, 19, 6, 1, 0), out) == 1 && out[0] == 2);
    CHECK(sched_due_at(s, 3, local(2026, 10, 19, 7, 0, 0), out) == 0);
}

int main(void)
{
    setenv("TZ", "<+06>-6", 1);
    tzset();

    test_civil();
    test_next_run();
    test_latest_between();
    test_due_at();

    if (s_failures) {
        printf("%d check(s) failed\n", s_failures);
        return 1;
    }
    printf("All scheduler tests passed\n");
    return 0;
}
