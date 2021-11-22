all: info linux windows

info:
	@echo "Building all"
	@echo ""
	@echo -e "To build for a specific platform, use \n\
$(tput bold)\e[95mmake [\e[3mwindows linux\e[0m\e[95m]\e[0m\n"

linux:
	@echo "Building Linux Binary"
	@nelua -b -o "Retrocord" main.nelua
	@echo "Generating Run Script"
	@echo "#!/usr/bin/env bash" >> run.sh
	@echo "export WEBFILES=`pwd`" >> run.sh
	@echo "./Retrocord" >> run.sh
	@chmod +x run.sh

windows:
	@echo "Building Windows Exec"
	@nelua --cc "/usr/bin/x86_64-w64-mingw32-gcc" -b -o "Retrocord" main.nelua
	@echo "Generating Run Script"
	@echo "@ECHO OFF" >> run.bat
	@echo "SET WEBFILES=%cd%" >> run.bat
	@echo "Retrocord.exe" >> run.bat

clean:
	@rm -f Retrocord Retrocord.exe run.bat run.sh -v
