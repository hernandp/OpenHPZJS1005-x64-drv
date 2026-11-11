// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
/* Test-only compatibility shim for building the unmodified Linux references. */
#pragma once
#include <sys/types.h>
extern int optind;
extern char* optarg;
int getopt(int argc, char* const argv[], const char* options);
