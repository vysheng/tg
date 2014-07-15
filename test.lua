started = 0
our_id = 0

require "scripts.base_functions";

print ("HI, this is lua script")

function on_msg_receive (msg)
  if started == 0 then
    return
  end
  if msg.out then
    return
  end
  if (msg.text == 'ping') then
    if (msg.to.id == our_id) then
      print ('sending pong to ' .. tostring (msg.from.print_name))
      send_msg (msg.from.print_name, 'pong')
    else
      print ('sending pong to ' .. tostring (msg.to.print_name))
      send_msg (msg.to.print_name, 'pong')
    end
    return
  end
  if (msg.text == 'PING') then
    if (msg.to.id == our_id) then
      fwd_msg (msg.from.print_name, msg.id)
    else
      fwd_msg (msg.to.print_name, msg.id)
    end
    return
  end
  --vardump (msg)
  --print ( "Message # " .. msg.id .. " (flags " .. msg.flags .. ")")
end

function on_our_id (id)
  our_id = id
end

function on_secret_chat_created (peer)
  --vardump (peer)
end

function on_user_update (user)
  --vardump (user)
end

function on_chat_update (user)
  --vardump (user)
end

function on_get_difference_end ()
end

function on_binlog_replay_end ()
  started = 1
end

function on_second_scheduler_end ()
	-- would be executed each second 
end
