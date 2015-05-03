

def on_binlog_replay_end():
    pass

def on_get_difference_end():
    pass

def on_our_id():
    pass

def on_msg_receive(msg):
    return "Got msg from " + msg["from"]["peer"]["first_name"]

def on_secret_chat_update(peer, types):
    return "on_secret_chat_update"

def on_user_update():
    pass

def on_chat_update():
    pass

