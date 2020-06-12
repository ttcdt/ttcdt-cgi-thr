PRJ=ttcdt-cgi-thr
PREFIX=/usr/local/bin

$(PRJ): $(PRJ).c
	gcc -Wall -g $< -o $@

install:
	install $(PRJ) $(PREFIX)/$(PRJ)

uninstall:
	rm -f $(PREFIX)/$(PRJ)

dist: clean
	rm -f ttcdt-cgi-thr.tar.gz && cd .. && \
        tar czvf ttcdt-cgi-thr/ttcdt-cgi-thr.tar.gz ttcdt-cgi-thr/*

clean:
	rm -f $(PRJ) *.tar.gz *.asc

