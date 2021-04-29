Build requirements
------------------

* lua-devel
* openssl-devel
* libconfig-devel
* readline-devel
* libevent-devel
* gcc
* git
* rpm-build


Building fresh RPMs
-------------------

Install build dependencies:
```
yum install git lua-devel openssl-devel libconfig-devel readline-devel libevent-devel gcc rpm-build
```
Clone the repo: 
```
git clone --recursive https://github.com/vysheng/tg.git
```
Build telegram-cli RPM:
```
cd tg/rpm
./telegram-cli-build-rpm
```
And install:
```
rpm -Uvh RPMS/$HOSTTYPE/telegram-cli-Beta-*.$HOSTTYPE.rpm
```
A SRPM will be generated at SRPMS/
