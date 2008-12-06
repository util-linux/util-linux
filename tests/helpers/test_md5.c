
#include <stdio.h>
#include <unistd.h>

#include "md5.h"

int
main(int argc, char *argv[])
{
	int i, ret;
	struct MD5Context ctx;
	unsigned char digest[16];
	unsigned char buf[BUFSIZ];

	MD5Init( &ctx );

	while(!feof(stdin) && !ferror(stdin)) {
		ret = fread(buf, 1, sizeof(buf), stdin);
		if (ret)
			MD5Update( &ctx, buf, ret );
	}

	fclose(stdin);
	MD5Final( digest, &ctx );

	for (i = 0; i < 16; i++)
		printf( "%02x", digest[i] );
	printf("  -\n");
	return 0;
}
