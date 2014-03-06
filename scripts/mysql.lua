our_id = 0

require "scripts.base_functions";

print ()
print (">---> Telegram-CLI Lua Script!")
print (">---> read from and write to MySQL database/tables for incoming and outgoing messages")
print ()


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
