#include <limits.h>
#include <errno.h>


#define ERRSTRING strerror (errno)
#define COMMAND_SIZE  (PIPE_BUF - 4)


#define COMMAND_TEST       0  /*  No wait, signal                            */
#define COMMAND_NEED       1  /*  Wait, signal                               */
#define COMMAND_ROLLBACK   2  /*  Wait, signal                               */
#define COMMAND_DUMP_LIST  3  /*  No wait, no signal                         */
#define COMMAND_PROVIDE    4  /*  Wait, signal                               */

#define SIG_PRESENT        SIGUSR1  /*  Service is available                 */
#define SIG_STOPPED        SIGUSR1  /*  Service was stopped OK               */
#define SIG_NOT_PRESENT    SIGUSR2  /*  Not present, but that's OK           */
#define SIG_FAILED         SIGPOLL  /*  Startup failed                       */
#define SIG_NOT_STOPPED    SIGPOLL  /*  Failed to stop                       */
#define SIG_NOT_CHILD      SIGPOLL  /*  Not a child of init                  */

struct command_struct  /*  Must always be COMMAND_SIZE  */
{
    signed int command;
    pid_t pid;
    pid_t ppid;
    char name[1];
};
