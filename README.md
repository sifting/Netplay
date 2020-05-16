### Netplay

This is demo of a simple custom network protocol. It's implemented in UDP for
speed. It takes care of fragmenting messages large messages and reassembling
them, and may be extended with ease. Included with it is a simple client and
server. Network IO is done in a separate thread.