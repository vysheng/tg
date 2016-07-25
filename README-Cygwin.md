### Installation on Windows
To use telegram-cli in Windows, you should compile with Cygwin which has POSIX API functionality.

Install [Cygwin](https://www.cygwin.com/).

In Cygwin Terminal, install cygwin's package manager, apt-cyg, as per apt-cyg's [project page](https://github.com/transcode-open/apt-cyg):

     lynx -source rawgit.com/transcode-open/apt-cyg/master/apt-cyg > apt-cyg
     install apt-cyg /bin
     
Then install compiler and tools: if you're on the **32-bit** version of cygwin,

     apt-cyg install gcc-core gcc-g++ gcc-core gcc-g++ make wget patch diffutils grep tar gzip
     
Whereas on the **64-bit** version,

     apt-cyg install cygwin32-gcc-core cygwin32-gcc-g++ gcc-core gcc-g++ make wget patch diffutils grep tar gzip zlib-devel

You need libraries *readline*, *openssl*, *libconfig*, *liblua*, *python* and *libjansson* to use telegram-cli's full functionality.

Clone this project's GitHub Repository in Cygwin Terminal

     git clone --recursive https://github.com/vysheng/tg.git

Then type: 

     apt-cyg install libevent-devel openssl-devel libreadline-devel lua-devel python3

(Install package 'python' to use Python 2.7, or install package 'python3' to use Python 3)

libconfig and libjansson are not included in the cygwin package, so you should compile them yourself.

Compile libconfig:
     
     wget http://www.hyperrealm.com/libconfig/libconfig-1.5.tar.gz
     tar xvf libconfig-1.5.tar.gz && cd libconfig-1.5
     ./configure
     make && make install && cd ..

Compile libjansson:

     wget http://www.digip.org/jansson/releases/jansson-2.7.tar.gz
     tar xvf jansson-2.7.tar.gz && cd jansson-2.7
     ./configure
     make && make install && cd ..

Then, change to the tg directory and generate the Makefile.

     cd tg
     ./configure

In case `configure` fails, it might be because of the CRLF line endings, so:

     dos2unix -f configure

And again,

     ./configure

We need to patch the Makefile and loop.c to compile properly in cygwin. The patch is included, so just type:

     patch -p1 < telegram-cli-cygwin.patch

Then,

     make

Once the compilation is complete, **telegram-cli.exe** will be found in the **bin** subdirectory.

To run `telegram-cli`, type
     
     bin/telegram-cli -k tg-server.pub

**Caution**: A binary compiled with Cygwin should be run in Cygwin Terminal.
