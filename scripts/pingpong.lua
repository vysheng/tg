started = 0
our_id = 0

require "scripts.base_functions";

print ()
print (">---> Telegram-CLI Lua Script!")
print (">---> this script replies to 'ping' with 'pong'")
print (">--->                 and to 'PING' with msg.id")
print ()

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
			print ('sending msg.id [' .. tostring (msg.id) .. '] to ' .. tostring (msg.from.print_name))
			fwd_msg (msg.from.print_name, msg.id)
		else
			print ('sending msg.id [' .. tostring (msg.id) .. '] to ' .. tostring (msg.to.print_name))
			fwd_msg (msg.to.print_name, msg.id)
		end
		return
	end
	--vardump (msg)
	--print ( "Message # " .. msg.id .. " (flags " .. msg.flags .. ")")
end

function on_our_id (id)
	print (id)
	our_id = id
end

function on_user_update (user)
    started = 1
end
