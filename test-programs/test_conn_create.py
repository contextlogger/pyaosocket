import e32
from socket import select_access_point
from pyaosocket import AoSocketServ, AoConnection

serv = AoSocketServ()
serv.connect()
try:
    apid = select_access_point()
    conn = AoConnection()
    try:
        conn.open(serv, apid)
    finally:
        conn.close()
finally:
    serv.close()

print "all done"
