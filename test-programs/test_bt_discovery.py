# 
# test_bt_discovery.py
# 
# Copyright 2008 Helsinki Institute for Information Technology (HIIT)
# and the authors. All rights reserved.
# 
# Authors: Tero Hasu <tero.hasu@hut.fi>
# 

# Permission is hereby granted, free of charge, to any person
# obtaining a copy of this software and associated documentation files
# (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge,
# publish, distribute, sublicense, and/or sell copies of the Software,
# and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
# BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
# ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
# CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

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
