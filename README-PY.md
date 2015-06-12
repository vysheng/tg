Python Bindings
====================
All of the functions and methods are executed in the network loop of tg, ***NOT IMMEDIATELY***. What this means is all calls should be considered async, and so there is an optional callback parameter for every function/method as the last parameter. For many uses, you won't care about the return value, so you can leave out the callback. Note there are a few cases where the callback is considered mandatory when the function is considered an information query and has no functionality without returned data. These will explicitly have the callback in the parameter list and will be noted in the description.

You can specify the python script from config ("python_script" option) or from command_line [-Z].

Version Notes
=====================
The python integration is written with Python 2/3 in mind, however, there is a bias to Python 3. Because of this, there are a few caveats:
- I am only testing against Python 2.7, and have no intention to support/test < 2.7 but am more than happy to accept PRs for fixes as long as it does not break 2.7/3
- repr/print of native types is dumbed down for < 2.7.9, I highly recommend using this version or newer. (This is due to a [bug](http://bugs.python.org/issue22023) in python)

TGL Callbacks
=============
Assign python fuctions to the following tgl attributes to set callbacks from TG.

| Callback | Description          |
|--------- | ---------------------|
|`tgl.on_binlog_replay_end()` | This is called when replay of old events end. Any updates prior this call were already received by this client some time ago.|
|`tgl.on_get_difference_end()`| This is called after first get_difference call. So we received all updates after last client execute.|
|`tgl.on_our_id(our_id)`|Informs about id of currently logged in user.|
|`tgl.on_msg_receive(msg)`| This is called when we receive new `tgl.Msg` object (*may be called before on_binlog_replay_end, than it is old msg*).|
|`tgl.on_user_update(peer, what_changed)`|updated info about user. peer is a `tgl.Peer` object representing the user, and what_changed is array of strings.|
|`tgl.on_chat_update(peer, what_changed)`|updated info about chat. peer is a `tgl.Peer` object representing the chat, and what_changed is array of strings.|
|`tgl.on_secret_chat_update(peer, what_changed)`|updated info about secret chat. peer is a `tgl.Peer` object representing the secret chat, and  what_changed is array of strings.|

Python Callback Signatures
=========================
As mentioned, all methods and functions have callbacks. The following are the different signatures that may be required (*The description of the method will mention which is used*)

| Type | Signature          | Description |
|----- | ------------------ | ------------|
|empty_cb|`(success)`|This just indicated the success of the call. All other callback types have this as well.|
|contact_list_cb|`(success, peer_list)`|`peer_list` is a list of `tgl.Peer` objects|
|dialog_list_cb|`(success, dialog_list)`|`dialog_list` is a list of dicts, with keys: 'peer': `tgl.Peer`, 'message': `tgl.Msg`|
|msg_cb|`(success, msg)`|`msg` is a `tgl.Msg`|
|msg_list_cb|`(success, msg_list)`|`msg_list` is a list of `tgl.Msg` objects|
|file_cb|`(success, file_path)`|`file_path` is a string with an absolute path|
|chat_cb|`(success, peer)`|`peer` is a `tgl.Peer` that will have the type of `tgl.PEER_CHAT`|
|peer_cb|`(success, peer)`|`peer` is a `tgl.Peer`|
|secret_chat_cb|`(success, peer)`|`peer` is a `tgl.Peer` that will have the type of `tgl.PEER_ENCR_CHAT`|
|user_cb|`(success, peer)`|`peer` is a `tgl.Peer` that will have the type of `tgl.PEER_USER`|
|str_cb|`(success, string)`|`string` is a simple string|

TGL Module Level Fuctions
=========================
All of these functions are accessed by importing the `tgl` module.

| Function | Description          | Callback Type |
|--------- | ---------------------| ------------- |
|`tgl.get_contact_list (callback)`|Retrieve peers stored in the contact list. *requires callback*|`contact_list_cb`|
|`tgl.get_dialog_list (callback)`|Get a list of current conversations with the `tgl.Peer` and the most recent `tgl.Msg`. *requires callback*|`dialog_list_cb`|
|`tgl.add_contact (phone, first_name, last_name)`|Adds contact to contact list, last name is optional|`contact_list_cb`|
|`tgl.del_contact (peer)`|Removes contact from contact list|`empty_cb`|
|`tgl.rename_contact (phone, first_name, last_name) `|Renaming a contact means sending the first/last name with the same phone number of an existing contact|`contact_list_cb`|
|`tgl.msg_global_search (text, callback)`|Get all messages that match the search text. *requires callback*|`msg_list_cb`|
|`tgl.set_profile_photo (file_path)`|Sets avatar to image found at `file_path`, no checking on the file.|`empty_cb`|
|`tgl.create_secret_chat (user)`|Creates secret chat with user, callback recommended to get new peer for the secret chat.|`secret_chat_cb`|
|`tgl.create_group_chat (peer_list, name)`|`peer_list` contains users to create group with, must be more than 1 peer.|`empty_cb`|
|`tgl.restore_msg (msg_id)`|Restore a deleted message by message id.|`empty_cb`|
|`tgl.status_online ()`|Sets status as online|`empty_cb`|
|`tgl.status_offline ()`|Sets status as offline|`empty_cb`|
|`tgl.import_chat_link (link)`|Join channel using the `link`.|`empty_cb`|
|`tgl.safe_exit (exit_status)`|Exception to the rule about callbacks, no callback parameter is accepted. Causes the bot to quit after cleaning up. `exit_status` is an optional parameter with the exit status (On glibc, this must be 0-255)|NONE|

Peer
====
## Attributes
| Attribute | Type |  Description          |
|---------- | ---- | ----------------------|
|`id`|`int`|Telegram peer id|
|`type`|`int`|Peer type, you can compare this with `tgl.PEER_CHAT`, `tgl.PEER_USER`, or `tgl.PEER_ENCR_CHAT`|
|`type_name`|`string`|Text representation of the type of the peer, `'chat'`, `'user'`, or `'secret_chat'`|
|`name`|`string`|Returns the tg print version of the name. Usually `FirstName_LastName` for user, and the chatname with spaces replaced with `_`|
|`user_id`|`int`|Used in secret chats, since a secret chat has it's own id, this is the id of the user at the endpoint.|
|`user_list`|`peer_list`|Only used in `tgl.PEER_CHAT` peers, contains list of users. This currently does not work, it is not populating properly.|
|`user_status`|`dict`|Only used in `tgl.PEER_USER` peers. Dictionary with the current status, keys: 'online': `bool`, 'when': `datetime`|
|`phone`|`string`|Only used in `tgl.PEER_USER` peers. Phone number, only available if user is on contact list.|
|`username`|`string`|Only used in `tgl.PEER_USER` peers. Will be `None` if username is not set.|
|`first_name`|`string`|Only used in `tgl.PEER_USER` peers.|
|`last_name`|`string`|Only used in `tgl.PEER_USER` peers.|

## Methods
| Method | Description          | Callback Type |
|------- | ---------------------| ------------- |
|`peer.rename_chat (new_name)`||`empty_cb`|
|`peer.chat_set_photo (file)`|Sets avatar for the group to image found at `file_path`, no checking on the file. The calling peer must be of type `tgl.PEER_CHAT`.|`msg_cb`|
|`peer.send_typing ()`|Tell peer that you are typing.|`empty_cb`|
|`peer.send_typing_abort ()`|Tell peer you are done typing.|`empty_cb`|
|`peer.send_msg (text, reply=msg_id, preview=bool)`|Sends message to peer. Optional keyword arguments: reply is the message id we are replying to, preview is a boolean that forces URL preview on or off.|`msg_cb`|
|`peer.fwd_msg (msg_id)`|Forwards msg with message id to peer.|`msg_cb`|
|`peer.fwd_media (msg_id)`|Forwards media with message id to peer.|`msg_cb`|
|`peer.send_photo (file)`|Send media to peer using `file`. No checking is done on the contents of the file.|`msg_cb`|
|`peer.send_video (file)`|Send media to peer using `file`. No checking is done on the contents of the file.|`msg_cb`|
|`peer.send_audio (file)`|Send media to peer using `file`. No checking is done on the contents of the file.|`msg_cb`|
|`peer.send_document (file)`|Send media to peer using `file`. No checking is done on the contents of the file.|`msg_cb`|
|`peer.send_text (file)`|Send media to peer using `file`. No checking is done on the contents of the file.|`msg_cb`|
|`peer.send_location (latitude, longitude)`|Sends location media message to peer, `longitude` and `latitude` should be specified as double.|`msg_cb`|
|`peer.chat_add_user (user)`|Adds `user`(`tgl.Peer`) to the group. The calling peer must be of type `tgl.PEER_CHAT`|`empty_cb`|
|`peer.chat_del_user (user)`|Removes `user`(`tgl.Peer`) from the group. The calling peer must be of type `tgl.PEER_CHAT`|`empty_cb`|
|`peer.mark_read ()`|Marks the dialog with the peer as read. This cannot be done on message level.|`empty_cb`|
|`peer.msg_search (text, callback)`|Get all messages that match the search text with the peer. *requires callback*|`msg_list_cb`|
|`peer.get_history (offset, limit, callback)`|Get all messages with the peer. `offset` specifies what message to start at, and `limit` specifies how many messages to retrieve. See example below for one method to get the entire history. *requires callback*|`msg_list_cb`|
|`peer.info ()`|Gets peer info.|`peer_cb`|

Example usage for `peer.get_history`:
```
from functools import partial
history = []
# Get all the history, 100 msgs at a time
peer.get_history(0, 100, partial(history_callback, 100, peer))

def history_callback(msg_count, peer, success, msgs)
    history.extend(msgs)
    if len(msgs) == msg_count:
        peer.get_history(len(history), msg_count, partial(history_callback, msg_count, peer))
```

Msg
====
## Attributes
| Attribute | Type |  Description          |
|---------- | ---- | ----------------------|
|`id`|`int`|Message id|
|`flags`|`int`|tgl flags, see source code for tgl for various possible flags. This is a bitfield in an int.|
|`mention`|`bool`|`True` if you are @mentioned.|
|`out`|`bool`|`True` if you sent this message.|
|`unread`|`bool`|`True` if you have not marked this as read.|
|`service`|`bool`|`True` if the message is a service messages, see tgl.Msg.action for the type.|
|`src`|`tgl.Peer`|Peer who sent the message|
|`dest`|`tgl.Peer`|Peer who the message was sent too. In a group, this will be the chat peer. Otherwise it will be you.|
|`text`|`string`|Text contents of the message. This may be `None` if it's media without caption.|
|`media`|`dict`|Dictionary that varies based on the media type.|
|`date`|`datetime`|When the message was sent.|
|`fwd_src`|`tgl.Peer`|The user that sent the forwarded message.|
|`fwd_date`|`datetime`|When the forwarded message was originally sent.|
|`reply`|`tgl.Msg`|Message that this message is replying to.|
|`reply_id`|`int`|Message id that this message is replying to.|
|`action`|`int`|Action enum for the message if `msg.service == True`. See all the possible values below|

## Action Type Constants
- tgl.ACTION_NONE
- tgl.ACTION_GEO_CHAT_CREATE
- tgl.ACTION_GEO_CHAT_CHECKIN
- tgl.ACTION_CHAT_CREATE
- tgl.ACTION_CHAT_EDIT_TITLE
- tgl.ACTION_CHAT_EDIT_PHOTO
- tgl.ACTION_CHAT_DELETE_PHOTO
- tgl.ACTION_CHAT_ADD_USER
- tgl.ACTION_CHAT_ADD_USER_BY_LINK
- tgl.ACTION_CHAT_DELETE_USER
- tgl.ACTION_SET_MESSAGE_TTL
- tgl.ACTION_READ_MESSAGES
- tgl.ACTION_DELETE_MESSAGES
- tgl.ACTION_SCREENSHOT_MESSAGES
- tgl.ACTION_FLUSH_HISTORY
- tgl.ACTION_RESEND
- tgl.ACTION_NOTIFY_LAYER
- tgl.ACTION_TYPING
- tgl.ACTION_NOOP
- tgl.ACTION_COMMIT_KEY
- tgl.ACTION_ABORT_KEY
- tgl.ACTION_REQUEST_KEY
- tgl.ACTION_ACCEPT_KEY

## Methods
| Method | Description          | Callback Type |
|------- | ---------------------| ------------- |
|`msg.load_photo(callback)`|Saves the media and returns the path to the file in the callback. *requires callback*|`file_cb`|
|`msg.load_video(callback)`|Saves the media and returns the path to the file in the callback. *requires callback*|`file_cb`|
|`msg.load_video_thumb(callback)`|Saves the media and returns the path to the file in the callback. *requires callback*|`file_cb`|
|`msg.load_audio(callback)`|Saves the media and returns the path to the file in the callback. *requires callback*|`file_cb`|
|`msg.load_document(callback)`|Saves the media and returns the path to the file in the callback. *requires callback*|`file_cb`|
|`msg.load_document_thumb(callback)`|Saves the media and returns the path to the file in the callback. *requires callback*|`file_cb`|
|`msg.delete_msg ()`|Deletes the message from the local history|`empty_cb`|
