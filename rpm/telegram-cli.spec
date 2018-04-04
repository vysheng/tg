Name:       	telegram-cli
Version:	1.4.1
Release:	0%{?dist}
Summary:	Private fast and open platform for instant messaging

Packager: 	Maksym Shkolnyi (maskimko@ukr.net)
Group:		Internet/Messaging
License:	GPL
URL:		https://github.com/vysheng/tg
Source:		telegram-cli-%{version}-fc27.tar.gz

BuildRequires:	lua-devel, openssl-devel, libconfig-devel, readline-devel, libevent-devel, jansson-devel, libgcrypt-devel


%description
Telegram is an Open Source messaging platform for mobile, desktop focused on privacy.




%prep
[ -d %{name} ] && rm -Rfv %{name}

%setup -q

%build
./configure --disable-openssl
make %{?_smp_mflags}


%install
%{__install} -D -m0755 bin/telegram-cli %{buildroot}/usr/bin/telegram-cli
%{__install} -D -m0644 tg-server.pub %{buildroot}/etc/telegram/server.pub

%files
/usr/bin/telegram-cli
/etc/telegram/server.pub

%changelog
* Wed Apr 04 2018 Maksym Shkolnyi <maskimko@ukr.net> - 1.4.1
- Updated Spec file
* Tue Feb 4 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add server key to /etc/telegram/
* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file
