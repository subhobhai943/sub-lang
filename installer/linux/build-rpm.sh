#!/bin/sh
# Build the RPM package for Fedora / RHEL / openSUSE.
#
#   installer/linux/build-rpm.sh <version> [outdir]
#   installer/linux/build-rpm.sh 1.0.9
#
# rpmbuild wants a source tarball whose top directory is name-version, so
# this makes one from the working tree and hands it over. The result is a
# binary RPM and the matching source RPM.
#
# Needs rpmbuild: `apt-get install rpm` on Debian and Ubuntu, `dnf install
# rpm-build` on Fedora.
set -eu

VERSION=${1:?usage: build-rpm.sh <version> [outdir]}
OUTDIR=${2:-.}

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
TOP="${ROOT}/build/rpm"
NAME=sub-lang

rm -rf "$TOP"
mkdir -p "$TOP/SOURCES" "$TOP/SPECS" "$TOP/BUILD" "$TOP/RPMS" "$TOP/SRPMS"

# The tracked files, as they are in the working tree. `git ls-files` keeps
# build artefacts and untracked scratch out of the tarball; reading them
# from the working tree rather than from HEAD means an uncommitted change
# is packaged rather than silently skipped, which is what you want when
# testing a packaging change before committing it.
git -C "$ROOT" ls-files -z \
    | tar -C "$ROOT" --null --files-from=- \
          --transform="s,^,${NAME}-${VERSION}/," \
          -czf "$TOP/SOURCES/${NAME}-${VERSION}.tar.gz"

cp "$ROOT/installer/linux/${NAME}.spec" "$TOP/SPECS/"

# On a Debian or Ubuntu host, gcc and make are installed but the rpm
# database has never heard of them, so BuildRequires fails on packages that
# are demonstrably present. Skip the check there; keep it on an RPM host,
# where it is meaningful. The BuildRequires lines stay in the spec either
# way -- they are what mock and dnf read when rebuilding the .src.rpm.
NODEPS=--nodeps
if rpm -q --whatprovides make >/dev/null 2>&1; then
    NODEPS=
fi

rpmbuild -ba "$TOP/SPECS/${NAME}.spec" \
    --define "_topdir $TOP" \
    --define "_sub_version $VERSION" \
    $NODEPS \
    >"$TOP/rpmbuild.log" 2>&1 || { cat "$TOP/rpmbuild.log" >&2; exit 1; }

mkdir -p "$OUTDIR"
find "$TOP/RPMS" "$TOP/SRPMS" -name '*.rpm' -exec cp {} "$OUTDIR/" \;
find "$OUTDIR" -name "${NAME}-${VERSION}-*.rpm" -print
