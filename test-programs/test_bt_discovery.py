# 
# test_bt_discovery.py
# 
# Copyright 2008 Helsinki Institute for Information Technology (HIIT)
# and the authors. All rights reserved.
# 
# Authors: Tero Hasu <tero.hasu@hut.fi>
# 

import e32
from pyaosocket import AoResolver

myLock = e32.Ao_lock()
count = 0
cont = None
rsv = AoResolver()

def cb(error, mac, name, dummy):
    global count
    global cont
    if error == -25: # KErrEof (no more devices)
        print "no more"
        cont = None
    elif error:
        raise
    else:
        count += 1
        print repr([mac, name, count])
        cont = rsv.next
    myLock.signal()

try:
    rsv.open()
    cont = lambda: rsv.discover(cb, None)
    while cont:
        cont()
        myLock.wait()
finally:
    rsv.close()

print "all done"
