#!/bin/bash
 
set -ex

PHASES=(${@:-CONFIGURE MAKE INSTALL CHECK DISTCHECK})
COMPILER="${COMPILER:?}"
COMPILER_VERSION="${COMPILER_VERSION}"
CFLAGS=(-O1 -g)
CXXFLAGS=(-O1 -g)
LDFLAGS=()
COVERITY_SCAN_TOOL_BASE="/tmp/coverity-scan-analysis"

# The project is still called "karelzak/util-linux" on Coverity
# so it shouldn't be changed to "util-linux/util-linux"
COVERITY_SCAN_PROJECT_NAME="karelzak/util-linux"

if [[ "$COMPILER" == clang ]]; then
    CC="clang${COMPILER_VERSION:+-$COMPILER_VERSION}"
    CXX="clang++${COMPILER_VERSION:+-$COMPILER_VERSION}"
elif [[ "$COMPILER" == gcc ]]; then
    CC="gcc${COMPILER_VERSION:+-$COMPILER_VERSION}"
    CXX="g++${COMPILER_VERSION:+-$COMPILER_VERSION}"
fi

function coverity_install_script {
    set +x # This is supposed to hide COVERITY_SCAN_TOKEN
    local platform=$(uname)
    local tool_url="https://scan.coverity.com/download/${platform}"
    local tool_archive="/tmp/cov-analysis-${platform}.tgz"

    echo -e "\033[33;1mDownloading Coverity Scan Analysis Tool...\033[0m"
    wget -nv -O $tool_archive $tool_url --post-data "project=$COVERITY_SCAN_PROJECT_NAME&token=$COVERITY_SCAN_TOKEN" || return

    echo -e "\033[33;1mExtracting Coverity Scan Analysis Tool...\033[0m"
    mkdir -p $COVERITY_SCAN_TOOL_BASE
    pushd $COVERITY_SCAN_TOOL_BASE
    tar xzf $tool_archive || return
    popd
    set -x
}

