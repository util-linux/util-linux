chfn_chsh_sources = files(
  'ch-common.c',
  'ch-common.h',
)
chfn_chsh_deps = [lib_readline]

if chfn_chsh_password
  chfn_chsh_sources += files(
    'auth.c',
    'auth.h',
  )
  chfn_chsh_deps += [
    lib_pam,
    lib_pam_misc,
  ]
endif

if lib_user.found()
  chfn_chsh_sources += files(
    'libuser.c',
    'libuser.h',
  )
  chfn_chsh_deps += lib_user
else
  chfn_chsh_sources += files(
    'islocal.c',
    'islocal.h',
    'setpwnam.c',
    'setpwnam.h',
  )
endif

if lib_selinux.found()
  chfn_chsh_sources += files(
    'selinux_utils.c',
    'selinux_utils.h',
  )
  chfn_chsh_deps += [lib_selinux]
endif

chfn_sources = files(
  'chfn.c',
  'logindefs.c',
  'logindefs.h',
)

test_islocal_sources = files(
  'islocal.c',
)

test_logindefs_sources = files(
  'logindefs.c',
  'logindefs.h',
)

test_consoles_sources = files(
  'sulogin-consoles.c',
)

last_sources = files(
  'last.c',
) + \
  monotonic_c

login_sources = files(
  'login.c',
  'logindefs.c',
  'logindefs.h',
)

sulogin_sources = files(
  'sulogin.c',
  'sulogin-consoles.c',
  'sulogin-consoles.h',
)
