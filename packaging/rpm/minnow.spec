Name:           minnow
Version:        0.1.1
Release:        1%{?dist}
Summary:        A simple, lightweight file manager for KDE

License:        GPL-3.0-or-later
URL:            https://github.com/minnowfm/minnow
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  extra-cmake-modules
BuildRequires:  qt6-qtbase-devel
BuildRequires:  kf6-kcoreaddons-devel
BuildRequires:  kf6-kconfigwidgets-devel
BuildRequires:  kf6-kwidgetsaddons-devel
BuildRequires:  kf6-kio-devel
BuildRequires:  kf6-kfilemetadata-devel
BuildRequires:  kf6-karchive-devel
BuildRequires:  kf6-knotifications-devel
BuildRequires:  kf6-kwindowsystem-devel

%description
Minnow is a small, KIO-based file browser built as a lighter alternative
to Dolphin: grid and list views, a customizable places sidebar, and
standard file operations, without the extra panels and configuration
surface of a full-featured file manager.

%prep
%autosetup

%build
%cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DMINNOW_APP_VERSION=%{version}
%cmake_build

%install
%cmake_install

%files
%{_bindir}/minnow
%{_datadir}/applications/io.github.minnowfm.Minnow.desktop
%{_datadir}/icons/hicolor/scalable/apps/io.github.minnowfm.Minnow.svg
%{_datadir}/icons/hicolor/16x16/apps/io.github.minnowfm.Minnow.png
%{_datadir}/icons/hicolor/22x22/apps/io.github.minnowfm.Minnow.png
%{_datadir}/icons/hicolor/24x24/apps/io.github.minnowfm.Minnow.png
%{_datadir}/icons/hicolor/32x32/apps/io.github.minnowfm.Minnow.png
%{_datadir}/icons/hicolor/48x48/apps/io.github.minnowfm.Minnow.png
%{_datadir}/icons/hicolor/64x64/apps/io.github.minnowfm.Minnow.png
%{_datadir}/icons/hicolor/128x128/apps/io.github.minnowfm.Minnow.png
%{_datadir}/metainfo/io.github.minnowfm.Minnow.metainfo.xml

%changelog
* Fri Jul 24 2026 Minnow Contributors <noreply@example.com> - 0.1.1-1
- Rename app ID to io.github.minnowfm.Minnow.

* Fri Jul 24 2026 Minnow Contributors <noreply@example.com> - 0.1.0-1
- Initial release.
