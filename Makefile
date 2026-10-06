CFLAGS  = -O2 -Wall -Wextra -Wno-deprecated-declarations -arch arm64 -arch x86_64 -mmacosx-version-min=13.0
LDLIBS  = -lcups

rastertotspl: rastertotspl.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)
	codesign --force --sign - $@

test: rastertotspl
	cd test && ./compare.sh && python3 checks.py

# Needs sudo.
install: rastertotspl
	./install.sh

clean:
	rm -f rastertotspl

.PHONY: test install clean
