import socket
import pytest


def _get_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture
def qemu_host_port(request):
    if hasattr(request, 'param') and request.param:
        return request.param
    return _get_free_port()


@pytest.fixture
def qemu_extra_args(request, target, qemu_host_port):
    args = []
    if hasattr(request, 'param') and request.param:
        args.append(request.param.format(host_port=qemu_host_port))
    return " ".join(args)
