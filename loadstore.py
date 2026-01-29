# See LICENSE for license.
import pytest
import re
from typing import Optional, Any, List, Tuple, Dict


class Value:
    def find(self):
        raise NotImplementedError("abstract")

    def _set_forwarded(self, value):
        raise NotImplementedError("abstract")


class Operation(Value):
    def __init__(
        self, name: str, args: List[Value]
    ):
        self.name = name
        self.args = args
        self.forwarded = None
        self.info = None

    def __repr__(self):
        return (
            f"Operation({self.name}, "
            f"{self.args}, {self.forwarded}, "
            f"{self.info})"
        )

    def find(self) -> Value:
        op = self
        while isinstance(op, Operation):
            next = op.forwarded
            if next is None:
                return op
            op = next
        return op

    def arg(self, index):
        return self.args[index].find()

    def make_equal_to(self, value: Value):
        self.find()._set_forwarded(value)

    def _set_forwarded(self, value: Value):
        self.forwarded = value


class Constant(Value):
    def __init__(self, value: Any):
        self.value = value

    def __repr__(self):
        return f"Constant({self.value})"

    def find(self):
        return self

    def _set_forwarded(self, value: Value):
        assert (
            isinstance(value, Constant)
            and value.value == self.value
        )

class Block(list):
    def opbuilder(opname: str):
        def wraparg(arg):
            if not isinstance(arg, Value):
                arg = Constant(arg)
            return arg
        def build(self, *args):
            # construct an Operation, wrap the
            # arguments in Constants if necessary
            op = Operation(opname,
                [wraparg(arg) for arg in args])
            # add it to self, the basic block
            self.append(op)
            return op
        return build

    # a bunch of operations we support
    add = opbuilder("add")
    mul = opbuilder("mul")
    getarg = opbuilder("getarg")
    dummy = opbuilder("dummy")
    lshift = opbuilder("lshift")
    # some new one for this post
    alloc = opbuilder("alloc")
    load = opbuilder("load")
    store = opbuilder("store")
    alias = opbuilder("alias")
    escape = opbuilder("escape")

def bb_to_str(bb: Block, varprefix: str = "var"):
    def arg_to_str(arg: Value):
        if isinstance(arg, Constant):
            return str(arg.value)
        else:
            return varnames[arg]

    varnames = {}
    res = []
    for index, op in enumerate(bb):
        var = f"{varprefix}{index}"
        varnames[op] = var
        arguments = ", ".join(
            arg_to_str(op.arg(i))
                for i in range(len(op.args))
        )
        strop = f"{var} = {op.name}({arguments})"
        res.append(strop)
    return "\n".join(res)

def get_num(op, index=1):
    assert isinstance(op.arg(index), Constant)
    return op.arg(index).value

def eq_value(left: Value|None, right: Value) -> bool:
    if isinstance(left, Constant) and isinstance(right, Constant):
        return left.value == right.value
    return left is right

def optimize_load_store(bb: Block):
    opt_bb = Block()
    # Stores things we know about the heap at... compile-time.
    # Key: an object and an offset pair acting as a heap address
    # Value: a previous SSA value we know exists at that address
    compile_time_heap: Dict[Tuple[Value, int], Value] = {}
    for op in bb:
        if op.name == "store":
            obj = op.arg(0)
            offset = get_num(op, 1)
            store_info = (obj, offset)
            current_value = compile_time_heap.get(store_info)
            new_value = op.arg(2)
            if eq_value(current_value, new_value):
                continue
            compile_time_heap = {
                load_info: value
                for load_info, value in compile_time_heap.items()
                if load_info[1] != offset
            }
            compile_time_heap[store_info] = new_value
        elif op.name == "load":
            load_info = (op.arg(0), get_num(op, 1))
            if load_info in compile_time_heap:
                op.make_equal_to(compile_time_heap[load_info])
                continue
            compile_time_heap[load_info] = op
        opt_bb.append(op)
    return opt_bb

def test_two_loads():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""

def test_store_to_same_object_offset_invalidates_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.store(var0, 0, 5)
    var3 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var3)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = store(var0, 0, 5)
var3 = escape(var1)
var4 = escape(5)"""

def test_store_to_same_object_different_offset_does_not_invalidate_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.store(var0, 4, 5)
    var3 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var3)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = store(var0, 4, 5)
var3 = escape(var1)
var4 = escape(var1)"""

