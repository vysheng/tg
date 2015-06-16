### Installation on Windows
To use telegram-cli in Windows, you should compile with Cygwin which has POSIX API functionality.

Install [Cygwin](https://www.cygwin.com/) and cygwin's package manager, [apt-cyg](https://github.com/transcode-open/apt-cyg).

In Cygwin Terminal, install compiler and tools :

     apt-cyg install cygwin32-gcc-core cygwin32-gcc-g++ gcc-core gcc-g++ make wget patch diffutils grep tar gzip

Now you have a compiler, but no libraries. You need readline, openssl, libconfig, liblua, python and libjansson to use telegram-cli's full functionality.


Then Clone GitHub Repository in Cygwin Terminal

     git clone --recursive https://github.com/vysheng/tg.git


In Cygwin Terminal, type: 

     apt-cyg install libevent-devel openssl-devel libreadline-devel lua-devel python3
(Install package 'python' to use Python 2.7, or install package 'python3' to use Python 3)

libconfig and libjansson is not in cygwin's package, so you should compile yourself.

Compile libconfig
     
     wget http://www.hyperrealm.com/libconfig/libconfig-1.5.tar.gz
     tar xvf libconfig-1.5.tar.gz && cd libconfig-1.5
     ./configure
     make && make install && cd ..

Compile libjansson

     wget http://www.digip.org/jansson/releases/jansson-2.7.tar.gz
     tar xvf jansson-2.7.tar.gz && cd jansson-2.7
     ./configure
     make && make install && cd ..

Then, go to tg directory then generate Makefile.

     cd tg
     ./configure
     
We need to patch Makefile and loop.c to compile in cygwin. Download this [patch](https://gist.github.com/ied206/d774a445f36004d263ab) then untar. Then, patch in tg directory.

     patch -p1 < telegram-cli-cygwin.patch

Then
     make

After compile is done, **telegram-cli.exe** will be generated in **bin** directory.

To run telegram-cli, type
     
     bin/telegram-cli -k tg-server.pub

**Caution**: A binary compiled with Cygwin should be run in Cygwin Terminal.
