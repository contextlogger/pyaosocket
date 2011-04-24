BASENAME := pyaosocket
VERSION := 2.03
KIT := s60_30
CERT := dev

bin :
	sake --trace kits=$(KIT) udeb=true logging=true cert=$(CERT)

bin-all :
	rm -r build
	sake all release kits=s60_20
	sake all release kits=s60_26
	sake all release kits=s60_30 cert=self
	sake all release kits=s60_30 cert=dev

.PHONY : web

web :
	cp -a ../tools/web/hiit.css web/
	../tools/bin/txt2tags --target xhtml --infile web/index.txt2tags.txt --outfile web/index.html --encoding utf-8 --verbose -C web/config.t2t

HTDOCS := ../contextlogger.github.com
PAGEPATH := pyaosocket
PAGEHOME := $(HTDOCS)/$(PAGEPATH)
DLPATH := $(PAGEPATH)/download
DLHOME := $(HTDOCS)/$(DLPATH)
MKINDEX := ../tools/bin/make-index-page.rb

release :
	-mkdir -p $(DLHOME)
	cp -a download/* $(DLHOME)/
	$(MKINDEX) $(DLHOME)
	cp -a web/*.css $(PAGEHOME)/
	cp -a web/*.html $(PAGEHOME)/
	chmod -R a+rX $(PAGEHOME)

upload :
	cd $(HTDOCS) && git add $(PAGEPATH) && git commit -a -m updates && git push

-include local/custom.mk
