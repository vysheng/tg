## Telegram messenger CLI [![Build Status](https://travis-ci.org/vysheng/tg.png)](https://travis-ci.org/vysheng/tg)

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### Official Docs

> Documentation for the Telegram API is available [here](http://core.telegram.org/api)

> Documentation for the MTproto protocol is available [here](http://core.telegram.org/mtproto)

### Upgrade to the v1.0

First of all, the main binary is placed in `./bin` folder now and it was renamed to `telegram-cli`. So, be careful, and do not run an old binary.

Second thing is the configuration folder was moved to `${HOME}/.telegram-cli` now.

At the third, a new database structure is no more compatible with older versions. It means you would need to re-login one more time.

Fourth surprise waits you in a peer name. All the `#` symbols there had been substituted with `@` symbols (but it wasn't applied for appending of `#%d` in a case of two peers have the same names).

### Install

Clone this repository

```sh
git clone --recursive https://github.com/vysheng/tg.git && cd tg
```

### Python Support

Python support is currently limited by Python 2.7 or Python 3.1+. Other versions should work too, but hadn't been tested yet.

#### Linux and BSD

Install these libs:

- `readline`
- `openssl`
- `libconfig` (if you want to use config)
- `liblua`
- `python`
- `libjansson`

If you don't want to use them, you may skip its install and then you should pass next options to the `./configure` script respectively:

- `--disable-libconfig`
- `--disable-liblua`
- `--disable-python`
- `--disable-json`

For Ubuntu/Debian use:

```sh
sudo apt-get install libreadline-dev libconfig-dev libssl-dev lua5.2 \
    liblua5.2-dev libevent-dev libjansson-dev libpython-dev make
```

For Gentoo:

```sh
sudo emerge -av sys-libs/readline dev-libs/libconfig dev-libs/openssl \
    dev-lang/lua dev-libs/libevent dev-libs/jansson dev-lang/python
```

For Fedora:

```sh
sudo dnf install lua-devel openssl-devel libconfig-devel readline-devel \
    libevent-devel libjansson-devel python-devel
```

For Archlinux:

```sh
yaourt -S telegram-cli-git
```

For FreeBSD:

```sh
pkg install libconfig libexecinfo lua52 python
```

For OpenBSD:

```sh
pkg_add libconfig libexecinfo lua python
```

For OpenSUSE:

```sh
sudo zypper in lua-devel libconfig-devel readline-devel libevent-devel \
    libjansson-devel python-devel libopenssl-devel
```

---

Then run:

```sh
./configure
make
```

#### Other install methods for Linux

On Gentoo: use ebuild provided package.

On Arch: [telegram-cli-git](https://aur.archlinux.org/packages/telegram-cli-git)

#### Mac OS X

The client depends on [readline library](http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html) and [libconfig](http://www.hyperrealm.com/libconfig/), which are not included in OS X by the default. You should install these libraries manually.

If you are using [Homebrew](http://brew.sh/):

```sh
brew install libconfig readline lua python libevent jansson
export CFLAGS="-I/usr/local/include -I/usr/local/Cellar/readline/6.3.8/include"
export LDFLAGS="-L/usr/local/lib -L/usr/local/Cellar/readline/6.3.8/lib"
./configure
make
```

Thanks to [@jfontan](https://github.com/vysheng/tg/issues/3#issuecomment-28293731) for this solution.

If you are using [MacPorts](https://www.macports.org):

```sh
sudo port install libconfig-hr
sudo port install readline
sudo port install lua51
sudo port install python34
sudo port install libevent
export CFLAGS="-I/usr/local/include -I/opt/local/include -I/opt/local/include/lua-5.1"
export LDFLAGS="-L/usr/local/lib -L/opt/local/lib -L/opt/local/lib/lua-5.1"
./configure
make
```

Install these ports:

- `devel/libconfig`
- `devel/libexecinfo`
- `lang/lua52`

Then build:

```sh
env CC=clang CFLAGS=-I/usr/local/include LDFLAGS=-L/usr/local/lib \
    LUA=/usr/local/bin/lua52 LUA_INCLUDE=-I/usr/local/include/lua52 \
    LUA_LIB=-llua-5.2 ./configure
make
```

#### Other UNIX

If you would need to launch it inside any other UNIX environment, please let me know about.

### Contacts

If you would like to ask a question, you can write to my telegram or to the github (or both). For contact with me via telegram, you should use `import_card` method with an argument: `000653bf:0738ca5d:5521fbac:29246815:a27d0cda`

### Usage

```sh
bin/telegram-cli -k PUBLIC_SERVER_KEY
```

By the default the public key is stored in the `./tg-server.pub` file or either in `/etc/telegram-cli/server.pub`. If not, you should specify a path to your key manually:

```sh
bin/telegram-cli -k tg-server.pub
```

The client supports `TAB` completion and the history of commands.

The peer name refers to the name of a chat and can be accessed with `TAB` completion from the cli app.

- For any private chat the peer name should match a pattern `{FIRST_NAME}_{LAST_NAME}`, where all included spaces replaced with underscores.
- For groups the peer name consists of a chat `{TITLE}` where all spaces replaced with underscores.
- For encrypted chats the peer name should match `{EXPLANATION_MARK}_{FIRST_NAME}_{LAST_NAME}` where all spaces replaced with underscores.

> If two or more peers would have the same names, then the `#{INDEX}` will be appended to their names. 
> _For example: if two peer names are the same `A_B`, they will be renamed to `A_B#1` and `A_B#2`._

### Supported commands

#### Messaging

* **`msg PEER_NAME TEXT`**  - sends message to this peer
* **fwd** \<user\> \<msg-seqno\> - forward message to user. You can see message numbers starting client with -N
* **chat_with_peer** \<peer\> starts one on one chat session with this peer. /exit or /quit to end this mode.
* **add_contact** \<phone-number\> \<first-name\> \<last-name\> - tries to add contact to contact-list by phone
* **rename_contact** \<user\> \<first-name\> \<last-name\> - tries to rename contact. If you have another device it will be a fight
* **mark_read** \<peer\> - mark read all received messages with peer
* **delete_msg** \<msg-seqno\> - deletes message (not completly, though)
* **restore_msg** \<msg-seqno\> - restores delete message. Impossible for secret chats. Only possible short time (one hour, I think) after deletion

#### Multimedia

* **send_photo** \<peer\> \<photo-file-name\> - sends photo to peer
* **send_video** \<peer\> \<video-file-name\> - sends video to peer
* **send_text** \<peer\> \<text-file-name> - sends text file as plain messages
* **load_photo**/load_video/load_video_thumb/load_audio/load_document/load_document_thumb \<msg-seqno\> - loads photo/video/audio/document to download dir
* **view_photo**/view_video/view_video_thumb/view_audio/view_document/view_document_thumb \<msg-seqno\> - loads photo/video to download dir and starts system default viewer
* **fwd_media** \<msg-seqno\> send media in your message. Use this to prevent sharing info about author of media (though, it is possible to determine user_id from media itself, it is not possible get access_hash of this user)
* **set_profile_photo** \<photo-file-name\> - sets userpic. Photo should be square, or server will cut biggest central square part


#### Group chat options

* **chat_info** \<chat\> - prints info about chat
* **chat_add_user** \<chat\> \<user\> - add user to chat
* **chat_del_user** \<chat\> \<user\> - remove user from chat
* **rename_chat** \<chat\> \<new-name\>
* **create_group_chat** \<chat topic\> \<user1\> \<user2\> \<user3\> ... - creates a groupchat with users, use chat_add_user to add more users
* **chat_set_photo** \<chat\> \<photo-file-name\> - sets group chat photo. Same limits as for profile photos.

#### Search

* **search** \<peer\> pattern - searches pattern in messages with peer
* **global_search** pattern - searches pattern in all messages

#### Secret chat

* **create_secret_chat** \<user\> - creates secret chat with this user
* **visualize_key** \<secret_chat\> - prints visualization of encryption key. You should compare it to your partner's one
* **set_ttl** \<secret_chat\> \<ttl\> - sets ttl to secret chat. Though client does ignore it, client on other end can make use of it
* **accept_secret_chat** \<secret_chat\> - manually accept secret chat (only useful when starting with -E key)

#### Stats and various info

* **user_info** \<user\> - prints info about user
* **history** \<peer\> [limit] - prints history (and marks it as read). Default limit = 40
* **dialog_list** - prints info about your dialogs
* **contact_list** - prints info about users in your contact list
* **suggested_contacts** - print info about contacts, you have max common friends
* **stats** - just for debugging
* **show_license** - prints contents of GPLv2
* **help** - prints this help
* **get_self** - get our user info

#### Card
* **export_card** - print your 'card' that anyone can later use to import your contact
* **import_card** \<card\> - gets user by card. You can write messages to him after that.

#### Other
* **quit** - quit
* **safe_quit** - wait for all queries to end then quit
