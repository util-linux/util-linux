/* su for Linux.  Run a shell with substitute user and group IDs.
   Copyright (C) 1992-2006 Free Software Foundation, Inc.
   Copyright (C) 2012 SUSE Linux Products GmbH, Nuernberg

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Run a shell with the real and effective UID and GID and groups
   of USER, default `root'.

   The shell run is taken from USER's password entry, /bin/sh if
   none is specified there.  If the account has a password, su
   prompts for a password unless run by a user with real UID 0.

   Does not change the current directory.
   Sets `HOME' and `SHELL' from the password entry for USER, and if
   USER is not root, sets `USER' and `LOGNAME' to USER.
   The subshell is not a login shell.

   If one or more ARGs are given, they are passed as additional
   arguments to the subshell.

   Does not handle /bin/sh or other shells specially
   (setting argv[0] to "-su", passing -c only to certain shells, etc.).
   I don't see the point in doing that, and it's ugly.

   Based on an implemenation by David MacKenzie <djm@gnu.ai.mit.edu>.  */

#ifndef MAX
# define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

/* Exit statuses for programs like 'env' that exec other programs.
   EXIT_FAILURE might not be 1, so use EXIT_FAIL in such programs.  */
enum
{
  EXIT_FAIL = 1,
  EXIT_CANNOT_INVOKE = 126,
  EXIT_ENOENT = 127
};

#include <config.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/fsuid.h>

#include <syslog.h>

#include "error.h"

#include <stdbool.h>
#include "xalloc.h"
#include "nls.h"
#include "pathnames.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "su"

/* name of the pam configuration files. separate configs for su and su -  */
#define PAM_SERVICE_NAME PROGRAM_NAME
#define PAM_SERVICE_NAME_L PROGRAM_NAME "-l"

#include "logindefs.h"

/* The shell to run if none is given in the user's passwd entry.  */
#define DEFAULT_SHELL "/bin/sh"

/* The user to become if none is specified.  */
#define DEFAULT_USER "root"

extern char **environ;

static void run_shell (char const *, char const *, char **, size_t)
     __attribute__ ((__noreturn__));

/* The name this program was run with.  */
char *program_name;

/* If true, pass the `-f' option to the subshell.  */
static bool fast_startup;

/* If true, simulate a login instead of just starting a shell.  */
static bool simulate_login;

/* If true, change some environment vars to indicate the user su'd to.  */
static bool change_environment;

/* If true, then don't call setsid() with a command. */
int same_session = 0;

static bool _pam_session_opened;
static bool _pam_cred_established;
static sig_atomic_t volatile caught_signal = false;
static pam_handle_t *pamh = NULL;


static struct option const longopts[] =
{
  {"command", required_argument, NULL, 'c'},
  {"session-command", required_argument, NULL, 'C'},
  {"fast", no_argument, NULL, 'f'},
  {"login", no_argument, NULL, 'l'},
  {"preserve-environment", no_argument, NULL, 'p'},
  {"shell", required_argument, NULL, 's'},
  {"help", no_argument, 0, 'u'},
  {"version", no_argument, 0, 'v'},
  {NULL, 0, NULL, 0}
};

/* Add NAME=VAL to the environment, checking for out of memory errors.  */

static void
xsetenv (char const *name, char const *val)
{
  size_t namelen = strlen (name);
  size_t vallen = strlen (val);
  char *string = xmalloc (namelen + 1 + vallen + 1);
  strcpy (string, name);
  string[namelen] = '=';
  strcpy (string + namelen + 1, val);
  if (putenv (string) != 0)
    error (EXIT_FAILURE, 0, _("out of memory"));
}

/* Log the fact that someone has run su to the user given by PW;
   if SUCCESSFUL is true, they gave the correct password, etc.  */

