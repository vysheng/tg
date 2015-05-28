import tgl
import pprint
from functools import partial


our_id = 0
pp = pprint.PrettyPrinter(indent=4)

binlog_done = False;

def on_binlog_replay_end():
    binlog_done = True;

def on_get_difference_end():
    pass

def on_our_id(id):
    our_id = id
    return "Set ID: " + str(our_id)

def msg_cb(success, msg):
    pp.pprint(success)
    pp.pprint(msg)

HISTORY_QUERY_SIZE = 100

def history_cb(msg_list, peer, success, msgs):
  print(len(msgs))
  msg_list.extend(msgs)
  print(len(msg_list))
  if len(msgs) == HISTORY_QUERY_SIZE:
    tgl.get_history(peer, len(msg_list), HISTORY_QUERY_SIZE, partial(history_cb, msg_list, peer));


def cb(success):
    print(success)

def on_msg_receive(msg):
    if msg.out and not binlog_done:
      return;

    if msg.dest.id == our_id: # direct message
      peer = msg.src
    else: # chatroom
      peer = msg.dest

    pp.pprint(msg)
    if msg.text.startswith("!ping"):
      peer.send_msg("PONG! google.com", preview=False, reply=msg.id)


def on_secret_chat_update(peer, types):
    return "on_secret_chat_update"

def on_user_update():
    pass

def on_chat_update():
    pass

# Set callbacks
tgl.set_on_binlog_replay_end(on_binlog_replay_end)
tgl.set_on_get_difference_end(on_get_difference_end)
tgl.set_on_our_id(on_our_id)
tgl.set_on_msg_receive(on_msg_receive)
tgl.set_on_secret_chat_update(on_secret_chat_update)
tgl.set_on_user_update(on_user_update)
tgl.set_on_chat_update(on_chat_update)

