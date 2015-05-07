import tgl
import geopy
import pprint
from geopy.geocoders import Nominatim


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

def on_msg_receive(msg):
    if msg["out"] and not binlog_done:
      return;

    if msg["to"]["id"] == our_id: # direct message
      ptype = msg["from"]["type"]
      pid   = msg["from"]["id"]
    else: # chatroom
      ptype = msg["to"]["type"]
      pid   = msg["to"]["id"]

    text = msg["text"]

    if text.startswith("!ping"):
      tgl.send_msg(ptype, pid, "PONG!")

    if text.startswith("!location"):
      geolocator = Nominatim()
      location = geolocator.geocode(msg["text"][9:])
      pp.pprint(location)
      tgl.send_location(ptype, pid, location.latitude, location.longitude)

def on_secret_chat_update(peer, types):
    return "on_secret_chat_update"

def on_user_update():
    pass

def on_chat_update():
    pass

