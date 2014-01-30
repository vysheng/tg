## Telegram messenger CLI [![Build Status](https://travis-ci.org/vysheng/tg.png)](https://travis-ci.org/vysheng/tg)

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### API, Protocol documentation

Documentation for Telegram API is available here: http://core.telegram.org/api

Documentation for MTproto protocol is available here: http://core.telegram.org/mtproto

### Installation

Clone GitHub Repository

    $ git clone https://github.com/vysheng/tg.git && cd tg
    
or download and extract zip

    $ wget https://github.com/vysheng/tg/archive/master.zip -O tg-master.zip
    $ unzip tg-master.zip && cd tg-master
    
#### Linux

Install libs: readline openssl and (if you want to use config) libconfig and liblua.
If you do not want to use them pass options --disable-libconfig and --disable-liblua respectively
On ubuntu use: 
    
    $ sudo apt-get install libreadline-dev libconfig-dev libssl-dev lua5.2 liblua5.2-dev
On gentoo:
    
    $ sudo emerge -av sys-libs/readline dev-libs/libconfig dev-libs/openssl dev-lang/lua
    
Default Makefile uses liblua5.2 from ubuntu. If you use different version of liblua or linux you have to run ./configure script or you will get some strange compilation error. 

Then

    $ ./configure
    $ make


#### Mac OS X

The client depends on [readline library](http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html) and [libconfig](http://www.hyperrealm.com/libconfig/), which are not included in OS X by default. You have to install these libraries manually, e.g. using [Homebrew](http://brew.sh/).

    $ brew install libconfig
    $ brew install readline
    $ export CFLAGS="-I/usr/local/include -I/usr/local/Cellar/readline/6.2.4/include"
    $ export LDFLAGS="-L/usr/local/lib -L/usr/local/Cellar/readline/6.2.4/lib"
    $ ./configure && make
    
Thanks to [@jfontan](https://github.com/vysheng/tg/issues/3#issuecomment-28293731) for this solution.

#### FreeBSD

Install these ports:

* devel/libconfig
* devel/libexecinfo

Then build:

    $ env CC=clang CFLAGS=-I/usr/local/include LDFLAGS=-L/usr/local/lib ./configure
    $ make
    
#### Other UNIX

If you manage to launch it on other UNIX, please let me know.


### Usage

    ./telegram -k <public-server-key>
    
By default public key is stored in the same folder named tg.pub:

    ./telegram -k tg.pub

Client support TAB completion and command history.

Peer refers to the name of the contact and can be accessed by TAB completion.
  
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
