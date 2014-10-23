---types---
binlog.fileLocation dc:int volume:long local_id:int secret:long = binlog.FileLocation;
binlog.chatParticipant user:int inviter:int date:int = binlog.ChatParticipant;

binlog.start = binlog.Update;

binlog.dcOption id:int name:string ip:string port:int = binlog.Update;
binlog.authKey dc:int key_id:long key:64*[int] = binlog.Update; 
binlog.defaultDc dc:int = binlog.Update;
binlog.ourId id:int = binlog.Update;
binlog.dcSigned id:int = binlog.Update;
binlog.dcSalt id:int salt:long = binlog.Update;

binlog.setDhParams root:int prime:64*[int] version:int = binlog.Update;
binlog.setPts pts:int = binlog.Update;
binlog.setQts qts:int = binlog.Update;
binlog.setDate date:int = binlog.Update;
binlog.setSeq seq:int = binlog.Update;

binlog.userAdd id:int first_name:string last_name:string hash:long phone:string is_contact:int = binlog.Update;
binlog.userDelete id:int = binlog.Update;
binlog.userSetAccessHash id:int hash:long = binlog.Update;
binlog.userSetPhone id:int phone:string = binlog.Update;
binlog.userSetFriend id:int friend:int = binlog.Update;
binlog.userSetFullPhoto id:int photo:Photo = binlog.Update;
binlog.userSetBlocked id:int blocked:int = binlog.Update;
binlog.userSetRealName id:int real_first_name:string real_last_name:string = binlog.Update;
binlog.userSetName id:int first_name:string last_name:string = binlog.Update;
binlog.userSetPhoto id:int photo:UserProfilePhoto = binlog.Update;
binlog.userSetUsername id:int username:string = binlog.Update;

binlog.encrChatDelete id:int = binlog.Update;
binlog.encrChatRequested id:int hash:long date:int admin:int user:int key:64*[int] nonce:64*[int] = binlog.Update;
binlog.encrChatAccepted id:int key:64*[int] nonce:64*[int] fingerprint:long = binlog.Update;
binlog.encrChatInit id:int user:int key:64*[int] g_key:64*[int] = binlog.Update;
binlog.encrChatCreate id:int user_id:int admin_id:int name:string = binlog.Update;

binlog.encrChatSetAccessHash id:int hash:long = binlog.Update;
binlog.encrChatSetDate id:int date:int = binlog.Update;
binlog.encrChatSetTtl id:int ttl:int = binlog.Update;
binlog.encrChatSetLayer id:int layer:int = binlog.Update;
binlog.encrChatSetState id:int state:int = binlog.Update;
binlog.encrChatSetKey id:int key:64*[int] fingerprint:long = binlog.Update;

binlog.encrChatUpdateSeq id:int in_seq_no:int out_seq_no:int = binlog.Update;
binlog.encrChatSetSeq id:int in_seq_no:int last_in_seq_no:int out_seq_no:int = binlog.Update;

binlog.chatCreate id:int flags:int title:string user_num:int date:int version:int photo_big:%binlog.FileLocation photo_small:%binlog.FileLocation = binlog.Update;
binlog.chatChangeFlags id:int set_flags:int clear_flags:int = binlog.Update;
binlog.chatSetTitle id:int title:string = binlog.Update;
binlog.chatSetPhoto id:int photo_big:%binlog.FileLocation photo_small:%binlog.FileLocation = binlog.Update;
binlog.chatSetDate id:int date:int = binlog.Update;
binlog.chatSetVersion id:int version:int users_num:int = binlog.Update;
binlog.chatSetAdmin id:int admin:int = binlog.Update;
binlog.chatSetParticipants id:int version:int participants:%(Vector %binlog.ChatParticipant) = binlog.Update;
binlog.chatSetFullPhoto id:int photo:Photo = binlog.Update;
binlog.chatAddParticipant id:int version:int user:%binlog.ChatParticipant = binlog.Update;
binlog.chatDelParticipant id:int version:int user:int = binlog.Update;

binlog.createMessageText id:int from_id:int to_type:int to_id:int date:int unread:int text:string = binlog.Update;
binlog.sendMessageText id:long from_id:int to_type:int to_id:int date:int text:string = binlog.Update;
binlog.sendMessageActionEncr id:long from_id:int to_type:int to_id:int date:int action:DecryptedMessageAction = binlog.Update;
binlog.createMessageTextFwd id:int from_id:int to_type:int to_id:int date:int fwd_from_id:int fwd_date:int unread:int text:string = binlog.Update;
binlog.createMessageMedia id:int from_id:int to_type:int to_id:int date:int unread:int text:string media:MessageMedia = binlog.Update;
binlog.createMessageMediaEncr id:long from_id:int to_type:int to_id:int date:int text:string media:DecryptedMessageMedia file:EncryptedFile = binlog.Update;
binlog.createMessageMediaEncrPending id:long from_id:int to_type:int to_id:int date:int text:string media:DecryptedMessageMedia = binlog.Update;
binlog.createMessageMediaEncrSent id:long file:EncryptedFile = binlog.Update;
binlog.createMessageMediaFwd id:int from_id:int to_type:int to_id:int date:int fwd_from_id:int fwd_date:int unread:int text:string media:MessageMedia = binlog.Update;
binlog.createMessageService id:int from_id:int to_type:int to_id:int date:int unread:int action:MessageAction = binlog.Update;
binlog.createMessageServiceEncr id:long from_id:int to_type:int to_id:int date:int action:DecryptedMessageAction = binlog.Update;
binlog.createMessageServiceFwd id:int from_id:int to_type:int to_id:int date:int fwd_from_id:int fwd_date:int unread:int action:MessageAction = binlog.Update;
binlog.messageSetUnread id:int = binlog.Update;
binlog.messageSetUnreadLong id:long = binlog.Update;
binlog.setMessageSent id:long = binlog.Update;
binlog.setMsgId old_id:long new_id:int = binlog.Update;
binlog.deleteMsg id:long = binlog.Update;

binlog.msgSeqUpdate id:long = binlog.Update;
binlog.msgUpdate id:long = binlog.Update;

binlog.resetAuthorization = binlog.Update;

//binlog.addDc id:int ip:string port:int auth_key_id:long auth_key:64*[int] = binlog.Update;