static void
log_su (struct passwd const *pw, bool successful)
{
  const char *new_user, *old_user, *tty;

  new_user = pw->pw_name;
  /* The utmp entry (via getlogin) is probably the best way to identify
     the user, especially if someone su's from a su-shell.  */
  old_user = getlogin ();
  if (!old_user)
    {
      /* getlogin can fail -- usually due to lack of utmp entry.
	 Resort to getpwuid.  */
      struct passwd *pwd = getpwuid (getuid ());
      old_user = (pwd ? pwd->pw_name : "");
    }
  tty = ttyname (STDERR_FILENO);
  if (!tty)
    tty = "none";

  openlog (basename (program_name), 0 , LOG_AUTH);
  syslog (LOG_NOTICE, "%s(to %s) %s on %s",
	  successful ? "" : "FAILED SU ",
	  new_user, old_user, tty);
  closelog ();
}

static struct pam_conv conv =
{
  misc_conv,
  NULL
};

# define PAM_BAIL_P(a) \
  if (retval) \
    { \
      pam_end (pamh, retval); \
      a; \
    }

static void
cleanup_pam (int retcode)
{
  if (_pam_session_opened)
    pam_close_session (pamh, 0);

  if (_pam_cred_established)
    pam_setcred (pamh, PAM_DELETE_CRED | PAM_SILENT);

  pam_end(pamh, retcode);
}

/* Signal handler for parent process.  */
static void
su_catch_sig (int sig)
{
  caught_signal = true;
}

/* Export env variables declared by PAM modules.  */
static void
export_pamenv (void)
{
  char **env;

  /* This is a copy but don't care to free as we exec later anyways.  */
  env = pam_getenvlist (pamh);
  while (env && *env)
    {
      if (putenv (*env) != 0)
	error (EXIT_FAILURE, 0, _("out of memory"));
      env++;
    }
}

static void
create_watching_parent (void)
{
  pid_t child;
  sigset_t ourset;
  int status = 0;
  int retval;

  retval = pam_open_session (pamh, 0);
  if (retval != PAM_SUCCESS)
    {
      cleanup_pam (retval);
      error (EXIT_FAILURE, 0, _("cannot not open session: %s"),
	     pam_strerror (pamh, retval));
    }
  else
    _pam_session_opened = 1;

  child = fork ();
  if (child == (pid_t) -1)
    {
      cleanup_pam (PAM_ABORT);
      error (EXIT_FAILURE, errno, _("cannot create child process"));
    }

  /* the child proceeds to run the shell */
  if (child == 0)
    return;

  /* In the parent watch the child.  */

  /* su without pam support does not have a helper that keeps
     sitting on any directory so let's go to /.  */
  if (chdir ("/") != 0)
    error (0, errno, _("warning: cannot change directory to %s"), "/");

  sigfillset (&ourset);
  if (sigprocmask (SIG_BLOCK, &ourset, NULL))
    {
      error (0, errno, _("cannot block signals"));
      caught_signal = true;
    }
  if (!caught_signal)
    {
      struct sigaction action;
      action.sa_handler = su_catch_sig;
      sigemptyset (&action.sa_mask);
      action.sa_flags = 0;
      sigemptyset (&ourset);
    if (!same_session)
      {
        if (sigaddset(&ourset, SIGINT) || sigaddset(&ourset, SIGQUIT))
          {
            error (0, errno, _("cannot set signal handler"));
            caught_signal = true;
          }
      }
    if (!caught_signal && (sigaddset(&ourset, SIGTERM)
                    || sigaddset(&ourset, SIGALRM)
                    || sigaction(SIGTERM, &action, NULL)
                    || sigprocmask(SIG_UNBLOCK, &ourset, NULL))) {
	  error (0, errno, _("cannot set signal handler"));
	  caught_signal = true;
	}
    if (!caught_signal && !same_session && (sigaction(SIGINT, &action, NULL)
                                     || sigaction(SIGQUIT, &action, NULL)))
      {
        error (0, errno, _("cannot set signal handler"));
        caught_signal = true;
      }
    }
  if (!caught_signal)
    {
      pid_t pid;
      for (;;)
	{
	  pid = waitpid (child, &status, WUNTRACED);

	  if (pid != (pid_t)-1 && WIFSTOPPED (status))
	    {
	      kill (getpid (), SIGSTOP);
	      /* once we get here, we must have resumed */
	      kill (pid, SIGCONT);
	    }
	  else
	    break;
	}
      if (pid != (pid_t)-1)
	if (WIFSIGNALED (status))
	  status = WTERMSIG (status) + 128;
	else
	  status = WEXITSTATUS (status);
      else
	status = 1;
    }
  else
    status = 1;

  if (caught_signal)
    {
      fprintf (stderr, _("\nSession terminated, killing shell..."));
      kill (child, SIGTERM);
    }

  cleanup_pam (PAM_SUCCESS);

  if (caught_signal)
    {
      sleep (2);
      kill (child, SIGKILL);
      fprintf (stderr, _(" ...killed.\n"));
    }
  exit (status);
}

