local req_stat = {}

function req_stat.incr(dict, key, increment)
   increment = increment or 1
   local newval, err = dict:incr(key, increment)
   if err then
      dict:set(key, increment)
      newval = increment
   end
   return newval
end

function req_stat.get(dict, key)
   local newval, err = dict:get(key)
   newval = newval or 0
   return newval
end

function req_stat.log(logfile,msg)
    local fd = io.open(logfile,"ab")
    if fd == nil then return end 
    fd:write(msg .. "\n")
    fd:flush()
    fd:close()
end

function req_stat.write(logfile,msg)
    req_stat.log(logfile,msg)
end

function req_stat.get_msg_id()
    local request_body = ngx.var.request_body or ""
    --req_stat.write("/tmp/api.stat.log", "body:[" .. request_body .. "]")
    local pos1, len = string.find(request_body,"msg_id=")
    if pos1 then
        local pos2 = string.find(request_body, "&", pos1+len)
        if pos2 and pos2 > pos1 then
            local msg_id = string.sub(request_body, pos1+len, pos2-1)
            return tonumber(msg_id)
        end
    end
    return 0
end

function req_stat.output_report(dict)
    local keys = dict:get_keys(512)
    if keys and next(keys) then
        for _, key in pairs (keys) do
            req_stat.log("/tmp/api.stat.log", "key:" .. key)
            local num = dict:get(key)
            ngx.say(key .. ":" .. num)
        end
    else
        req_stat.log("/tmp/api.stat.log", "empty dict")
    end
end

return req_stat
