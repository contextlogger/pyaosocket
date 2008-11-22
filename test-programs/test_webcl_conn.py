# 
# Copyright 2004-2008 Helsinki Institute for Information Technology
# (HIIT) and the authors. All rights reserved.
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
from pyaosocket import AoSocketServ, AoConnection, AoSocket
from socket import select_access_point

apid = select_access_point()

myLock = e32.Ao_lock()

def cb(*args):
    print repr(args)
    myLock.signal()

serv = AoSocketServ()
serv.connect()
try:
    conn = AoConnection()
    try:
        conn.open(serv, apid)
        s = AoSocket()
        try:
            s.set_socket_serv(serv)
            s.set_connection(conn)
            s.open_tcp()
            s.connect_tcp(u"pdis.hiit.fi", 80, cb, "connect")
            myLock.wait()
            s.write_data("GET / HTTP/1.0\n\n", cb, "write")
            myLock.wait()
            s.read_some(256, cb, "read")
            myLock.wait()
        finally:
            s.close()
    finally:
        conn.close()
finally:
    serv.close()

print "all done"
