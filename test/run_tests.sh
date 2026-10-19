#!/bin/sh
# Host unit tests for the pure scheduling logic (no ESP-IDF needed).
set -e
cd "$(dirname "$0")"
gcc -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -I../main \
    test_scheduler.c ../main/scheduler.c -o test_scheduler
./test_scheduler
