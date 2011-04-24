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

web :
	sake web
