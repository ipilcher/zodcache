Name:		zodcache
Summary:	Device mapper (dm-cache)-based block device caching
Version:	0.0.1
Release:	1%{?dist}
URL:		https://github.com/ipilcher/zodcache
Source:		%{name}-%{version}.tar.gz
License:	GPLv2
BuildRequires:	device-mapper-devel libuuid-devel

%description
zodcache is a block device caching mechanism that uses device mapper
(dm-cache).

%prep
%setup -q -n %{name}

%build
gcc -O3 -Wall -Wextra -o mkzc mkzc.c lib.c -luuid
gcc -O3 -Wall -Wextra -o zcdump zcdump.c lib.c
gcc -O3 -Wall -Wextra -o zcstart zcstart.c lib.c -ldevmapper

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}/usr/sbin
cp mkzc zcdump zcstart %{buildroot}/usr/sbin/
mkdir -p %{buildroot}/usr/lib/udev/rules.d
cp 69-zodcache.rules %{buildroot}/usr/lib/udev/rules.d/

%clean
rm -rf %{buildroot}

%files
%attr(0755,root,root) /usr/sbin/mkzc
%attr(0755,root,root) /usr/sbin/zcdump
%attr(0755,root,root) /usr/sbin/zcstart
%attr(0644,root,root) /usr/lib/udev/rules.d/69-zodcache.rules
%attr(0644,root,root) %doc LICENSE

%changelog
* Wed Dec 16 2015 Ian Pilcher <arequipeno@gmail.com> - 0.0.1-1
- Initial SPEC file
