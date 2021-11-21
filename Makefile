linux:
	nelua -b -o "Retrocord" main.nelua
windows:
	nelua --cc "/usr/bin/x86_64-w64-mingw32-gcc" -b -o "Retrocord" main.nelua
