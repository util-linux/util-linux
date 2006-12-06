/*
 * krishna balasubramanian 1993
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>

int main(int argc, char **argv)
{
	int id;
	union semun arg;

	arg.val = 0; 

	if (argc != 3 || strlen(argv[1]) < 3) {
		printf ("usage: %s [shm | msg | sem] id\n", argv[0]);
		exit (1);
	}
	id = atoi (argv[2]);
	switch (argv[1][1])  {
	case 'h':
		if (!shmctl (id, IPC_RMID, NULL)) 
		break;
		perror ("shmctl "); 
		exit (1);
		
	case 'e':
		if (!semctl (id, 0, IPC_RMID, arg)) 
		break;
		perror ("semctl "); 
		exit (1);

	case 's':
		if (!msgctl (id, IPC_RMID, NULL)) 
		break;
		perror ("msgctl "); 
		exit (1);

	default:
		printf ("usage: %s [-shm | -msg | -sem] id\n", argv[0]);
		exit (1);
	}
	printf ("resource deleted\n");
	return 0;
}
			
