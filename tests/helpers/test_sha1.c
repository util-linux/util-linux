
#include <stdio.h>
#include <unistd.h>

#include "sha1.h"

int main(void)
{
	int i, ret;
	SHA1_CTX ctx;
	unsigned char digest[SHA1LENGTH];
	unsigned char buf[BUFSIZ];

	SHA1Init( &ctx );

	while(!feof(stdin) && !ferror(stdin)) {
		ret = fread(buf, 1, sizeof(buf), stdin);
		if (ret)
			SHA1Update( &ctx, buf, ret );
	}

	fclose(stdin);
	SHA1Final( digest, &ctx );

	for (i = 0; i < SHA1LENGTH; i++)
		printf( "%02x", digest[i] );
	printf("\n");
	return 0;
}
