Name:       	telegram-cli
Version:	0.01
Release:	0.3%{?dist}
Summary:	Private fast and open platform for instant messaging

Packager: 	Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
Group:		Internet/Messaging
License:	GPLv2
URL:		https://github.com/vysheng/tg
Source0:	master.zip
nosource:	0

BuildRequires:	lua-devel, openssl-devel, libconfig-devel, readline-devel, wget
#Requires:	wget

%description
Telegram is an Open Source messaging platform for mobile, desktop focused on privacy.




%prep
[ -d %{name} ] && rm -Rfv %{name}
mkdir %{name}
cd %{name}
wget -O master.zip https://github.com/vysheng/tg/archive/master.zip
unzip master.zip
cd tg-master
./configure
make %{?_smp_mflags}


%install
cd %{name}
cd tg-master
%{__install} -D -m0755 telegram %{buildroot}%{_bindir}/telegram
%{__install} -D -m0644 tg.pub %{buildroot}%{_sysconfdir}/telegram/server.pub

%files
%{_bindir}/telegram
%{_sysconfdir}/telegram/server.pub

%changelog
* Thu Feb 13 2013 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com
- Adapt spec file to be more compliant with Fedora Packaging Guidelines
* Tue Feb 4 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add server key to /etc/telegram/
* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file
