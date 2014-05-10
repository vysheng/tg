Name:       	telegram-cli
Version:	0.1
Release:	3%{?dist}
Summary:	Private fast and open platform for instant messaging

Packager: 	Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
Group:		Internet/Messaging
License:	GPLv2
URL:		https://github.com/koter84/tg
Source:		https://github.com/koter84/tg/archive/master.zip

BuildRequires:	lua-devel, openssl-devel, libconfig-devel, readline-devel

%description
Telegram is an Open Source messaging platform for mobile, desktop focused on privacy.

%prep
[ -f %{sources} ] || 	wget -O %{sources} https://github.com/koter84/tg/archive/master.zip
[ -d %{name} ] && rm -Rfv %{name}
mkdir %{name}
cd %{name}
unzip %{sources}
cd tg-master
./configure

%build
cd %{name}
cd tg-master
%{__make} %{?_smp_mflags}

%install
cd %{name}
cd tg-master
%{__install} -D -m0755 telegram %{buildroot}%{_bindir}/telegram
%{__install} -D -m0644 tg-server.pub %{buildroot}%{_sysconfdir}/telegram/server.pub
#%{__install} -D -m0644 rpm/telegram-cli.repo %{buildroot}%{_sysconfdir}/yum.repos.d/telegram-cli.repo

%files
%{_bindir}/telegram
%{_sysconfdir}/telegram/server.pub
#%config %{_sysconfdir}/yum.repos.d/telegram-cli.repo


%changelog
* Sat May 10 2014 Dennis Koot (koter84@gmail.com)
- Fixed building packages on Fedora
* Sun Feb 23 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add repo definition and increase rpm spec version
* Sun Feb 16 2014 Iavael (iavaelooeyt@gmail.com)
- Prettified spec file
* Thu Feb 13 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Adapt spec file to be more compliant with Fedora Packaging Guidelines
* Tue Feb 4 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add server key to /etc/telegram/
* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file
