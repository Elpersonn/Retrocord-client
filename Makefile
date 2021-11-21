all: info linux windows

info:
	@echo "Building all"

linux:
	@echo "Building Linux Binary"
	@nelua -b -o "Retrocord" main.nelua

windows:
	@echo "Building Windows Exec"
	@nelua --cc "/usr/bin/x86_64-w64-mingw32-gcc" -b -o "Retrocord" main.nelua
