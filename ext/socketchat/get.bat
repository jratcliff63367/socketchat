@echo off
set SOURCE=F:\nvidiagit\backend\api_server_cpp\src
set SOURCE_INCLUDE=f:\nvidiagit\backend\api_server_cpp\include

copy %SOURCE%\*.cpp
copy %SOURCE_INCLUDE%\*.h

del FastXOR.cpp
del FastXOR.h
del easywsclient.cpp
del easywsclient.h
del ApiGen.cpp
del ApiGen.h
del ApiServer.cpp
del ApiServer.h
del InputLine.cpp
del InputLine.h
del FileURI.cpp
del FileURI.h
del InParser.cpp
del InParser.h
del MemoryStream.h
del StringId.h
del itoa_jeaiii.cpp
del itoa_jeaiii.h
