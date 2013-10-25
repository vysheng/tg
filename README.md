## Telegram messenger CLI

Command-line interface for [Telegram](http://telegram.org). Uses readline interface.

### API, Protocol documentation

Documentation for Telegram API is available here: http://core.telegram.org/api

Documentation for MTproto protocol is available here: http://core.telegram.org/mtproto

### Installation

Just run `make`

#### Requirements

Currently only Linux OS is supported. But if you manage to launch it on OS X or other UNIX, please let me know.

### Usage

    ./telegram -k <public-server-key>
    
By default public key is stored in the same folder named tg.pub:

    ./telegram -k tg.pub
  
#### Supported commands:

* chat_info
* contact_list
* dialog_list
* help
* history
* msg
* send_photo
* send_text
* send_video
* stats

