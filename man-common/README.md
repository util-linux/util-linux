# Working with the Asciidoc files

To get the groff version from an asciidoc man page (example for a dummy man page filename.1.adoc):

    asciidoctor -b manpage \
                -a release-version=2.37 \
                -a adjtime_path=/etc/adjtime \
                -a docdir=/usr/share/doc \
                -a runstatedir=/run \
                -D output_directory \
                filename.1.adoc

To get the translated versions, add an extra option which includes translation.adoc:

                -a translation

The "-a" options are applicable to certain man pages. However, if the mentioned
variable is unused, they don't produce an error message.

The footers of the asciidoc files already contain relative paths to the files in man-common.
