import {pack, unpack} from "struct";
import * as socket from "socket";

let sock = socket.connect('127.0.0.1', 65500, {
	family: socket.AF_INET,
	socktype: socket.SOCK_STREAM
});

let data = {
	'type': ARGV[0],
	'data': ARGV[2] || {},
	'path': ARGV[1] || '/',
	'user': ARGV[3] || 'admin',
};

let pretty = sprintf("%#J", data); 

print(pretty);