
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

function escapeCSV (value)
    if type(value) ~= 'string'
    then
        value = tostring(value)
    end
    if string.find(value, '[,"]') then
        value = '"' .. string.gsub(value, '"', '""') .. '"'
    end
    return value
end

function toCSV (value)
    local s = ""
    for _,p in pairs(value) do
        if type(p) == 'table'
        then
            s = s .. "," .. toCSV(p)
        else
            s = s .. "," .. escapeCSV(p)
        end
    end
    return string.sub(s, 2)      -- remove first comma
end

function fromCSV (value)
    value = value .. ','        -- ending comma
    local t = {}        -- table to collect fields
    local fieldstart = 1
    repeat
        -- next field is quoted? (start with `"'?)
        if string.find(value, '^"', fieldstart) then
            local a, c
            local i  = fieldstart
            repeat
                -- find closing quote
                a, i, c = string.find(value, '"("?)', i+1)
                until c ~= '"'    -- quote not followed by quote?
            if not i then error('unmatched "') end
            local f = string.sub(value, fieldstart+1, i-1)
            table.insert(t, (string.gsub(f, '""', '"')))
            fieldstart = string.find(s, ',', i) + 1
        else                -- unquoted; find next comma
            local nexti = string.find(value, ',', fieldstart)
            table.insert(t, string.sub(value, fieldstart, nexti-1))
            fieldstart = nexti + 1
        end
        until fieldstart > string.len(value)
    return t
end

function file_exists (name)
	local f = io.open( name, "r" )
	if f ~= nil then
		io.close(f)
		return true
	else
		return false
	end
end

-- no need to have all the functions in every script
function on_msg_receive (msg) end
function on_our_id (id) end
function on_secret_chat_created (peer) end
function on_user_update (user) end
function on_user_status_update (user, online) end
function on_chat_update (user) end
function on_get_difference_end () end
function on_binlog_replay_end () end
