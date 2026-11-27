/* Fuzz check of hand-written core functions (fuzz.c). */
#pragma once

/* Calls the functions named in `list` ("all" or names separated by commas)
 * on random inputs; 0 on success. */
int fuzz_run(const char *list);
