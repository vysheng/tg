## Telegram messenger CLI

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### API, Protocol documentation

Documentation for Telegram API is available here: http://core.telegram.org/api

Documentation for MTproto protocol is available here: http://core.telegram.org/mtproto

### Installation

#### Linux

Just run

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
    
#### Other UNIX

If you manage to launch it on other UNIX, please let me know.


### Usage

    ./telegram -k <public-server-key>
    
By default public key is stored in the same folder named tg.pub:

    ./telegram -k tg.pub
  
#### Supported commands:

* **msg** \<peer\> Text - sends message to this peer
* **contact_list** - prints info about users in your contact list
* **stats** - just for debugging 
* **history** \<peer\> [limit] - prints history (and marks it as read). Default limit = 40
* **dialog_list** - prints info about your dialogs
* **send_photo** \<peer\> \<photo-file-name\> - sends photo to peer
* **send_video** \<peer\> \<video-file-name\> - sends video to peer
* **send_text** \<peer\> \<text-file-name> - sends text file as plain messages
* **chat_info** \<chat\> - prints info about chat
* **user_info** \<user\> - prints info about user
* **fwd** \<user\> \<msg-seqno\> - forward message to user. You can see message numbers starting client with -N
* **chat_add_user** \<chat\> \<user\> - add user to chat
* **chat_del_user** \<chat\> \<user\> - remove user from chat
* **rename_chat** \<chat\> \<new-name\>
* **load_photo**/load_video/load_video_thumb \<msg-seqno\> - loads photo/video to download dir
* **view_photo**/view_video/view_video_thumb \<msg-seqno\> - loads photo/video to download dir and starts system default viewer
* **search** \<peer\> pattern - searches pattern in messages with peer
* **global_search** pattern - searches pattern in all messages
* **mark_read** \<peer\> - mark read all received messages with peer
* **add_contact** \<phone-number\> \<first-name\> \<last-name\> - tries to add contact to contact-list by phone
* **create_secret_chat** \<user\> - creates secret chat with this user
* **rename_contact** \<user\> \<first-name\> \<last-name\> - tries to rename contact. If you have another device it will be a fight
* **suggested_contacts** - print info about contacts, you have max common friends
* **visualize_key** \<secret_chat\> - prints visualization of encryption key. You should compare it to your partner's one
* **show_license** - prints contents of GPLv2
* **help** - prints this help
