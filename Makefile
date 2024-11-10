all:
	g++ -o serverside\server .\server.cpp .\Address.cpp .\Socket.cpp -lws2_32
	g++ -o clientside\client .\client.cpp .\Address.cpp .\Socket.cpp -lws2_32

server:
	g++ -o serverside\server .\server.cpp .\Address.cpp .\Socket.cpp -lws2_32

client:
	g++ -o clientside\client .\client.cpp .\Address.cpp .\Socket.cpp -lws2_32