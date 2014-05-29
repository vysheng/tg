## Telegram messenger CLI [![Build Status](https://travis-ci.org/koter84/tg.svg?branch=master)](https://travis-ci.org/koter84/tg) [![Coverity Scan](https://scan.coverity.com/projects/2233/badge.svg)](https://scan.coverity.com/projects/2233/)

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### API, Protocol documentation

Documentation for Telegram API is available here: http://core.telegram.org/api

Documentation for MTproto protocol is available here: http://core.telegram.org/mtproto

### Download Binaries

After Travis-CI is done testing it uploads it's build artifacts to https://tg-koter84.rhcloud.com

For now there is a Mac OS X and a Ubuntu build environment. The Ubuntu build also generates a installable amd64 .deb file.

On my ToDo-list is building an .rpm on OpenShift, build a Mac OS X package, and maybe put some effort in building 32bit versions.

### Installation

Clone GitHub Repository

    $ git clone https://github.com/koter84/tg.git && cd tg

or download and extract zip

    $ wget https://github.com/koter84/tg/archive/master.zip -O tg-master.zip
    $ unzip tg-master.zip && cd tg-master

#### Linux and BSDs

Install libs: readline or libedit, openssl and (if you want to use config) libconfig and liblua.
If you do not want to use them pass options --disable-libconfig and --disable-liblua respectively.

On Debian/Ubuntu:

    $ sudo apt-get install libreadline-dev libconfig-dev libssl-dev lua5.2 liblua5.2-dev

On Gentoo:

    $ sudo emerge -av sys-libs/readline dev-libs/libconfig dev-libs/openssl dev-lang/lua

On Fedora:

    $ sudo yum install lua-devel openssl-devel libconfig-devel readline-devel

On ArchLinux:

    $ sudo pacman -S base-devel readline libconfig openssl lua

On FreeBSD:

    pkg install libconfig libexecinfo lua52

On OpenBSD:

    pkg_add libconfig libexecinfo lua

Then, on all Linux and BSDs

    $ ./configure
    $ make
    $ sudo make install

#### Mac OS X

The client depends on [readline library](http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html) and [libconfig](http://www.hyperrealm.com/libconfig/), which are not included in OS X by default. Lua is optional. You have to install these libraries manually. 

Using [Homebrew](http://brew.sh/).
Thanks to [@jfontan](https://github.com/vysheng/tg/issues/3#issuecomment-28293731) for this solution.

    $ brew install libconfig readline lua
    $ ./configure
    $ make

Using MacPorts
Thanks to [@p3k](https://github.com/vysheng/tg/issues/129) for this solution. [extra-link](https://blog.p3k.org/stories/8350/)

    $ sudo port install libconfig-hr readline lua
    $ export CFLAGS=-I/opt/local/include
    $ export LDFLAGS=-L/opt/local/lib
    $ ./configure
    $ sed -i.bak 's/-rdynamic //g' Makefile
    $ make

#### Other UNIX

If you manage to launch it on other UNIX, please let me know.

### Building Packages

On Fedora

    rpmbuild -ba telegram-cli.spec

On Debian/Ubuntu

    dpkg-buildpackage -B

### Usage

    ./telegram
    
By default public key is stored in the same folder named tg-server.pub or in /etc/telegram/server.pub, if it's not, specify where to find it:

    ./telegram -k <public-server-key>

Client support TAB completion and command history.

Peer refers to the name of the contact or dialog and can be accessed by TAB completion.
For user contacts peer name is Name <underscore> Lastname with all spaces changed to underscores.
For chats it is it's title with all spaces changed to underscores
For encrypted chats it is <Exсlamation mark> <underscore> Name <underscore> Lastname with all spaces changed to underscores. 

If two or more peers have same name, <sharp>number is appended to the name. (for example A_B, A_B#1, A_B#2 and so on)
  
### Supported commands

#### Messaging

* **msg** \<peer\> Text - sends message to this peer
* **fwd** \<user\> \<msg-seqno\> - forward message to user. You can see message numbers starting client with -N
* **chat_with_peer** \<peer\> starts one on one chat session with this peer. /exit or /quit to end this mode.
* **add_contact** \<phone-number\> \<first-name\> \<last-name\> - tries to add contact to contact-list by phone
* **rename_contact** \<user\> \<first-name\> \<last-name\> - tries to rename contact. If you have another device it will be a fight
* **mark_read** \<peer\> - mark read all received messages with peer

#### Multimedia

* **send_photo** \<peer\> \<photo-file-name\> - sends photo to peer
* **send_video** \<peer\> \<video-file-name\> - sends video to peer
* **send_text** \<peer\> \<text-file-name> - sends text file as plain messages
* **load_photo**/load_video/load_video_thumb \<msg-seqno\> - loads photo/video to download dir
* **view_photo**/view_video/view_video_thumb \<msg-seqno\> - loads photo/video to download dir and starts system default viewer

#### Group chat options

* **chat_info** \<chat\> - prints info about chat
* **chat_add_user** \<chat\> \<user\> - add user to chat
* **chat_del_user** \<chat\> \<user\> - remove user from chat
* **rename_chat** \<chat\> \<new-name\>
* **create_group_chat** \<user\> \<chat topic\> - creates a groupchat with user, use chat_add_user to add more users

#### Search

* **search** \<peer\> pattern - searches pattern in messages with peer
* **global_search** pattern - searches pattern in all messages

#### Secret chat

* **create_secret_chat** \<user\> - creates secret chat with this user
* **visualize_key** \<secret_chat\> - prints visualization of encryption key. You should compare it to your partner's one

#### Stats and various info

* **user_info** \<user\> - prints info about user
* **history** \<peer\> [limit] - prints history (and marks it as read). Default limit = 40
* **dialog_list** - prints info about your dialogs
* **contact_list** - prints info about users in your contact list
* **suggested_contacts** - print info about contacts, you have max common friends
* **stats** - just for debugging
* **show_license** - prints contents of GPLv2
* **help** - prints this help
