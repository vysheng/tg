import tgl
import pprint


our_id = 0
pp = pprint.PrettyPrinter(indent=4)

def on_binlog_replay_end():
    pass

def on_get_difference_end():
    pass

def on_our_id(id):
    our_id = id
    return "Set ID: " + str(our_id) 

def on_msg_receive(msg):
    if msg["out"]:
      return;

    tgl.send_msg(msg["from"]["type"], msg["from"]["id"], "PONG!")

def on_secret_chat_update(peer, types):
    return "on_secret_chat_update"

def on_user_update():
    pass

def on_chat_update():
    pass

