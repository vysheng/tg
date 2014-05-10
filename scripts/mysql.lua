
require "scripts.base_functions";

print ()
print (">---> Telegram-CLI Lua Script!")
print (">---> read from and write to MySQL database/tables for incoming and outgoing messages")
print ()

-- check for mysql-conf file
if not file_exists("./scripts/mysql.conf") then
	f=io.open( "./scripts/mysql.conf", "w" )
	f:write( 'db_db = "telegram"\n' )
	f:write( 'db_user = "telegram"\n' )
	f:write( 'db_pass = "telegram"\n' )
	f:write( 'db_host = "127.0.0.1"\n' )
	f:write( 'db_port = "3306"\n' )
	f:close()
end

-- http://www.keplerproject.org/luasql/examples.html
luasql = require "luasql.mysql"
dofile "./scripts/mysql.conf"
env = assert (luasql.mysql())
con = assert (env:connect( db_db, db_user, db_pass, db_host, db_port ))

res = assert (con:execute[[
	CREATE TABLE IF NOT EXISTS `users` (
		`uid` int(11) NOT NULL,
		`phone` varchar(25) NOT NULL,
		`first_name` varchar(255) NOT NULL,
		`last_name` varchar(255) NOT NULL,
		`print_name` varchar(255) NOT NULL,
		PRIMARY KEY (`uid`)
	)
]])
res = assert (con:execute[[
	CREATE TABLE IF NOT EXISTS `online` (
		`online_id` int(11) NOT NULL AUTO_INCREMENT,
		`uid` int(11) NOT NULL,
		`moment` datetime NOT NULL,
		`online` int(11) NOT NULL,
		PRIMARY KEY (`online_id`)
	)
]])
res = assert (con:execute[[
	CREATE TABLE IF NOT EXISTS `inbox` (
		`mid` int(11) NOT NULL,
		`uid` int(11) NOT NULL,
		`moment` datetime NOT NULL,
		`msg` text NOT NULL,
		PRIMARY KEY (`mid`, `uid`)
	)
]])
res = assert (con:execute[[
	CREATE TABLE IF NOT EXISTS `outbox` (
		`mid` int(11) NOT NULL,
		`uid` int(11) NOT NULL,
		`moment` datetime NOT NULL,
		`msg` text NOT NULL,
		PRIMARY KEY (`mid`, `uid`)
	)
]])
res = assert (con:execute[[
	CREATE TABLE IF NOT EXISTS `sendbox` (
		`send_id` int(11) NOT NULL AUTO_INCREMENT,
		`uid` int(11) NOT NULL,
		`msg` text NOT NULL,
		PRIMARY KEY (`send_id`)
	)
]])


function on_msg_receive (msg)
	if msg.out
	then
		print (">---> on_msg_send");
		--vardump (msg)
		res = con:execute("INSERT INTO `outbox` ( mid, uid, moment, msg ) VALUES ( '" .. msg.id .. "', '" .. msg.to.id .. "', FROM_UNIXTIME(" .. msg.date .. "), '" .. msg.text .. "' );");
	else
		print (">---> on_msg_receive");
		--vardump (msg)
		res = con:execute("INSERT INTO `inbox` ( mid, uid, moment, msg ) VALUES ( '" .. msg.id .. "', '" .. msg.from.id .. "', FROM_UNIXTIME(" .. msg.date .. "), '" .. msg.text .. "' );");
	end
end

function on_user_update (user)
	print (">---> on_user_update");
	res = con:execute("INSERT INTO `users` ( uid, phone, first_name, last_name, print_name ) VALUES ( '" .. user.id .. "', '" .. user.phone .. "', '" .. user.first_name .. "', '" .. user.last_name .. "', '" .. user.print_name .. "' );");
end

function on_user_status_update (user, online)
	print (">---> on_user_status_update");
	if online == -1
	then
		online = 0;
	end
	res = con:execute("INSERT INTO `online` ( uid, moment, online ) VALUES ( '" .. user.id .. "', NOW(), '" .. online .. "' );");
end

function on_chat_update (user)
	print (">---> on_chat_update");
--  vardump (user)
end