static bool
correct_password (const struct passwd *pw)
{
  const struct passwd *lpw;
  const char *cp;
  int retval;

  retval = pam_start (simulate_login ? PAM_SERVICE_NAME_L : PAM_SERVICE_NAME,
		      pw->pw_name, &conv, &pamh);
  PAM_BAIL_P (return false);

  if (isatty (0) && (cp = ttyname (0)) != NULL)
    {
      const char *tty;

      if (strncmp (cp, "/dev/", 5) == 0)
	tty = cp + 5;
      else
	tty = cp;
      retval = pam_set_item (pamh, PAM_TTY, tty);
      PAM_BAIL_P (return false);
    }
# if 0 /* Manpage discourages use of getlogin.  */
  cp = getlogin ();
  if (!(cp && *cp && (lpw = getpwnam (cp)) != NULL && lpw->pw_uid == getuid ()))
# endif
  lpw = getpwuid (getuid ());
  if (lpw && lpw->pw_name)
    {
      retval = pam_set_item (pamh, PAM_RUSER, (const void *) lpw->pw_name);
      PAM_BAIL_P (return false);
    }
  retval = pam_authenticate (pamh, 0);
  PAM_BAIL_P (return false);
  retval = pam_acct_mgmt (pamh, 0);
  if (retval == PAM_NEW_AUTHTOK_REQD)
    {
      /* Password has expired.  Offer option to change it.  */
      retval = pam_chauthtok (pamh, PAM_CHANGE_EXPIRED_AUTHTOK);
      PAM_BAIL_P (return false);
    }
  PAM_BAIL_P (return false);
  /* Must be authenticated if this point was reached.  */
  return true;
}

/* Add or clear /sbin and /usr/sbin for the su command
   used without `-'.  */

/* Set if /sbin is found in path.  */
#define SBIN_MASK	0x01
/* Set if /usr/sbin is found in path.  */
#define USBIN_MASK	0x02

static char *
addsbin (const char *const path)
{
  unsigned char smask = 0;
  char *ptr, *tmp, *cur, *ret = NULL;
  size_t len;

  if (!path || *path == 0)
    return NULL;

  tmp = xstrdup (path);
  cur = tmp;
  for (ptr = strsep (&cur, ":"); ptr != NULL; ptr = strsep (&cur, ":"))
    {
      if (!strcmp (ptr, "/sbin"))
	smask |= SBIN_MASK;
      if (!strcmp (ptr, "/usr/sbin"))
	smask |= USBIN_MASK;
    }

  if ((smask & (USBIN_MASK|SBIN_MASK)) == (USBIN_MASK|SBIN_MASK))
    {
      free (tmp);
      return NULL;
    }

  len = strlen (path);
  if (!(smask & USBIN_MASK))
    len += strlen ("/usr/sbin:");

  if (!(smask & SBIN_MASK))
    len += strlen (":/sbin");

  ret = xmalloc (len + 1);
  strcpy (tmp, path);

  *ret = 0;
  cur = tmp;
  for (ptr = strsep (&cur, ":"); ptr; ptr = strsep (&cur, ":"))
    {
      if (!strcmp (ptr, "."))
	continue;
      if (*ret)
	strcat (ret, ":");
      if (!(smask & USBIN_MASK) && !strcmp (ptr, "/bin"))
	{
	  strcat (ret, "/usr/sbin:");
	  strcat (ret, ptr);
	  smask |= USBIN_MASK;
	  continue;
	}
      if (!(smask & SBIN_MASK) && !strcmp (ptr, "/usr/bin"))
	{
	  strcat (ret, ptr);
	  strcat (ret, ":/sbin");
	  smask |= SBIN_MASK;
	  continue;
	}
      strcat (ret, ptr);
    }
  free (tmp);

  if (!(smask & USBIN_MASK))
    strcat (ret, ":/usr/sbin");

  if (!(smask & SBIN_MASK))
    strcat (ret, ":/sbin");

  return ret;
}

