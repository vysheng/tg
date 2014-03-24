our_id = 0
inbox = "/tmp/telegram_inbox"
outbox = "/tmp/telegram_outbox"

require "scripts.base_functions";

print ()
print (">---> Telegram-CLI Lua Script!")
print (">---> read from " .. inbox .. " for incoming messages")
print (">---> write to " .. outbox .. " for outgoing messages")
print ()


-- (table)
--   [date] = (number) 1393973625
--   [from] = (table)
--     [id] = (number) 32778144
--     [type] = (string) user
--     [print_name] = (string) user#32778144
--   [unread] = (boolean) true
--   [flags] = (number) 16
--   [service] = (boolean) false
--   [id] = (string) 119
--   [text] = (string) 💋
--   [to] = (table)
--     [id] = (number) 25928291
--     [type] = (string) user
--     [print_name] = (string) user#25928291
--   [out] = (boolean) false
-- Message # 119 (flags 16)
-- date,       from_id,  from_type, from_print_name, unread, flags, service, id,  text,    to_id,    to_type, to_print_name, out
-- 1393973625, 32778144, user,      user#32778144,   true,   16,    false,   119, bericht, 25928291, user,    user#25928291, false
--
--> >---> on_msg_receive
--(table)
--[to] = (table)
--  [phone] = (string) 31629735850
--  [print_name] = (string) Dennis
--  [flags] = (number) 144
--  [first_name] = (string) Dennis
--  [type] = (string) user
--  [id] = (number) 25928291
--[date] = (number) 1394041546
--[flags] = (number) 16
--[from] = (table)
--  [phone] = (string) 31641903990
--  [last_name] = (string) Litchidova
--  [print_name] = (string) Anna_Litchidova
--  [flags] = (number) 528
--  [first_name] = (string) Anna
--  [type] = (string) user
--  [id] = (number) 32778144
--[text] = (string) Ik ben op de terugweg
--[service] = (boolean) false
--[unread] = (boolean) true
--[id] = (string) 120
--[out] = (boolean) false


function on_msg_receive (msg)
    if msg.out
    then
        print (">---> on_msg_send");
        --vardump (msg)

    else
        print (">---> on_msg_receive");
        --vardump (msg)

        csv =
            msg.id .. ";" ..
            msg.date .. ";" ..
            msg.from.id .. ";" ..
            msg.from.phone .. ";" ..
            msg.from.print_name .. ";" ..
            msg.to.id .. ";" ..
            msg.to.phone .. ";" ..
            msg.to.print_name .. ";" ..
            msg.text .. ";" ..
            tostring(msg.out)
        print ( csv )
    end
end

function on_our_id (id)
    print (">---> on_our_id");
    vardump (id)
    our_id = id
end

function on_secret_chat_created (peer)
    print (">---> on_secret_chat_created");
    vardump (peer)
end

function on_user_update (user)
    print (">---> on_user_update");
    vardump (user)
end

function on_user_status_update (user, online)
	print (">---> on_user_status_update");
	vardump (user)
	vardump (online)
end

function on_chat_update (user)
    print (">---> on_chat_update");
    vardump (user)
end

function on_get_difference_end ()
    print (">---> on_get_difference_end");
end

function on_binlog_replay_end ()
    print (">---> on_binlog_replay_end");
end
