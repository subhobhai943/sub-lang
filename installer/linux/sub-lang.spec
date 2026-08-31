# RPM package for Fedora, RHEL, CentOS Stream, openSUSE and derivatives.
#
# Built by installer/linux/build-rpm.sh, which makes the source tarball this
# expects and calls rpmbuild. The file layout comes from `make install
# PREFIX=/usr`, the same recipe the .deb uses, so the two packages cannot
# put the standard library in different places.

# The binaries are built without -g, so the debuginfo subpackage would be
# empty -- and an empty one is a build failure, not a no-op.
%global debug_package %{nil}

Name:           sub-lang
Version:        %{?_sub_version}%{!?_sub_version:0.0.0}
Release:        1%{?dist}
Summary:        SUB programming language toolchain

License:        MIT
URL:            https://github.com/subhobhai943/sub-lang
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
Requires:       glibc
Recommends:     gcc

%description
SUB is a small language with one specification and many backends: the
interpreter defines what a program prints and what status it exits with,
and every compiler and transpiler is tested against it.

This package installs three tools:

  * subi - the interpreter and REPL, and the language specification
  * subc - the native compiler; on x86-64 it emits machine code and writes
    the ELF itself, with no C toolchain involved
  * sub  - the transpiler, to thirteen other languages

gcc is recommended, not required: subc only needs a C compiler for the
programs its machine-code backend cannot build yet.

%prep
%setup -q

%build
# Not %%make_build: the distribution's %%optflags include -Werror-prone
# hardening flags this project has not been through yet, and CFLAGS is
# assigned rather than appended in the Makefile, so passing them would
# silently drop the include paths.
make all

%install
# PREFIX=/usr rather than the default /usr/local: the module resolver finds
# the standard library at ../lib/sub/stdlib relative to the binary, so
# /usr/bin/subi reads /usr/lib/sub/stdlib.
make install DESTDIR=%{buildroot} PREFIX=%{_prefix}

# %%license and %%doc place these themselves; leaving the copies the
# Makefile installed would list the same file twice.
rm -f %{buildroot}%{_docdir}/%{name}/LICENSE
rm -f %{buildroot}%{_docdir}/%{name}/README.md

%check
printf 'let x = 42\nprintln(x)\n' > smoke.sb
test "$(%{buildroot}%{_bindir}/subi smoke.sb)" = "42"

%files
%license LICENSE
%doc README.md
%{_bindir}/sub
%{_bindir}/subc
%{_bindir}/subi
%dir %{_prefix}/lib/sub
%dir %{_prefix}/lib/sub/stdlib
%{_prefix}/lib/sub/stdlib/*.sb
%{_mandir}/man1/sub.1*
%{_mandir}/man1/subc.1*
%{_mandir}/man1/subi.1*
%{_datadir}/applications/%{name}.desktop
%{_datadir}/mime/packages/%{name}-mime.xml
%{_datadir}/icons/hicolor/*/apps/%{name}.png
%{_datadir}/icons/hicolor/*/mimetypes/text-x-sub.png

# The desktop caches that have to be refreshed before a .sb file gets its
# icon. Each is guarded: none of these programs is a dependency, and the
# package must install on a machine with no desktop at all.
%post
if [ -x /usr/bin/update-desktop-database ]; then
    /usr/bin/update-desktop-database -q %{_datadir}/applications || :
fi
if [ -x /usr/bin/update-mime-database ]; then
    /usr/bin/update-mime-database %{_datadir}/mime >/dev/null 2>&1 || :
fi
if [ -x /usr/bin/gtk-update-icon-cache ]; then
    /usr/bin/gtk-update-icon-cache -qtf %{_datadir}/icons/hicolor || :
fi

%postun
if [ $1 -eq 0 ]; then
    if [ -x /usr/bin/update-desktop-database ]; then
        /usr/bin/update-desktop-database -q %{_datadir}/applications || :
    fi
    if [ -x /usr/bin/update-mime-database ]; then
        /usr/bin/update-mime-database %{_datadir}/mime >/dev/null 2>&1 || :
    fi
    if [ -x /usr/bin/gtk-update-icon-cache ]; then
        /usr/bin/gtk-update-icon-cache -qtf %{_datadir}/icons/hicolor || :
    fi
fi

%changelog
* Mon Aug 31 2026 SUB Language Project <noreply@github.com> - 1.0.9-1
- First RPM package. Same contents as the .deb, from the same make install.