static char *
clearsbin (const char *const path)
{
  char *ptr, *tmp, *cur, *ret = NULL;

  if (!path || *path == 0)
    return NULL;

  tmp = strdup (path);
  if (!tmp)
    return NULL;

  ret = xmalloc (strlen (path) + 1);
  *ret = 0;
  cur = tmp;
  for (ptr = strsep (&cur, ":"); ptr; ptr = strsep (&cur, ":"))
    {
      if (!strcmp (ptr, "/sbin"))
	continue;
      if (!strcmp (ptr, "/usr/sbin"))
	continue;
      if (!strcmp (ptr, "/usr/local/sbin"))
	continue;
      if (*ret)
	strcat (ret, ":");
      strcat (ret, ptr);
    }
  free (tmp);

  return ret;
}

/* Update `environ' for the new shell based on PW, with SHELL being
   the value for the SHELL environment variable.  */

static void
modify_environment (const struct passwd *pw, const char *shell)
{
  if (simulate_login)
    {
      /* Leave TERM unchanged.  Set HOME, SHELL, USER, LOGNAME, PATH.
         Unset all other environment variables.  */
      char const *term = getenv ("TERM");
      if (term)
	term = xstrdup (term);
      environ = xmalloc ((6 + !!term) * sizeof (char *));
      environ[0] = NULL;
      if (term)
	xsetenv ("TERM", term);
      xsetenv ("HOME", pw->pw_dir);
      xsetenv ("SHELL", shell);
      xsetenv ("USER", pw->pw_name);
      xsetenv ("LOGNAME", pw->pw_name);
      xsetenv ("PATH", (pw->pw_uid
			? getlogindefs_str ("PATH", _PATH_DEFPATH)
			: getlogindefs_str ("SUPATH", _PATH_DEFPATH_ROOT)));
    }
  else
    {
      /* Set HOME, SHELL, and if not becoming a super-user,
	 USER and LOGNAME.  */
      if (change_environment)
        {
          xsetenv ("HOME", pw->pw_dir);
          xsetenv ("SHELL", shell);
	  if (getlogindefs_bool ("ALWAYS_SET_PATH", 0))
	    xsetenv ("PATH", (pw->pw_uid
			      ? getlogindefs_str ("PATH",
					    _PATH_DEFPATH)
			      : getlogindefs_str ("SUPATH",
					    _PATH_DEFPATH_ROOT)));
	  else
	    {
	      char const *path = getenv ("PATH");
	      char *new = NULL;

	      if (pw->pw_uid)
		new = clearsbin (path);
	      else
		new = addsbin (path);

	      if (new)
		{
		  xsetenv ("PATH", new);
		  free (new);
		}
	    }
          if (pw->pw_uid)
            {
              xsetenv ("USER", pw->pw_name);
              xsetenv ("LOGNAME", pw->pw_name);
            }
        }
    }

  export_pamenv ();
}

/* Become the user and group(s) specified by PW.  */

