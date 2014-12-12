peggrep: peggrep.c ../src/libgramparser.a
	$(CC) $< -o $@ -lgramparser -L../src -I../src

clean:
	rm -f peggrep

../src/libgramparser.a:
	$(MAKE) -C ../src
