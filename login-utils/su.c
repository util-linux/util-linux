/* su for GNU.  Run a shell with substitute user and group IDs.
   Copyright (C) 1992-2006 Free Software Foundation, Inc.

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

   This program intentionally does not support a "wheel group" that
   restricts who can su to UID 0 accounts.  RMS considers that to
   be fascist.

#ifdef USE_PAM

   Actually, with PAM, su has nothing to do with whether or not a
   wheel group is enforced by su.  RMS tries to restrict your access
   to a su which implements the wheel group, but PAM considers that
   to be fascist, and gives the user/sysadmin the opportunity to
   enforce a wheel group by proper editing of /etc/pam.d/su

#endif

   Compile-time options:
   -DSYSLOG_SUCCESS	Log successful su's (by default, to root) with syslog.
   -DSYSLOG_FAILURE	Log failed su's (by default, to root) with syslog.

   -DSYSLOG_NON_ROOT	Log all su's, not just those to root (UID 0).
   Never logs attempted su's to nonexistent accounts.

   Written by David MacKenzie <djm@gnu.ai.mit.edu>.  */

#include <config.h>
#include <stdio.h>
#include <getopt.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#ifdef USE_PAM
# include <security/pam_appl.h>
# include <security/pam_misc.h>
# include <signal.h>
# include <sys/wait.h>
# include <sys/fsuid.h>
#endif

/* Hide any system prototype for getusershell.
   This is necessary because some Cray systems have a conflicting
   prototype (returning `int') in <unistd.h>.  */
#define getusershell _getusershell_sys_proto_

#include "system.h"
#include "getpass.h"

#undef getusershell

#if HAVE_SYSLOG_H && HAVE_SYSLOG
# include <syslog.h>
# define SYSLOG_SUCCESS  1
# define SYSLOG_FAILURE  1
# define SYSLOG_NON_ROOT 1
#else
# undef SYSLOG_SUCCESS
# undef SYSLOG_FAILURE
# undef SYSLOG_NON_ROOT
#endif

#if HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifndef HAVE_ENDGRENT
# define endgrent() ((void) 0)
#endif

#ifndef HAVE_ENDPWENT
# define endpwent() ((void) 0)
#endif

#if HAVE_SHADOW_H
# include <shadow.h>
#endif

#include "error.h"

/* The official name of this program (e.g., no `g' prefix).  */
#define PROGRAM_NAME "su"

#define AUTHORS "David MacKenzie"

#if HAVE_PATHS_H
# include <paths.h>
#endif

#include "getdef.h"

/* The default PATH for simulated logins to non-superuser accounts.  */
#define DEFAULT_LOGIN_PATH "/usr/local/bin:/bin:/usr/bin"

/* The default PATH for simulated logins to superuser accounts.  */
#define DEFAULT_ROOT_LOGIN_PATH "/usr/sbin:/bin:/usr/bin:/sbin"

/* The shell to run if none is given in the user's passwd entry.  */
#define DEFAULT_SHELL "/bin/sh"

/* The user to become if none is specified.  */
#define DEFAULT_USER "root"

#ifndef USE_PAM
char *crypt ();
#endif
char *getusershell ();
void endusershell ();
void setusershell ();

extern char **environ;

static void run_shell (char const *, char const *, char **, size_t)
     ATTRIBUTE_NORETURN;

/* The name this program was run with.  */
char *program_name;

/* If true, pass the `-f' option to the subshell.  */
static bool fast_startup;

/* If true, simulate a login instead of just starting a shell.  */
static bool simulate_login;

/* If true, change some environment vars to indicate the user su'd to.  */
static bool change_environment;

#ifdef USE_PAM
static bool _pam_session_opened;
static bool _pam_cred_established;
#endif

static struct option const longopts[] =
{
  {"command", required_argument, NULL, 'c'},
  {"fast", no_argument, NULL, 'f'},
  {"login", no_argument, NULL, 'l'},
  {"preserve-environment", no_argument, NULL, 'p'},
  {"shell", required_argument, NULL, 's'},
  {GETOPT_HELP_OPTION_DECL},
  {GETOPT_VERSION_OPTION_DECL},
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
    xalloc_die ();
}

#if defined SYSLOG_SUCCESS || defined SYSLOG_FAILURE
/* Log the fact that someone has run su to the user given by PW;
   if SUCCESSFUL is true, they gave the correct password, etc.  */

static void
log_su (struct passwd const *pw, bool successful)
{
  const char *new_user, *old_user, *tty;

# ifndef SYSLOG_NON_ROOT
  if (pw->pw_uid)
    return;
# endif
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
  /* 4.2BSD openlog doesn't have the third parameter.  */
  openlog (last_component (program_name), 0
# ifdef LOG_AUTH
	   , LOG_AUTH
# endif
	   );
  syslog (LOG_NOTICE,
# ifdef SYSLOG_NON_ROOT
	  "%s(to %s) %s on %s",
# else
	  "%s%s on %s",
# endif
	  successful ? "" : "FAILED SU ",
# ifdef SYSLOG_NON_ROOT
	  new_user,
# endif
	  old_user, tty);
  closelog ();
}
#endif

#ifdef USE_PAM
# define PAM_SERVICE_NAME PROGRAM_NAME
# define PAM_SERVICE_NAME_L PROGRAM_NAME "-l"
static sig_atomic_t volatile caught_signal = false;
static pam_handle_t *pamh = NULL;
static int retval;
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
	xalloc_die ();
      env++;
    }
}

