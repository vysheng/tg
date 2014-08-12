---types---
binlog.start = binlog.Update;
binlog.dcUpdate id:int name:string ip:string port:int = binlog.Update;
binlog.authKey dc:int key_id:long key:64*[int] = binlog.Update; 
binlog.defaultDc dc:int = binlog.Update;
binlog.ourId id:int = binlog.Update;
binlog.dcSigned id:int = binlog.Update;
binlog.dcSalt id:int salt:long = binlog.Update;

binlog.newUser id:int first_name:string last_name:string hash:long phone:string is_contact:int = binlog.Update;
binlog.userDelete id:int = binlog.Update;
