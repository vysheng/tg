log.peer peer_type:int peer_id:int = log.Peer;

log.dc num:int hostname:string ip:string port:int = log.Event;
log.dcRenum old_num:int new_num:int = log.Event;
log.authKey dc:int key:bytes key_id:long = log.Event;
log.signIn dc:int id:int = log.Event;


log.user id:int flags:int access_hash:long first_name:string last_name:string real_first_name:string real_last_name:string phone:string photo:log.Photo photo_id:long photo_big:log.FileLocation photo_small:long.FileLocation = log.Event;

