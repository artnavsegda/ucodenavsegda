import {pack, unpack} from "struct";
import * as socket from "socket";

let sock = socket.connect('127.0.0.1', 65500, {
	family: socket.AF_INET,
	socktype: socket.SOCK_STREAM
});

let data = {
	'type': ARGV[1],
	'data': ARGV[3] || {},
	'path': ARGV[2] || '/',
	'user': ARGV[4] || 'admin',
};