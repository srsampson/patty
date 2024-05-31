all:
	$(MAKE) -C src all
	$(MAKE) -C bin all
	$(MAKE) -C examples all

install:
	$(MAKE) -C src install
	$(MAKE) -C bin install
	$(MAKE) -C doc install

clean:
	$(MAKE) -C src clean
	$(MAKE) -C bin clean
	$(MAKE) -C examples clean
