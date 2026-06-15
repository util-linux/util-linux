#include "fdiskP.h"
#include "fuzz.h"

#include <stdlib.h>
#include <unistd.h>

static int process_file(const char *name)
{
    int rc = -1;
    struct fdisk_context *cxt = fdisk_new_context();
    if (cxt != NULL) {
        if (fdisk_assign_device(cxt, name, 1) == 0) {
            struct fdisk_table *tb = NULL;
            if (fdisk_get_partitions(cxt, &tb) == 0 && tb != NULL) {
                size_t i, n = fdisk_table_get_nents(tb);
                for (i = 0; i < n; i++)
                    fdisk_table_get_partition(tb, i);
                fdisk_unref_table(tb);
            }
            fdisk_deassign_device(cxt, 1);
            rc = 0;
        }
        fdisk_unref_context(cxt);
    }
    return rc;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    int fd;
    char name[] = "/tmp/test-fdisk-fuzz.XXXXXX";

    fd = mkostemp(name, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC);
    if (fd == -1)
        err(EXIT_FAILURE, "mkostemp() failed");

    if (write(fd, data, size) != (ssize_t)size)
        goto out;

    process_file(name);
out:
    close(fd);
    unlink(name);
    return 0;
}

#ifndef FUZZ_TARGET
int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
        if (process_file(argv[i]) == 0)
            printf(" OK\n");
        else
            printf(" FAILED\n");

    }
}
#endif
