Name:           momentum-anc
Version:        0.1.0
Release:        1%{?dist}
Summary:        Control noise cancellation on the Sennheiser MOMENTUM 4

License:        MIT
URL:            https://github.com/skurtz97/momentum-anc
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson
BuildRequires:  pkgconfig(gtk4) >= 4.16
BuildRequires:  pkgconfig(libadwaita-1) >= 1.6
BuildRequires:  bluez-libs-devel
Requires:       gtk4 >= 4.16
Requires:       libadwaita >= 1.6
Requires:       bluez-libs

%description
GTK 4 application that talks to a paired Sennheiser MOMENTUM 4 over Bluetooth
Classic (GAIA v3 over RFCOMM). It switches noise cancellation, transparency and
adaptive mode, and shows the battery level.

%prep
%autosetup

%build
%meson
%meson_build

%check
%meson_test

%install
%meson_install

%files
%{_bindir}/momentum-anc
%{_datadir}/applications/io.github.momentum_anc.desktop
%{_datadir}/metainfo/io.github.momentum_anc.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.momentum_anc.svg

%changelog
* Wed Nov 11 2026 skurtz97 <skurtz97@users.noreply.github.com> - 0.1.0-1
- Initial package
