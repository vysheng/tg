
require "scripts.base_functions";

print ()
print (">---> Telegram-CLI Lua Script!")
print (">---> this script outputs all information which gets pushed from the main-loop")
print ()

function on_msg_receive (msg)
    print (">---> on_msg_receive");
    vardump (msg)
end

function on_our_id (id)
    print (">---> on_our_id");
    vardump (id)
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
