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

binlog.newUser id:int first_name:string last_name:string hash:long phone:string is_contact:int = binlog.Update;
binlog.userDelete id:int = binlog.Update;
binlog.setUserAccessToken id:int hash:long = binlog.Update;
binlog.setUserPhone id:int phone:string = binlog.Update;
binlog.setUserFriend id:int friend:int = binlog.Update;
binlog.userFullPhoto id:int photo:Photo = binlog.Update;
binlog.userBlocked id:int blocked:int = binlog.Update;
binlog.setUserFullName id:int real_first_name:string real_last_name:string = binlog.Update;

binlog.encrChatDelete id:int = binlog.Update;
binlog.encrChatRequested id:int hash:long date:int admin:int user:int key:64*[int] nonce:64*[int] = binlog.Update;
binlog.setEncrChatAccessHash id:int hash:long = binlog.Update;
binlog.setEncrChatDate id:int date:int = binlog.Update;
binlog.setEncrChatState id:int state:int = binlog.Update;
binlog.encrChatAccepted id:int key:64*[int] nonce:64*[int] fingerprint:long = binlog.Update;
binlog.setEncrChatKey id:int key:64*[int] fingerprint:long = binlog.Update;
binlog.encrChatInit id:int user:int key:64*[int] g_key:64*[int] = binlog.Update;


binlog.chatCreate id:int flags:int title:string user_num:int date:int version:int photo_big:%binlog.FileLocation photo_small:%binlog.FileLocation = binlog.Update;
binlog.chatChangeFlags id:int set_flags:int clear_flags:int = binlog.Update;
binlog.setChatTitle id:int title:string = binlog.Update;
binlog.setChatPhoto id:int photo_big:%binlog.FileLocation photo_small:%binlog.FileLocation = binlog.Update;
binlog.setChatDate id:int date:int = binlog.Update;
binlog.setChatVersion id:int version:int users_num:int = binlog.Update;
binlog.setChatAdmin id:int admin:int = binlog.Update;
binlog.setChatParticipants id:int version:int participants:%(Vector %binlog.ChatParticipant) = binlog.Update;
binlog.chatFullPhoto id:int photo:Photo = binlog.Update;
binlog.addChatParticipant id:int version:int user:%binlog.ChatParticipant = binlog.Update;
binlog.delChatParticipant id:int version:int user:int = binlog.Update;

binlog.createMessageText id:int from_id:int to_type:int to_id:int date:int text:string = binlog.Update;
binlog.sendMessageText id:long from_id:int to_type:int to_id:int date:int text:string = binlog.Update;
binlog.createMessageTextFwd id:int from_id:int to_type:int to_id:int date:int fwd_from_id:int fwd_date:int text:string = binlog.Update;
binlog.createMessageMedia id:int from_id:int to_type:int to_id:int date:int text:string media:MessageMedia = binlog.Update;
binlog.createMessageMediaEncr id:int from_id:int to_type:int to_id:int date:int text:string media:DecryptedMessageMedia file:EncryptedFile = binlog.Update;
