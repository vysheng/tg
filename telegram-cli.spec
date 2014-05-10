Name:       	telegram-cli
Version:	0.1
Release:	2%{?dist}
Summary:	Private fast and open platform for instant messaging

Packager: 	Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
Group:		Internet/Messaging
License:	GPL
URL:		https://github.com/vysheng/tg
Source:		https://github.com/koter84/tg/archive/master.zip

BuildRequires:	lua-devel, openssl-devel, libconfig-devel, readline-devel

%description
Telegram is an Open Source messaging platform for mobile, desktop focused on privacy.

%prep
%setup -n tg-master

%configure

%build
%{__make} %{?_smp_mflags}

%install
make DESTDIR=%{buildroot}/usr install
%{__install} -D -m0644 tg.pub %{buildroot}%{_sysconfdir}/telegram/server.pub

%files
%{_bindir}/telegram
%{_sysconfdir}/telegram/server.pub

%changelog
* Sun Feb 16 2014 Iavael (iavaelooeyt@gmail.com)
- Prettified spec file
* Tue Feb 4 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add server key to /etc/telegram/
* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file
