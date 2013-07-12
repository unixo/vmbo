all:
	cd src && make && cp vmbo ..

clean:
	cd src && make clean
	rm -f vmbo PROC_*.log
