started = 0
our_id = 0

function vardump(value, depth, key)
  local linePrefix = ""
  local spaces = ""
  
  if key ~= nil then
    linePrefix = "["..key.."] = "
  end
  
  if depth == nil then
    depth = 0
  else
    depth = depth + 1
    for i=1, depth do spaces = spaces .. "  " end
  end
  
  if type(value) == 'table' then
    mTable = getmetatable(value)
    if mTable == nil then
      print(spaces ..linePrefix.."(table) ")
    else
      print(spaces .."(metatable) ")
        value = mTable
    end		
    for tableKey, tableValue in pairs(value) do
      vardump(tableValue, depth, tableKey)
    end
  elseif type(value)	== 'function' or 
      type(value)	== 'thread' or 
      type(value)	== 'userdata' or
      value		== nil
  then
    print(spaces..tostring(value))
  else
    print(spaces..linePrefix.."("..type(value)..") "..tostring(value))
  end
end

print ("HI, this is lua script")

function ok_cb(extra, success, result)
  if success then
    print ("SUCCESS!\n")
  end
end

function my_set_chat_photo (extra, success, file)
  if success then
    chat_set_photo (extra, file, ok_cb, false)
  end
end

function on_msg_receive (msg)
  if started == 0 then
    return
  end
  if msg.out then
    return
  end

  if msg.media == 'photo' and msg.to.type == 'chat' then
    load_photo (msg.id, my_set_chat_photo, msg.to.print_name)
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

function on_user_update (user, what)
  --vardump (user)
end

function on_chat_update (chat, what)
  --vardump (chat)
end

function on_secret_chat_update (schat, what)
  --vardump (schat)
end

function on_get_difference_end ()
end

function on_binlog_replay_end ()
  started = 1
end
