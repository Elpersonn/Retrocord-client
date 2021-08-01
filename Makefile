fast:
	nelua -j main.nelua
linux:
	nelua -r -b -o bin/RetroCord main.nelua
windows:
	nelua --cc "/usr/bin/x86_64-w64-mingw32-gcc" --cflags="-static -fstack-protector" -r -b -o bin/RetroCord-win.exe main.nelua
web:
	nelua --cc "/usr/lib/emscripten/emcc" -r -b -o bin/retrocord.js main.nelua
