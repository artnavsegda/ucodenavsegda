import {pack, unpack} from "struct";
import * as socket from "socket";

function sock_send(sock, data) {
	let buff = sprintf("%J", data);
	let ll = pack("<L", length(buff));
	return sock.send(ll + buff);
}

function sock_recv(sock) {
	let data, ll;

	data = sock.recv(4);
	if(!data) { return data; }

	ll = unpack("<L", data)[0];

	data = '';
	while(ll) {
		let c = sock.recv( (ll > CHUNK_TCP_SIZE ? CHUNK_TCP_SIZE : ll) );
		if(!c) return {error: "socket: connection closed"};

		data += c;
		ll -= length(c);
	}

	if(!data) { return data; }

	try {
		data = json(data);
	} catch(err) {
		return {error: "socket: invalid json data"};
	}

	return data;
}

let data = {
	'type': ARGV[0],
	'data': ARGV[2] || {},
	'path': ARGV[1] || '/',
	'user': ARGV[3] || 'admin',
};

let pretty = sprintf("%#J\n", data); 

print(pretty);

let sock = socket.connect('127.0.0.1', 65500, {
	family: socket.AF_INET,
	socktype: socket.SOCK_STREAM
});

if(!sock) {
	print("Socket error\n");
	exit(1)
}

sock_send(sock, data);
data = sock_recv(sock);
pretty = sprintf("%#J\n", data); 
print(pretty);
sock.close();