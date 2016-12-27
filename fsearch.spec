Name: fsearch
Version: git.2016.12.03.12.55
Release: alt1

Summary: FSearch is a fast file search utility for GNU/Linux operating systems, inspired by Everything Search Engine. It's written in C and based on GTK+3

License: GPLv2+
Group: File tools
Url: https://github.com/cboxdoerfer/fsearch

Packager: Sample Maintainer <samplemaintainer@altlinux.org>
BuildPreReq: libpcre-devel glib2-devel libgtk+3-devel xml-utils libxml2-devel intltool
Source: %name-%version.tar
Patch0: %name-po-ru-new.patch
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
%patch0 -p1
%patch1 -p0

%build

# put new translation for russian and tell into po/LINGUAS
pushd po
# .altnew is created by patch
mv ru.po.altnew ru.po
popd
echo -en "\\nru\\n" >> po/LINGUAS


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
* Sat Dec 03 2016 Sample Maintainer <samplemaintainer@altlinux.org> git.2016.12.03.12.55-alt1
- initial build

