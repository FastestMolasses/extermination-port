"""Test-only canonical callback adapter for the AREA01 instruction oracles.

Runtime owners never import this file. The callback points into the oracle's
existing buffers, with region arrays disabled on the translated side. It adds
no byte copies, and compares exactly the same memory as each original suite.
"""
import ctypes as C
import os

ENABLED = os.environ.get('EM_AREA01_CANONICAL_VIEW', '') not in ('', '0')
VIEW = C.CFUNCTYPE(C.c_void_p, C.c_void_p, C.c_uint32, C.c_uint32, C.c_int)


class CanonicalView:
    def __init__(self, regions, base='base'):
        self.regions = tuple((getattr(r, base), r.size, C.cast(r.bytes, C.c_void_p).value) for r in regions)
        self.loads = self.stores = 0
        self.callback = VIEW(self.resolve)

    def resolve(self, _ctx, address, size, write):
        if write == 0:
            self.loads += 1
        elif write == 1:
            self.stores += 1
        else:
            return None
        for start, count, ptr in self.regions:
            if ptr and size and start <= address and address + size <= start + count:
                return ptr + address - start
        return None

    def install(self, state):
        state.view = C.cast(self.callback, C.c_void_p).value
        state.regions = None
        state.region_count = 0

    def install_render(self, world):
        world.view = C.cast(self.callback, C.c_void_p).value
        world.views = None
        world.view_count = 0
