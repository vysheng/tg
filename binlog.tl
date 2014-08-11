---types---
binlog.start = binlog.Update;
binlog.dcUpdate id:int name:string ip:string port:int = binlog.Update;
binlog.authKey dc:int key_id:long key:64*[int] = binlog.Update; 
binlog.defaultDc dc:int = binlog.Update;
binlog.ourId id:int = binlog.Update;
binlog.dcSigned id:int = binlog.Update;
binlog.dcSalt id:int salt:long = binlog.Update;

binlog.setDhParams root:int prime:64*[int] version:int = binlog.Update;

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