static void
init_groups (const struct passwd *pw)
{
  int retval;
  errno = 0;
  if (initgroups (pw->pw_name, pw->pw_gid) == -1)
    {
      cleanup_pam (PAM_ABORT);
      error (EXIT_FAIL, errno, _("cannot set groups"));
    }
  endgrent ();

  retval = pam_setcred (pamh, PAM_ESTABLISH_CRED);
  if (retval != PAM_SUCCESS)
    error (EXIT_FAILURE, 0, "%s", pam_strerror (pamh, retval));
  else
    _pam_cred_established = 1;
}

static void
change_identity (const struct passwd *pw)
{
  if (setgid (pw->pw_gid))
    error (EXIT_FAIL, errno, _("cannot set group id"));
  if (setuid (pw->pw_uid))
    error (EXIT_FAIL, errno, _("cannot set user id"));
}

/* Run SHELL, or DEFAULT_SHELL if SHELL is empty.
   If COMMAND is nonzero, pass it to the shell with the -c option.
   Pass ADDITIONAL_ARGS to the shell as more arguments; there
   are N_ADDITIONAL_ARGS extra arguments.  */

static void
run_shell (char const *shell, char const *command, char **additional_args,
	   size_t n_additional_args)
{
  size_t n_args = 1 + fast_startup + 2 * !!command + n_additional_args + 1;
  char const **args = xcalloc (n_args, sizeof *args);
  size_t argno = 1;

  if (simulate_login)
    {
      char *arg0;
      char *shell_basename;

      shell_basename = basename (shell);
      arg0 = xmalloc (strlen (shell_basename) + 2);
      arg0[0] = '-';
      strcpy (arg0 + 1, shell_basename);
      args[0] = arg0;
    }
  else
    args[0] = basename (shell);
  if (fast_startup)
    args[argno++] = "-f";
  if (command)
    {
      args[argno++] = "-c";
      args[argno++] = command;
    }
  memcpy (args + argno, additional_args, n_additional_args * sizeof *args);
  args[argno + n_additional_args] = NULL;
  execv (shell, (char **) args);

  {
    int exit_status = (errno == ENOENT ? EXIT_ENOENT : EXIT_CANNOT_INVOKE);
    error (0, errno, "%s", shell);
    exit (exit_status);
  }
}

/* Return true if SHELL is a restricted shell (one not returned by
   getusershell), else false, meaning it is a standard shell.  */

static bool
restricted_shell (const char *shell)
{
  char *line;

  setusershell ();
  while ((line = getusershell ()) != NULL)
    {
      if (*line != '#' && !strcmp (line, shell))
	{
	  endusershell ();
	  return false;
	}
    }
  endusershell ();
  return true;
}

void
usage (int status)
{
  if (status != EXIT_SUCCESS)
    fprintf (stderr, _("Try `%s --help' for more information.\n"),
	     program_name);
  else
    {
      printf (_("Usage: %s [OPTION]... [-] [USER [ARG]...]\n"), program_name);
      fputs (_("\
Change the effective user id and group id to that of USER.\n\
\n\
  -, -l, --login               make the shell a login shell\n\
  -c, --command=COMMAND        pass a single COMMAND to the shell with -c\n\
  --session-command=COMMAND    pass a single COMMAND to the shell with -c\n\
                               and do not create a new session\n\
  -f, --fast                   pass -f to the shell (for csh or tcsh)\n\
  -m, --preserve-environment   do not reset environment variables\n\
  -p                           same as -m\n\
  -s, --shell=SHELL            run SHELL if /etc/shells allows it\n\
"), stdout);
      fputs (_(" -u, --help     display this help and exit\n"), stdout);
      fputs (_(" -v, --version  output version information and exit\n"), stdout);
      fputs (_("\
\n\
A mere - implies -l.   If USER not given, assume root.\n\
"), stdout);
    }
  exit (status);
}

void load_config(void)
{
  logindefs_load_file("/etc/default/su");
  logindefs_load_file(_PATH_LOGINDEFS);
}

