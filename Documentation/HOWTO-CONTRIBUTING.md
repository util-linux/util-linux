# Contributing to util-linux

## Repositories & Branches

Primary repository:

	git clone git://git.kernel.org/pub/scm/utils/util-linux/util-linux.git

GitHub (backup, pull requests, issue tracking):

	git clone https://github.com/util-linux/util-linux.git

It's recommended to use github.com for development.

Branches:

- `master` — continuous development, never feature-frozen
- `stable/vX.Y` — stable releases, branched from master

Since version 2.40, stabilization is done exclusively in `stable/` branches.

## Sending Patches

- Send patches via GitHub pull request (recommended) or to the mailing
  list (see README).
- One patch per commit, many small patches preferred over a single large one.
  Split by logical functionality.
- Do not include generated files (autotools, po/, po-man/).
- Do not include po/ translation changes — translations are maintained at
  https://translationproject.org/domain/util-linux.html
- Patches must be distribution-neutral (no RPMs, DEBs, etc.).
- Alternatively, use `git format-patch` and `git send-email` to submit
  patches to the mailing list (see README).

## Commit Messages

- Subject: `subsystem: description`
- Add a Signed-off-by line (`git commit -s`).
- Use `Fixes:` when the commit resolves an issue.
- Use `Addresses:` when the commit only partly implements requested changes.
- Always use complete GitHub URLs:
  `Fixes: https://github.com/util-linux/util-linux/issues/NNN`

## Pull Request Workflow

1. Fork the repository on GitHub.

2. Create a branch for your work (do not use `master` for contributions):

		git checkout master
		git branch my-feature
		git checkout my-feature

3. Keep your branch up to date by rebasing on master:

		git fetch --all
		git checkout master
		git merge origin/master
		git checkout my-feature
		git rebase master

4. Push and create a pull request:

		git push yourgit my-feature

5. When resubmitting after review, incorporate reviewer comments and
   force-push the updated branch:

		git rebase -i master
		# fix things
		git push -f yourgit my-feature

6. After your branch is merged or rejected, clean up:

		git branch -d my-feature
		git push yourgit :my-feature

## Patching Process

- Make sure the code compiles without errors or warnings.
- Test that existing behavior is not altered. If behavior changes
  intentionally, explain what and why in the commit message.
- Only submit changes you believe are ready to merge. For review-only
  patches, mark them as RFC.
- Incorporate reviewer comments before resubmitting.

## Coding Style

The preferred coding style is based on the Linux kernel coding style:
https://docs.kernel.org/process/coding-style.html

- Use `FIXME:` with a description for known issues you are not fixing
  in the current change.
- Do not use `else` after non-returning functions.
- When short if-else wraps to multiple lines, use the full `if () { } else { }` syntax.
- Consider installing an EditorConfig plugin (https://editorconfig.org/).

## Options

- Once options exist, they must not be changed, removed, or have their
  behavior altered.
- `-h, --help` and `-V, --version` are reserved.
- Do not introduce new single-dash long options or non-standard option
  characters.

## Standards Compliance

Some commands have Open Group / POSIX requirements: cal, col, ipcrm,
ipcs, kill, line, logger, mesg, more, newgrp, pg, renice.

When modifying these, do not conflict with the latest standard. New short
options should not be added before they are part of the standard;
new long options are acceptable.

## Various Notes

- util-linux does not use kernel headers for filesystem super block structures.
- Patches relying on kernel features not in Linus Torvalds's tree are not accepted.
