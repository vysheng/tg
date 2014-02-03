Name:       Telegram
Version:	Beta
Release:	1%{?dist}
Summary:	Private fast and open platform for instant messaging

Group:		Internet/Messaging
License:	GPL
URL:		https://github.com/vysheng/tg
Source:		master.zip

BuildRequires:	lua-devel, openssl-devel, libconfig-devel, readline-devel
Requires:	wget

%description
Telegram is an Open Source messaging platform for mobile, desktop focused on privacy.

Packager: Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)


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

%files
/usr/bin/telegram


%changelog
* Sat Feb 1 2014 Pablo Iranzo Gómez (Pablo.Iranzo@gmail.com)
- Initial SPEC file

