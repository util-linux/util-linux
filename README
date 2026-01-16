
				  util-linux

		util-linux is a random collection of Linux utilities

     Note: for the years 2006-2010 this project was named "util-linux-ng".

COMPILE & INSTALL:

      See Documentation/howto-compilation.txt.

MAILING LIST:

      E-MAIL:  util-linux@vger.kernel.org
      URL:     http://vger.kernel.org/vger-lists.html#util-linux
      ARCHIVE: https://lore.kernel.org/util-linux/

      The mailing list will reject email messages that contain:
       - more than 100K characters
       - html
       - spam phrases/keywords
      See: http://vger.kernel.org/majordomo-info.html#taboo

IRC CHANNEL:

      #util-linux at libera.chat:

      irc://irc.libera.chat/util-linux

      The IRC channel and Mailing list are for developers and project
      maintainers. For end users it is recommended to utilize the
      distribution's support system.

BUG REPORTING:

      E-MAIL: util-linux@vger.kernel.org
      Web:    https://github.com/util-linux/util-linux/issues

      Bug reports with sensitive or private information: Karel Zak <kzak@redhat.com>

      This project has no resources to provide support for distribution specific
      issues. For end users it is recommended to utilize the distribution's
      support system.

NLS (PO TRANSLATIONS):

      PO files are maintained by:
	  https://translationproject.org/domain/util-linux.html

VERSION SCHEMA:

      Standard releases:
	  <major>.<minor>[.<maint>]
	     major = fatal and deep changes
	     minor = typical release with new features
	     maint = maintenance releases; bug fixes only

      Development releases:
	 <major>.<minor>-rc<N>

SOURCE CODE:

 Download archive:
	  https://www.kernel.org/pub/linux/utils/util-linux/

 See also:
     Documentation/howto-contribute.txt
     Documentation/howto-build-sys.txt
     Documentation/howto-pull-request.txt

 SCM (Source Code Management) Repository:

    Primary repository:
	  git clone git://git.kernel.org/pub/scm/utils/util-linux/util-linux.git

    Backup repository:
	  git clone https://github.com/util-linux/util-linux.git

    Web interfaces:
	  https://git.kernel.org/cgit/utils/util-linux/util-linux.git
	  https://github.com/util-linux/util-linux

      Note: the GitHub repository may contain temporary development branches too.

      The kernel.org repository contains master (current development) and stable/*
      (maintenance) branches only. All master or stable/* changes are always pushed
      to both repositories at the same time.

    Repository Branches: 'git branch -a'
	  Master Branch:
	   - Continuously developed, no feature freeze or translation freezes.
	   - Day-to-day status is: 'it works for me'. This means that its
	     normal state is useful but not well tested.

	  Stable Branches:
	   - Public releases.
	   - Branch name: stable/v<major>.<minor>.
	   - Created from the 'master' branch.
	   - The release candidates and final release are always based
             on the stable branch.
	   - Maintenance releases are part of, and belong to, their respective
	     stable branch. As such, they are tags(<major>.<minor>.<maint>) and
	     not branches of their own. They are not part of, visible in, or
	     have anything to do with the 'master' development branch. In git
	     terminology: maintenance releases are not reachable from 'master'.
	   - When initially cloned (as with the 'git clone' command given above),
	     these branches are created as 'remote tracking branches' and are
	     only visible by using the -a or -r options to 'git branch'. To
	     create a local branch, use the desired tag with this command:
	     'git checkout -b v2.29.2 v2.29.2'

    Tags: 'git tag'
	   - v<version> tag is created in the stable branch for every release.
	   - v<version>-start is created in the master branch to start work on the next release.
	   - All tags are signed by the maintainer's PGP key.


WORKFLOW EXAMPLE:

    Development                     Releases
    (Master Branch)                 (Stable/vX.Y Branch)

    - Sync latest translations
      from translationproject.org
    - Tag v<X.Y+1>-devel            - Fork from master to stable/v<X.Y> branch
                                      - Code stabilization
                                    - RC1 (Tag v<X.Y>-rc1)
                                      - Backport bug fixes
                                    - RC2 (Tag v<X.Y>-rc2)
                                      - po/ and po-man/ translations available on
                                        translationproject.org/
                                      - Wait 7-17 days for translators
                                      - Sync latest translations
                                      - Backport bug fixes
                                    - Final release v<X.Y> (Tag v<X.Y>)
                                      ...
                                    - Release v<X.Y>.1
                                      ...
                                    - Release v<X.Y>.2

