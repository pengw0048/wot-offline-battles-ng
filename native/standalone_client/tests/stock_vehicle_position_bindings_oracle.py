"""Execute #1513 node-vector query and LOD position-owner selection unchanged."""
import random
from unicorn import UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX
from stock_battle_vehicle_animation_oracle import Machine, HEAP


def check(executable):
    m = Machine(executable, None)
    owner, pool, indices, records = (HEAP + n for n in (0x1000, 0x2000, 0x3000, 0x4000))
    m.word(owner + 8, pool)
    m.word(pool, records)
    m.word(pool + 0x10, indices)
    m.word(pool + 0x14, indices + 8)
    m.word(indices, 1)
    m.word(indices + 4, 0)
    rng = random.Random(1513)
    for count in range(64):
        logical = count % 2
        physical = 1 - logical
        m.word(owner + 0xc, logical)
        start = 0x12345000
        length = rng.randrange(8)
        m.word(records + physical * 80 + 0x38, start)
        m.word(records + physical * 80 + 0x3c, start + length * 68)
        m.call(0x781650, owner, [])
        assert bool(m.u.reg_read(UC_X86_REG_EAX) & 0xff) == bool(length)

    # setupPosition's native body resolves the current pool slot then writes
    # that slot's producer. std::function copying is a recorded boundary.
    assignments = []
    def assign():
        assignments.append((m.u.reg_read(UC_X86_REG_ECX), m.arg(0)))
        m._return(1)
    m.handlers[0x759090] = assign
    m.u.hook_add(UC_HOOK_CODE, m._hook, begin=0x759090, end=0x759090)
    for logical in (0, 1, 0, 1):
        m.word(owner + 0xc, logical)
        m.call(0x7613f0, owner, [0] * 10)
        assert assignments[-1][0] == records + (1 - logical) * 64 + 8
    assert len(assignments) == 4

    # The original callable functor asks Script::setData to update its
    # retained Vector3, then returns that same retained storage regardless
    # of conversion success. Python execution/conversion is the boundary.
    reader = HEAP + 0x6000
    outcome = [True]
    def produce():
        m._return(0, 0x1234)
    def convert():
        assert m.arg(0) == 0x1234 and m.arg(1) == reader + 4
        if outcome[0]:
            m.floats(m.arg(1), 3, [3., 4., 5.])
        m._return(0, 0 if outcome[0] else 0xffffffff)
    for address, handler in ((0x75b0e0, produce), (0x690240, convert)):
        m.handlers[address] = handler
        m.u.hook_add(UC_HOOK_CODE, m._hook, begin=address, end=address)
    for success in (True, False):
        outcome[0] = success
        m.call(0x759340, reader, [])
        assert m.u.reg_read(UC_X86_REG_EAX) == reader + 4
        assert m.floats(reader + 4, 3) == [3., 4., 5.]
    print('Exact #1513 hasGroundNodes: 64 vector cases; setupPosition: 4 current-owner replacements; callable cache success/failure passed')