static void
create_watching_parent (void)
{
  pid_t child;
  sigset_t ourset;
  int status = 0;

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
      if (sigaddset (&ourset, SIGTERM)
	  || sigaddset (&ourset, SIGALRM)
	  || sigaction (SIGTERM, &action, NULL)
	  || sigprocmask (SIG_UNBLOCK, &ourset, NULL))
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
#endif

/* Ask the user for a password.
   If PAM is in use, let PAM ask for the password if necessary.
   Return true if the user gives the correct password for entry PW,
   false if not.  Return true without asking for a password if run by UID 0
   or if PW has an empty password.  */

static bool
correct_password (const struct passwd *pw)
{
#ifdef USE_PAM
  const struct passwd *lpw;
  const char *cp;

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
#else /* !USE_PAM */
  char *unencrypted, *encrypted, *correct;
# if HAVE_GETSPNAM && HAVE_STRUCT_SPWD_SP_PWDP
  /* Shadow passwd stuff for SVR3 and maybe other systems.  */
  const struct spwd *sp = getspnam (pw->pw_name);

  endspent ();
  if (sp)
    correct = sp->sp_pwdp;
  else
# endif
    correct = pw->pw_passwd;

  if (getuid () == 0 || !correct || correct[0] == '\0')
    return true;

  unencrypted = getpass (_("Password:"));
  if (!unencrypted)
    {
      error (0, 0, _("getpass: cannot open /dev/tty"));
      return false;
    }
  encrypted = crypt (unencrypted, correct);
  memset (unencrypted, 0, strlen (unencrypted));
  return STREQ (encrypted, correct);
#endif /* !USE_PAM */
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
			? getdef_str ("PATH", DEFAULT_LOGIN_PATH)
			: getdef_str ("SUPATH", DEFAULT_ROOT_LOGIN_PATH)));
    }
  else
    {
      /* Set HOME, SHELL, and if not becoming a super-user,
	 USER and LOGNAME.  */
      if (change_environment)
        {
          xsetenv ("HOME", pw->pw_dir);
          xsetenv ("SHELL", shell);
	  if (getdef_bool ("ALWAYS_SET_PATH", 0))
	    xsetenv ("PATH", (pw->pw_uid
			      ? getdef_str ("PATH",
					    DEFAULT_LOGIN_PATH)
			      : getdef_str ("SUPATH",
					    DEFAULT_ROOT_LOGIN_PATH)));
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

#ifdef USE_PAM
  export_pamenv ();
#endif
}

/* Become the user and group(s) specified by PW.  */

static void
init_groups (const struct passwd *pw)
{
#ifdef HAVE_INITGROUPS
  errno = 0;
  if (initgroups (pw->pw_name, pw->pw_gid) == -1)
    {
# ifdef USE_PAM
      cleanup_pam (PAM_ABORT);
# endif
      error (EXIT_FAIL, errno, _("cannot set groups"));
    }
  endgrent ();
#endif

#ifdef USE_PAM
  retval = pam_setcred (pamh, PAM_ESTABLISH_CRED);
  if (retval != PAM_SUCCESS)
    error (EXIT_FAILURE, 0, "%s", pam_strerror (pamh, retval));
  else
    _pam_cred_established = 1;
#endif
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
  char const **args = xnmalloc (n_args, sizeof *args);
  size_t argno = 1;

  if (simulate_login)
    {
      char *arg0;
      char *shell_basename;

      shell_basename = last_component (shell);
      arg0 = xmalloc (strlen (shell_basename) + 2);
      arg0[0] = '-';
      strcpy (arg0 + 1, shell_basename);
      args[0] = arg0;
    }
  else
    args[0] = last_component (shell);
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
      if (*line != '#' && STREQ (line, shell))
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
  -f, --fast                   pass -f to the shell (for csh or tcsh)\n\
  -m, --preserve-environment   do not reset environment variables\n\
  -p                           same as -m\n\
  -s, --shell=SHELL            run SHELL if /etc/shells allows it\n\
"), stdout);
      fputs (HELP_OPTION_DESCRIPTION, stdout);
      fputs (VERSION_OPTION_DESCRIPTION, stdout);
      fputs (_("\
\n\
A mere - implies -l.   If USER not given, assume root.\n\
"), stdout);
      emit_bug_reporting_address ();
    }
  exit (status);
}

int
main (int argc, char **argv)
{
  int optc;
  const char *new_user = DEFAULT_USER;
  char *command = NULL;
  char *shell = NULL;
  struct passwd *pw;
  struct passwd pw_copy;

  initialize_main (&argc, &argv);
  program_name = argv[0];
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, LOCALEDIR);
  textdomain (PACKAGE);

  initialize_exit_failure (EXIT_FAIL);
  atexit (close_stdout);

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

	case_GETOPT_HELP_CHAR;

	case_GETOPT_VERSION_CHAR (PROGRAM_NAME, AUTHORS);

	default:
	  usage (EXIT_FAIL);
	}
    }

  if (optind < argc && STREQ (argv[optind], "-"))
    {
      simulate_login = true;
      ++optind;
    }
  if (optind < argc)
    new_user = argv[optind++];

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
#ifdef SYSLOG_FAILURE
      log_su (pw, false);
#endif
      sleep (getdef_num ("FAIL_DELAY", 1));
      error (EXIT_FAIL, 0, _("incorrect password"));
    }
#ifdef SYSLOG_SUCCESS
  else
    {
      log_su (pw, true);
    }
#endif

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

#ifdef USE_PAM
  create_watching_parent ();
  /* Now we're in the child.  */
#endif

  change_identity (pw);

  /* Set environment after pam_open_session, which may put KRB5CCNAME
     into the pam_env, etc.  */

  modify_environment (pw, shell);

  if (simulate_login && chdir (pw->pw_dir) != 0)
    error (0, errno, _("warning: cannot change directory to %s"), pw->pw_dir);

  run_shell (shell, command, argv + optind, MAX (0, argc - optind));
}