def test_store_at_same_offset_invalidates_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.getarg(1)
    var2 = bb.load(var0, 0)
    var3 = bb.store(var1, 0, 5)
    var4 = bb.load(var0, 0)
    bb.escape(var2)
    bb.escape(var4)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = load(var0, 0)
var3 = store(var1, 0, 5)
var4 = load(var0, 0)
var5 = escape(var2)
var6 = escape(var4)"""

def test_load_after_store_removed():
    bb = Block()
    var0 = bb.getarg(0)
    bb.store(var0, 0, 5)
    var1 = bb.load(var0, 0)
    var2 = bb.load(var0, 1)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = store(var0, 0, 5)
var2 = load(var0, 1)
var3 = escape(5)
var4 = escape(var2)"""

def test_loads_between_stores_removed():
    bb = Block()
    var0 = bb.getarg(0)
    bb.store(var0, 0, 5)
    var1 = bb.load(var0, 0)
    bb.store(var0, 0, 7)
    var2 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = store(var0, 0, 5)
var2 = store(var0, 0, 7)
var3 = escape(5)
var4 = escape(7)"""

def test_two_stores_same_offset():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.getarg(1)
    bb.store(var0, 0, 5)
    bb.store(var1, 0, 7)
    load1 = bb.load(var0, 0)
    load2 = bb.load(var1, 0)
    bb.escape(load1)
    bb.escape(load2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = store(var0, 0, 5)
var3 = store(var1, 0, 7)
var4 = load(var0, 0)
var5 = escape(var4)
var6 = escape(7)"""

def test_two_stores_different_offset():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.getarg(1)
    bb.store(var0, 0, 5)
    bb.store(var1, 1, 7)
    load1 = bb.load(var0, 0)
    load2 = bb.load(var1, 1)
    bb.escape(load1)
    bb.escape(load2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = store(var0, 0, 5)
var3 = store(var1, 1, 7)
var4 = escape(5)
var5 = escape(7)"""

def test_two_loads():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""

def test_load_store_load():
    bb = Block()
    arg1 = bb.getarg(0)
    arg2 = bb.getarg(1)
    var1 = bb.load(arg1, 0)
    bb.store(arg2, 0, 123)
    var2 = bb.load(arg1, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    # Cannot optimize :(
    assert bb_to_str(opt_bb) == bb_to_str(bb)

def test_load_then_store():
    bb = Block()
    arg1 = bb.getarg(0)
    var1 = bb.load(arg1, 0)
    bb.store(arg1, 0, var1)
    bb.escape(var1)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)"""

# TODO(max): Test above with aliasing objects

def test_load_then_store_then_load():
    bb = Block()
    arg1 = bb.getarg(0)
    var1 = bb.load(arg1, 0)
    bb.store(arg1, 0, var1)
    var2 = bb.load(arg1, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""

def test_store_after_store():
    bb = Block()
    arg1 = bb.getarg(0)
    bb.store(arg1, 0, 5)
    bb.store(arg1, 0, 5)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = store(var0, 0, 5)"""

def test_load_store_aliasing():
    bb = Block()
    arg0 = bb.getarg(0)
    arg1 = bb.getarg(1)
    var0 = bb.load(arg0, 0)
    var1 = bb.load(arg1, 0)
    var2 = bb.store(arg0, 0, var0)
    var3 = bb.load(arg0, 0)
    var4 = bb.load(arg1, 0)
    bb.escape(var3)
    bb.escape(var4)
    # In the non-aliasing case (arg0 is not arg1), then we can remove:
    # * var2, because we are storing the result of a read;
    # * var3, because we know what we just stored in var2;
    # * var4, because we know the store in var2 did not affect arg1 and we
    #   already have a load
    # In the aliasing case (arg0 is arg1), then we can remove:
    # * var1, because we have already loaded off the same object in var0;
    # * var2, because we are storing the result of a read;
    # * var3, because we know what we just stored in var2;
    # * var4, for the same reason as above
    # Because we don't know if they alias or not, we can only remove the
    # intersection of the above two cases: var2, var3, var4.
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = load(var0, 0)
var3 = load(var1, 0)
var4 = escape(var2)
var5 = escape(var3)"""

@pytest.mark.xfail
def test_exercise_for_the_viewer():
    bb = Block()
    arg0 = bb.getarg(0)
    var0 = bb.store(arg0, 0, 5)
    var1 = bb.store(arg0, 0, 7)
    var2 = bb.load(arg0, 0)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert bb_to_str(opt_bb) == """\
var0 = getarg(0)
var1 = store(var0, 0, 7)
var2 = escape(7)"""
