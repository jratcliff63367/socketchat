# SocketChat


This is a relatively small application which demonstrates how to write both a client and
server over sockets.  It implements a simple chat client which sends and recieves messages
deineated with a carriage return and line-feed, like Redis does.

To build for windows you first need CMAKE installed on your machine.

Then run 'build_win64.bat' or 'generate_projects.bat' to create the Visual Studio Solution and project files

To build for Linux you need CMAKE installed on your machine.

Then run 'build_linux64.sh' or 'generate_projects.sh'

When you run ./TestClient pass the name of the server you are trying to connect to: i.e. "localhost"

