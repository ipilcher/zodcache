Name:		zodcache
Summary:	Device mapper (dm-cache)-based block device caching
Version:	0.0.2
Release:	1%{?dist}
URL:		https://github.com/ipilcher/zodcache
Source:		%{name}-%{version}.tar.gz
License:	GPLv2
BuildRequires:	device-mapper-devel libuuid-devel

%description
zodcache is a block device caching mechanism that uses device mapper
(dm-cache).

%prep
%setup -q

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
mkdir -p %{buildroot}/usr/lib/dracut/modules.d/90zodcache
cp module-setup.sh %{buildroot}/usr/lib/dracut/modules.d/90zodcache/
mkdir -p %{buildroot}/etc/dracut.conf.d
cp 50-zodcache.conf %{buildroot}/etc/dracut.conf.d/

%clean
rm -rf %{buildroot}

%files
%attr(0755,root,root) /usr/sbin/mkzc
%attr(0755,root,root) /usr/sbin/zcdump
%attr(0755,root,root) /usr/sbin/zcstart
%attr(0644,root,root) /usr/lib/udev/rules.d/69-zodcache.rules
%attr(0755,root,root) %dir /usr/lib/dracut/modules.d/90zodcache
%attr(0755,root,root) /usr/lib/dracut/modules.d/90zodcache/module-setup.sh
%attr(0644,root,root) %config(noreplace) /etc/dracut.conf.d/50-zodcache.conf
%attr(0644,root,root) %doc LICENSE

%changelog
* Tue Dec 27 2016 Ian Pilcher <arequipeno@gmail.com> - 0.0.2-1
- Fix metadata size calculation
- Put metadata region at end of combined cache device
- Add alignment (-a) option to mkzc
- Change device names for easier LVM filtering

* Thu Dec 17 2015 Ian Pilcher <arequipeno@gmail.com> - 0.0.1-2
- Add dracut module

* Wed Dec 16 2015 Ian Pilcher <arequipeno@gmail.com> - 0.0.1-1
- Initial SPEC file
