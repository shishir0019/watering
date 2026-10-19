#include "scheduler.h"

#include <stdbool.h>

#define SEARCH_DAYS 8

const char *source_name(run_source_t src)
{
    switch (src) {
    case SRC_SCHEDULE: return "schedule";
    case SRC_MANUAL:   return "manual";
    case SRC_CATCHUP:  return "catchup";
    case SRC_RETRY:    return "retry";
    default:           return "none";
    }
}

static bool runs_on(const schedule_t *s, int weekday)
{
    return s->enabled && (s->days & (1u << weekday));
}

// Start time of `minute` on the local day `day_offset` days from `ref`.
static time_t occurrence(const struct tm *ref, int day_offset, uint16_t minute, int *weekday)
{
    struct tm t = *ref;
    t.tm_mday += day_offset;
    t.tm_hour = minute / 60;
    t.tm_min = minute % 60;
    t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t r = mktime(&t);
    *weekday = (t.tm_wday + 6) % 7;
    return r;
}

time_t sched_next_run(const schedule_t *s, int n, time_t now)
{
    struct tm ref;
    localtime_r(&now, &ref);
    time_t best = 0;
    for (int d = 0; d <= SEARCH_DAYS; d++) {
        for (int i = 0; i < n; i++) {
            int wd;
            time_t t = occurrence(&ref, d, s[i].minute, &wd);
            if (t > now && runs_on(&s[i], wd) && (best == 0 || t < best)) {
                best = t;
            }
        }
        if (best) {
            break;
        }
    }
    return best;
}

time_t sched_latest_between(const schedule_t *s, int n, time_t from, time_t to, uint16_t *seconds)
{
    if (to <= from) {
        return 0;
    }
    struct tm ref;
    localtime_r(&to, &ref);
    time_t best = 0;
    for (int d = 0; d >= -SEARCH_DAYS; d--) {
        for (int i = 0; i < n; i++) {
            int wd;
            time_t t = occurrence(&ref, d, s[i].minute, &wd);
            if (t > from && t < to && t > best && runs_on(&s[i], wd)) {
                best = t;
                if (seconds) {
                    *seconds = s[i].seconds;
                }
            }
        }
        if (best) {
            break;
        }
    }
    return best;
}

int sched_due_at(const schedule_t *s, int n, time_t t, int *out)
{
    struct tm lt;
    localtime_r(&t, &lt);
    int minute = lt.tm_hour * 60 + lt.tm_min;
    int wd = (lt.tm_wday + 6) % 7;
    int count = 0;
    for (int i = 0; i < n; i++) {
        if (s[i].minute == minute && runs_on(&s[i], wd)) {
            out[count++] = i;
        }
    }
    return count;
}