int
main (int argc, char **argv)
{
  int optc;
  const char *new_user = DEFAULT_USER;
  char *command = NULL;
  int request_same_session = 0;
  char *shell = NULL;
  struct passwd *pw;
  struct passwd pw_copy;

  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  fast_startup = false;
  simulate_login = false;
  change_environment = true;

  while ((optc = getopt_long (argc, argv, "c:flmps:", longopts, NULL)) != -1)
    {
      switch (optc)
	{
	case 'c':
	  command = optarg;
	  break;

        case 'C':
          command = optarg;
          request_same_session = 1;
          break;

	case 'f':
	  fast_startup = true;
	  break;

	case 'l':
	  simulate_login = true;
	  break;

	case 'm':
	case 'p':
	  change_environment = false;
	  break;

	case 's':
	  shell = optarg;
	  break;

	case 'u':
	  usage(0);

	case 'v':
	  printf(UTIL_LINUX_VERSION);
	  exit(EXIT_SUCCESS);

	default:
	  usage (EXIT_FAIL);
	}
    }

  if (optind < argc && !strcmp (argv[optind], "-"))
    {
      simulate_login = true;
      ++optind;
    }
  if (optind < argc)
    new_user = argv[optind++];

  logindefs_load_defaults = load_config;

  pw = getpwnam (new_user);
  if (! (pw && pw->pw_name && pw->pw_name[0] && pw->pw_dir && pw->pw_dir[0]
	 && pw->pw_passwd))
    error (EXIT_FAIL, 0, _("user %s does not exist"), new_user);

  /* Make a copy of the password information and point pw at the local
     copy instead.  Otherwise, some systems (e.g. Linux) would clobber
     the static data through the getlogin call from log_su.
     Also, make sure pw->pw_shell is a nonempty string.
     It may be NULL when NEW_USER is a username that is retrieved via NIS (YP),
     but that doesn't have a default shell listed.  */
  pw_copy = *pw;
  pw = &pw_copy;
  pw->pw_name = xstrdup (pw->pw_name);
  pw->pw_passwd = xstrdup (pw->pw_passwd);
  pw->pw_dir = xstrdup (pw->pw_dir);
  pw->pw_shell = xstrdup (pw->pw_shell && pw->pw_shell[0]
			  ? pw->pw_shell
			  : DEFAULT_SHELL);
  endpwent ();

  if (!correct_password (pw))
    {
      log_su (pw, false);
      sleep (getlogindefs_num ("FAIL_DELAY", 1));
      error (EXIT_FAIL, 0, _("incorrect password"));
    }
  else
    {
      log_su (pw, true);
    }

  if (request_same_session || !command || !pw->pw_uid)
    same_session = 1;

  if (!shell && !change_environment)
    shell = getenv ("SHELL");
  if (shell && getuid () != 0 && restricted_shell (pw->pw_shell))
    {
      /* The user being su'd to has a nonstandard shell, and so is
	 probably a uucp account or has restricted access.  Don't
	 compromise the account by allowing access with a standard
	 shell.  */
      error (0, 0, _("using restricted shell %s"), pw->pw_shell);
      shell = NULL;
    }
  shell = xstrdup (shell ? shell : pw->pw_shell);

  init_groups (pw);

  create_watching_parent ();
  /* Now we're in the child.  */

  change_identity (pw);
  if (!same_session)
    setsid ();

  /* Set environment after pam_open_session, which may put KRB5CCNAME
     into the pam_env, etc.  */

  modify_environment (pw, shell);

  if (simulate_login && chdir (pw->pw_dir) != 0)
    error (0, errno, _("warning: cannot change directory to %s"), pw->pw_dir);

  run_shell (shell, command, argv + optind, MAX (0, argc - optind));
}

// vim: sw=2 cinoptions=>4,n-2,{2,^-2,\:2,=2,g0,h2,p5,t0,+2,(0,u0,w1,m1
