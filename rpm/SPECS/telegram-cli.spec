Name:       	telegram-cli
Version:	Beta
Release:	3%{?dist}
Summary:	Private fast and open platform for instant messaging

Packager: 	Julio González Gil <git@juliogonzalez.es>
Group:		Internet/Messaging
License:	GPL
URL:		https://github.com/vysheng/tg
Source:		telegram-cli.zip

BuildRequires:  lua-devel, openssl-devel, libconfig-devel, readline-devel, libevent-devel, gcc, git

%description
Telegram is an Open Source messaging platform for mobile, desktop focused on privacy.

%prep
[ -d %{name} ] && rm -Rfv %{name}
git clone --recursive --depth=1 https://github.com/vysheng/tg.git telegram-cli
zip -r telegram-cli.zip telegram-cli
mv telegram-cli.zip %{_sourcedir}
cd %{name}
./configure
make %{?_smp_mflags}

%install
cd %{name}
%{__install} -D -m0755 bin/telegram-cli %{buildroot}/usr/bin/telegram-cli
%{__install} -D -m0644 server.pub %{buildroot}/etc/telegram/server.pub

%files
/usr/bin/telegram-cli
/etc/telegram/server.pub

%changelog
* Wed May 13 2015 Julio González Gil (git@juliogonzalez.com)
- Build was not working: Fix it to use latest available code from GitHub

* Tue Feb 4 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add server key to /etc/telegram/

* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file
