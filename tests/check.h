/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Davidian */
/* Minimal test helpers: CHECK prints the failing expression and counts failures. */
#ifndef DAVIDIAN_TEST_CHECK_H
#define DAVIDIAN_TEST_CHECK_H

#include <stdio.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                                        \
    do {                                                                        \
        ++g_checks;                                                             \
        if (!(cond)) {                                                          \
            ++g_failures;                                                       \
            printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond);              \
            printf(__VA_ARGS__);                                                \
            printf("\n");                                                       \
        }                                                                       \
    } while (0)

static int check_summary(const char* name)
{
    printf("%s: %d checks, %d failed\n", name, g_checks, g_failures);
    return g_failures ? 1 : 0;
}

#endif
