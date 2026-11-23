"""Portable maker policy: Python may contact only its local build server."""
import sys
import socket
import ipaddress


def local(host):
    if isinstance(host, bytes):
        host = host.decode('ascii', 'strict')
    if host is None or str(host).lower() == 'localhost':
        return True
    try:
        return ipaddress.ip_address(str(host)).is_loopback
    except ValueError:
        return False


def audit(event, args):
    if event == 'socket.getaddrinfo' and not local(args[0]):
        raise OSError('Portable ROM maker blocks external network access')
    if event == 'socket.connect':
        sock, address = args
        if sock.family in (socket.AF_INET, socket.AF_INET6) and not local(address[0]):
            raise OSError('Portable ROM maker blocks external network access')


sys.addaudithook(audit)
