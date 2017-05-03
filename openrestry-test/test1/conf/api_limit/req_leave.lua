local req_stat = require "req_stat"

local req_dict = ngx.shared.req_dict

local num_all = req_stat.incr(req_dict, "all", -1)

-- 判断请求的并发数量是否达到限制
local msg_id = ngx.ctx.msg_id or 0

req_stat.incr(req_dict, msg_id, -1)
local num = req_stat.get(req_dict, msg_id)

req_stat.log("/tmp/api.stat.log", "\nmsg_id:" .. msg_id .. ",after leave num:" .. num)
