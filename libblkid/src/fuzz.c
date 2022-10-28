#include "blkidP.h"
#include "fuzz.h"

#include <stdlib.h>
#include <unistd.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int fd;
    char name[] = "/tmp/test-script-fuzz.XXXXXX";

    fd = mkostemp(name, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC);
    if (fd == -1)
        err(EXIT_FAILURE, "mkostemp() failed");

    if (write(fd, data, size) != (ssize_t)size)
        goto out;

    blkid_probe pr = blkid_new_probe_from_filename(name);
    if (pr != NULL) {
        blkid_probe_enable_partitions(pr, TRUE);
        blkid_probe_set_partitions_flags(pr, FALSE);
        blkid_probe_enable_superblocks(pr, TRUE);
        blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_DEFAULT | BLKID_SUBLKS_FSINFO | BLKID_SUBLKS_MAGIC | BLKID_SUBLKS_VERSION | BLKID_SUBLKS_BADCSUM);
        blkid_do_safeprobe(pr);
    }
    blkid_free_probe(pr);
out:
    close(fd);
    unlink(name);
    return 0;
}