Name: fsearch
Version: git.2017.01.02.05.43
Release: alt2

Summary: FSearch is a fast file search utility for GNU/Linux operating systems, inspired by Everything Search Engine. It's written in C and based on GTK+3

License: GPLv2+
Group: File tools
Url: https://github.com/cboxdoerfer/fsearch

Packager: Sample Maintainer <samplemaintainer@altlinux.org>
BuildPreReq: libpcre-devel glib2-devel libgtk+3-devel xml-utils libxml2-devel intltool
Source: %name-%version.tar
Patch1: %name-configure-ac-remove-ax-check-compile-flags.patch

%description
FSearch is a fast file search utility for GNU/Linux
operating systems, inspired by Everything Search Engine.
It's written in C and based on GTK+3.

Note: The application is still in beta stage, but
will see its first release as soon as localization support has been added

Features:
    - Instant (as you type) results
    - Wildcard support
    - RegEx support
    - Filter support (only search for files, folders or everything)
    - Include and exclude specific folders to be indexed
    - Ability to exclude certain files/folders from
      index using wildcard expressions
    - Fast sort by filename, path, size or modification time
    - Customizable interface


%prep
%setup
%patch1 -p0

%build
./autogen.sh
%configure
%make_build

%install
%makeinstall_std

%find_lang %name


%files -f %name.lang
%doc README.md License
%_desktopdir/%name.desktop
%_bindir/*

%changelog
* Mon Jan 02 2017 Sample Maintainer <samplemaintainer@altlinux.org> git.2017.01.02.05.43-alt2
- rm .po/LINGUAS update from spec
* Mon Jan 02 2017 Sample Maintainer <samplemaintainer@altlinux.org> git.2017.01.02.05.43-alt1
- clone upstream git
* Sat Dec 03 2016 Sample Maintainer <samplemaintainer@altlinux.org> git.2016.12.03.12.55-alt1
- initial build

