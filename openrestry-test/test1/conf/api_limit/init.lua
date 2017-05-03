local req_dict = ngx.shared.req_dict
req_dict:flush_all()
local req_stat = require "req_stat"
req_stat.log("/tmp/api.stat.log", "flush_all")
