#include "mountP.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
        struct libmnt_table *tb = NULL;
        FILE *f = NULL;

        if (size == 0)
                return 0;

        tb = mnt_new_table();
        assert(tb);

        f = fmemopen((char*) data, size, "re");
        assert(f);

        (void) mnt_table_parse_stream(tb, f, "mountinfo");

        mnt_unref_table(tb);
        fclose(f);

        return 0;
}
