Name:       	telegram-cli
Version:	Beta
Release:	2%{?dist}
Summary:	Private fast and open platform for instant messaging

Packager: 	Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
Group:		Internet/Messaging
License:	GPL
URL:		https://github.com/vysheng/tg
Source:		master.zip

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
%{__install} -D -m0755 telegram %{buildroot}/usr/bin/telegram
%{__install} -D -m0644 tg-server.pub %{buildroot}/etc/telegram/server.pub

%files
/usr/bin/telegram
/etc/telegram/server.pub

%changelog
* Tue Feb 4 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Add server key to /etc/telegram/
* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file
