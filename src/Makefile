CFLAGS+=-Wall -Os
LIBS=-lgramparser -L.

all: gram-test gramparser-test gramparser-test2 gramparser-test3 gramparser-test4

libgramparser.a: gramparser.o gram.o
	ar rcs $@ $^

gram-test: gram-test.c gram.o libgramparser.a
	$(CC) $^ -o $@ $(CFLAGS) $(LIBS)

gramparser-test: gramparser-test.c gramparser.o gram.o libgramparser.a
	$(CC) $^ -o $@ $(CFLAGS) $(LIBS)

gramparser-test2: gramparser-test2.c gramparser.o gram.o libgramparser.a
	$(CC) $^ -o $@ $(CFLAGS) $(LIBS)

gramparser-test3: gramparser-test3.c gramparser.o gram.o libgramparser.a
	$(CC) $^ -o $@ $(CFLAGS) $(LIBS)

gramparser-test4: gramparser-test4.c libgramparser.a gramparser-test4.h
	$(CC) $< -o $@ $(CFLAGS) $(LIBS)

gramparser-test4.h: gramparser-test4.peg gramparser-test4.awk
	awk -f gramparser-test4.awk < $< > $@

gram.o: gram.c gram.h
	$(CC) $< -o $@ -c $(CFLAGS)

gramparser.o: gramparser.c gramparser.h gram.h
	$(CC) $< -o $@ -c $(CFLAGS)

clean:
	rm -f gram.o gramparser.o gram-test gramparser-test gramparser-test2 gramparser-test3
