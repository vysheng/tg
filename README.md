## Telegram messenger CLI [![Build Status](https://travis-ci.org/vysheng/tg.png)](https://travis-ci.org/vysheng/tg)

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### API, Protocol documentation

Documentation for Telegram API is available here: http://core.telegram.org/api

Documentation for MTproto protocol is available here: http://core.telegram.org/mtproto

### Installation

Clone GitHub Repository

     git clone https://github.com/vysheng/tg.git && cd tg

or download and extract zip

     wget https://github.com/vysheng/tg/archive/master.zip -O tg-master.zip
     unzip tg-master.zip && cd tg-master

#### Linux and BSDs

Install libs: readline or libedit, openssl and (if you want to use config) libconfig and liblua.
If you do not want to use them pass options --disable-libconfig and --disable-liblua respectively.

On ubuntu use: 

     sudo apt-get install libreadline-dev libconfig-dev libssl-dev lua5.2 liblua5.2-dev

On gentoo:

     sudo emerge -av sys-libs/readline dev-libs/libconfig dev-libs/openssl dev-lang/lua

On Fedora:

     sudo yum install lua-devel openssl-devel libconfig-devel readline-devel
     
On Arch Linux:
   Can be installed via AUR

     $ yaourt -S telegram-git    

On FreeBSD:

     pkg install libconfig libexecinfo lua52

On OpenBSD:

     pkg_add libconfig libexecinfo lua

Then,

     ./configure
     make

#### Mac OS X

The client depends on [readline library](http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html) and [libconfig](http://www.hyperrealm.com/libconfig/), which are not included in OS X by default. You have to install these libraries manually, e.g. using [Homebrew](http://brew.sh/).

     brew install libconfig
     brew install readline
     brew install lua
     export CFLAGS="-I/usr/local/include -I/usr/local/Cellar/readline/6.2.4/include"
     export LDFLAGS="-L/usr/local/lib -L/usr/local/Cellar/readline/6.2.4/lib"
     ./configure && make

Thanks to [@jfontan](https://github.com/vysheng/tg/issues/3#issuecomment-28293731) for this solution.


Install these ports:

* devel/libconfig
* devel/libexecinfo
* lang/lua52

Then build:

     env CC=clang CFLAGS=-I/usr/local/include LDFLAGS=-L/usr/local/lib LUA=/usr/local/bin/lua52 LUA_INCLUDE=-I/usr/local/include/lua52 LUA_LIB=-llua-5.2 ./configure
     make

#### Other UNIX

If you manage to launch it on other UNIX, please let me know.

### Usage

    ./telegram -k <public-server-key>
    
By default public key is stored in the same folder named tg-server.pub
