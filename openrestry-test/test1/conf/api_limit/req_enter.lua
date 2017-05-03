local req_stat = require "req_stat"
local req_dict = ngx.shared.req_dict


req_stat.incr(req_dict, "all")

local msg_id = req_stat.get_msg_id()
ngx.ctx.msg_id = msg_id
req_stat.incr(req_dict, msg_id, 1)

