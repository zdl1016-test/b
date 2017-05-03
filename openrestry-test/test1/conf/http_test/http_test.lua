local http = require "resty.http"
local httpc = http.new()
--local res, err = httpc:request_uri("http://www.baidu.com")
local res, err = httpc:request_uri("http://127.0.0.1:8082/s")
if res.status == ngx.HTTP_OK then
    ngx.say(res.body)
else
    ngx.exit(res.status)
end