function run_coverity {
    set +x # This is supposed to hide COVERITY_SCAN_TOKEN
    local results_dir="cov-int"
    local tool_dir=$(find $COVERITY_SCAN_TOOL_BASE -type d -name 'cov-analysis*')
    local results_archive="analysis-results.tgz"
    local sha=$(git rev-parse --short HEAD)
    local author_email=$(git log -1 --pretty="%aE")
    local response status_code

    echo -e "\033[33;1mRunning Coverity Scan Analysis Tool...\033[0m"
    COVERITY_UNSUPPORTED=1 $tool_dir/bin/cov-build --dir $results_dir sh -c "make -j && make -j check-programs" || return
    $tool_dir/bin/cov-import-scm --dir $results_dir --scm git --log $results_dir/scm_log.txt || return

    echo -e "\033[33;1mTarring Coverity Scan Analysis results...\033[0m"
    tar czf $results_archive $results_dir || return

    echo -e "\033[33;1mUploading Coverity Scan Analysis results...\033[0m"
    response=$(curl \
               --silent --write-out "\n%{http_code}\n" \
               --form project=$COVERITY_SCAN_PROJECT_NAME \
               --form token=$COVERITY_SCAN_TOKEN \
               --form email=$author_email \
               --form file=@$results_archive \
               --form version=$sha \
               --form description="Daily build" \
               https://scan.coverity.com/builds)
    printf "\033[33;1mThe response is\033[0m\n%s\n" "$response"
    status_code=$(echo "$response" | sed -n '$p')
    if [ "$status_code" != "200" ]; then
        echo -e "\033[33;1mCoverity Scan upload failed: $(echo "$response" | sed '$d').\033[0m"
        return 1
    fi

    echo -e "\n\033[33;1mCoverity Scan Analysis completed successfully.\033[0m"
    set -x
}

for phase in "${PHASES[@]}"; do
    case $phase in
    CONFIGURE)
        opts=(
            --disable-use-tty-group
            --disable-makeinstall-chown
            --enable-all-programs
            --enable-werror
        )

        if [[ "$COVERAGE" == "yes" ]]; then
            CFLAGS+=(--coverage)
            CXXFLAGS+=(--coverage)
            LDFLAGS+=(--coverage)
        fi

        if [[ "$SANITIZE" == "yes" ]]; then
            opts+=(--enable-asan --enable-ubsan)
            CFLAGS+=(-fno-omit-frame-pointer)
            CXXFLAGS+=(-fno-omit-frame-pointer)
        fi

        if [[ "$COMPILER" == clang* && "$SANITIZE" == "yes" ]]; then
            opts+=(--enable-fuzzing-engine)
            CFLAGS+=(-shared-libasan)
            CXXFLAGS+=(-shared-libasan)
        fi

        git clean -xdf

        ./autogen.sh
        CC="$CC" CXX="$CXX" CFLAGS="${CFLAGS[@]}" CXXFLAGS="${CXXFLAGS[@]}" LDFLAGS="${LDFLAGS[@]}" ./configure "${opts[@]}"
        ;;
    MAKE)
        make -j"$(nproc)"
        make -j"$(nproc)" check-programs
        ;;
    INSTALL)
        make install DESTDIR=/tmp/dest
        ;;
    MESONCONF)
        meson build
        ;;
    MESONBUILD)
        ninja -C build
        ;;
    CODECHECK)
	make checklibdoc
	make checkxalloc
	;;
    CHECK)
        if [[ "$SANITIZE" == "yes" ]]; then
            # All the following black magic is to make test/eject/umount work, since
            # eject execl()s the uninstrumented /bin/umount binary, which confuses
            # ASan. The workaround for this is to set $LD_PRELOAD to the ASan's
            # runtime DSO, which works well with gcc without any additional hassle.
            # However, since clang, by default, links ASan statically, we need to
            # explicitly state we want dynamic linking (see -shared-libasan above).
            # That, however, introduces another issue - clang's ASan runtime is in
            # a non-standard path, so all binaries compiled in such way refuse
            # to start. That's what the following blob of code is for - it detects
            # the ASan's runtime path and adds the respective directory to
            # the dynamic linker cache.
            #
            # The actual $LD_PRELOAD sheanigans are done directly in
            # tests/ts/eject/umount.
            asan_rt_name="$(ldd ./kill | awk '/lib.+asan.*.so/ {print $1; exit}')"
            asan_rt_path="$($CC --print-file-name "$asan_rt_name")"
            echo "Detected ASan runtime: $asan_rt_name ($asan_rt_path)"
            if [[ -z "$asan_rt_name" || -z "$asan_rt_path" ]]; then
                echo >&2 "Couldn't detect ASan runtime, can't continue"
                exit 1
            fi

            if [[ "$COMPILER" == clang* ]]; then
                mkdir -p /etc/ld.so.conf.d/
                echo "${asan_rt_path%/*}" > /etc/ld.so.conf.d/99-clang-libasan.conf
                ldconfig
            fi
        fi

        ./tests/run.sh --show-diff

        if [[ "$COVERAGE" == "yes" ]]; then
            lcov --directory . --capture --initial --output-file coverage.info.initial
            lcov --directory . --capture --output-file coverage.info.run --no-checksum --rc lcov_branch_coverage=1
            lcov -a coverage.info.initial -a coverage.info.run --rc lcov_branch_coverage=1 -o coverage.info.raw
            lcov --extract coverage.info.raw "$(pwd)/*" --rc lcov_branch_coverage=1 --output-file coverage.info
        fi
        ;;
    DISTCHECK)
        make distcheck
        ;;
    COVERITY)
        coverity_install_script
        run_coverity
        ;;

    *)
        echo >&2 "Unknown phase '$phase'"
        exit 1
    esac
done
