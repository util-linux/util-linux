#include "fuzz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libsmartcols.h"

static void process_string(const char *str)
{
    /* Parse the input as a libsmartcols filter expression. This drives the
       flex scanner, the bison grammar, parameter/holder construction and the
       regcomp() done for the =~ operator. */
    struct libscols_filter *fltr = scols_new_filter(str);

    scols_unref_filter(fltr);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *str;

    /* Larger than the library's 1024-byte limit to exercise the
     * rejection path, small enough to avoid OOM in sanitizer builds */
    if (size > 4096)
        return 0;

    str = malloc(size + 1);
    if (!str)
        return 0;

    memcpy(str, data, size);
    str[size] = '\0';

    process_string(str);

    free(str);
    return 0;
}

#ifndef FUZZ_TARGET
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "r");
        char *buf;
        long len;
        size_t n;

        if (!f)
            continue;
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len < 0) {
            fclose(f);
            continue;
        }
        buf = malloc(len + 1);
        if (!buf) {
            fclose(f);
            continue;
        }
        n = fread(buf, 1, len, f);
        fclose(f);
        buf[n] = '\0';
        process_string(buf);
        free(buf);
    }
    return 0;
}
#endif
