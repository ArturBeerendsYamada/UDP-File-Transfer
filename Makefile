all:
	g++ -o serverside\server .\server.cpp .\Address.cpp .\Socket.cpp .\SHA256.cpp -lws2_32
	g++ -o clientside\client .\client.cpp .\Address.cpp .\Socket.cpp .\SHA256.cpp -lws2_32

linux:
	g++ -o serverside/server server.cpp Address.cpp Socket.cpp SHA256.cpp
	g++ -o clientside/client client.cpp Address.cpp Socket.cpp SHA256.cpp

server:
	g++ -o serverside\server .\server.cpp .\Address.cpp .\Socket.cpp .\SHA256.cpp -lws2_32

client:
	g++ -o clientside\client .\client.cpp .\Address.cpp .\Socket.cpp .\SHA256.cpp -lws2_32