## Telegram messenger CLI [![Build Status](https://travis-ci.org/vysheng/tg.png)](https://travis-ci.org/vysheng/tg)

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### API, Protocol documentation

Documentation for Telegram API is available here: http://core.telegram.org/api

Documentation for MTproto protocol is available here: http://core.telegram.org/mtproto

### Upgrading to version 1.0

First of all, the binary is now in ./bin folder and is named telegram-cli. So be careful, not to use old binary.

Second, config folder is now ${HOME}/.telegram-cli

Third, database is not compatible with older versions, so you'll have to login again.

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

     sudo apt-get install libreadline-dev libconfig-dev libssl-dev lua5.2 liblua5.2-dev libevent-dev

On gentoo:

     sudo emerge -av sys-libs/readline dev-libs/libconfig dev-libs/openssl dev-lang/lua dev-libs/libevent

On Fedora:

     sudo yum install lua-devel openssl-devel libconfig-devel readline-devel

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

    bin/telegram-cli -k <public-server-key>
    
By default public key is stored in the same folder named tg-server.pub or in /etc/telegram-cli/server.pub, if it's not, specify where to find it:

    bin/telegram-cli -k tg-server.pub

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
