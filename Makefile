CC = clang
CFLAGS ?= -Os -Wall -Wextra -Werror
FRAMEWORKS = -framework IOKit -framework CoreFoundation
LABEL = com.sj26.webcam-settings
PLIST = $(HOME)/Library/LaunchAgents/$(LABEL).plist
BIN = $(HOME)/.local/bin/webcam-settings
DOMAIN = gui/$(shell id -u)

.PHONY: all check install uninstall clean
all: webcam-settings

webcam-settings: webcam-settings.c
	$(CC) -std=c11 $(CFLAGS) $(FRAMEWORKS) $< -o $@

check: webcam-settings
	$(CC) --analyze -std=c11 $(CFLAGS) -Xanalyzer -analyzer-output=text webcam-settings.c -o /dev/null
	plutil -lint $(LABEL).plist

install: webcam-settings
	@if launchctl print "$(DOMAIN)/$(LABEL)" >/dev/null 2>&1; then launchctl bootout "$(DOMAIN)/$(LABEL)"; fi
	mkdir -p "$(HOME)/.local/bin" "$(HOME)/Library/LaunchAgents"
	install -m 755 webcam-settings "$(BIN)"
	install -m 644 $(LABEL).plist "$(PLIST)"
	plutil -insert ProgramArguments.0 -string "$(BIN)" "$(PLIST)"
	launchctl bootstrap "$(DOMAIN)" "$(PLIST)"

uninstall:
	@if launchctl print "$(DOMAIN)/$(LABEL)" >/dev/null 2>&1; then launchctl bootout "$(DOMAIN)/$(LABEL)"; fi
	rm -f "$(PLIST)" "$(BIN)"

clean:
	rm -f webcam-settings
