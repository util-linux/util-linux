chfn_chsh_sources = files(
  'ch-common.c',
  'ch-common.h',
)
chfn_chsh_deps = [lib_readline]
chfn_chsh_manadocs = files(
  'chfn.1.adoc',
  'chsh.1.adoc',
)

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
  chfn_chsh_sources += selinux_utils_c
  chfn_chsh_deps += [lib_selinux]
endif

chfn_sources = files(
  'chfn.c',
)

test_islocal_sources = files(
  'islocal.c',
)

test_consoles_sources = files(
  'sulogin-consoles.c',
)

last_sources = files(
  'last.c',
) + \
  monotonic_c
last_manadocs = files('last.1.adoc')

login_sources = files(
  'login.c',
)
login_manadocs = files('login.1.adoc')

sulogin_sources = files(
  'sulogin.c',
  'sulogin-consoles.c',
  'sulogin-consoles.h',
)
sulogin_manadocs = files('sulogin.8.adoc')

lslogins_manadocs = files('lslogins.1.adoc')

nologin_manadocs = files('nologin.8.adoc')

newgrp_manadocs = files('newgrp.1.adoc')

runuser_manadocs = files('runuser.1.adoc')

su_manadocs = files('su.1.adoc')

utmpdump_manadocs = files('utmpdump.1.adoc')

vipw_manadocs = files('vipw.8.adoc')
