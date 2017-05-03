local req_stat = require "req_stat"

local req_dict = ngx.shared.req_dict

req_stat.output_report(req_dict)
