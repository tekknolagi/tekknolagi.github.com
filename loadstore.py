# See LICENSE for license.
import pytest
import random
import re
from typing import Optional, Any, List, Tuple, Dict


class Value:
    def find(self):
        raise NotImplementedError("abstract")

    def _set_forwarded(self, value):
        raise NotImplementedError("abstract")


class Operation(Value):
    def __init__(self, name: str, args: List[Value]):
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
        assert isinstance(value, Constant) and value.value == self.value


class HeapRange:
    def __init__(self, start: int, end: int) -> None:
        self.start = start
        self.end = end

    def __repr__(self) -> str:
        return f"[{self.start}, {self.end})"

    def is_empty(self) -> bool:
        return self.start == self.end

    def overlaps(self, other: "HeapRange") -> bool:
        # Empty ranges interfere with nothing
        if self.is_empty() or other.is_empty():
            return False
        return self.end > other.start and other.end > self.start


class AbstractHeap:
    def __init__(self, name: str) -> None:
        self.name = name
        self.parent = None
        self.children = []
        self.range = None

    def add_child(self, name: str) -> None:
        result = AbstractHeap(name)
        result.parent = self
        self.children.append(result)
        return result

    def compute(self, start: int) -> None:
        current = start
        if not self.children:
            self.range = HeapRange(start, current + 1)
            return
        for child in self.children:
            child.compute(current)
            current = child.range.end
        self.range = HeapRange(start, current)

    def all_heaps(self) -> List["AbstractHeap"]:
        result = [self]
        for child in self.children:
            result.extend(child.all_heaps())
        return result


Any = AbstractHeap("Any")
Object = Any.add_child("Object")
Array = Object.add_child("Array")
String = Object.add_child("String")
Other = Any.add_child("Other")
Any.compute(0)


class Block(list):
    def opbuilder(opname: str):
        def wraparg(arg):
            if not isinstance(arg, Value):
                arg = Constant(arg)
            return arg

        def build(self, *args):
            # construct an Operation, wrap the
            # arguments in Constants if necessary
            op = Operation(opname, [wraparg(arg) for arg in args])
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
        arguments = ", ".join(arg_to_str(op.arg(i)) for i in range(len(op.args)))
        strop = f"{var} = {op.name}({arguments})"
        res.append(strop)
    return "\n".join(res)


def get_num(op, index=1):
    assert isinstance(op.arg(index), Constant)
    return op.arg(index).value


def eq_value(left: Value | None, right: Value) -> bool:
    if isinstance(left, Constant) and isinstance(right, Constant):
        return left.value == right.value
    return left is right


def may_alias(left: Value, right: Value) -> bool:
    # Consider also: newness
    # See Hydrogen lightweight alias analysis
    return (left.info or Any).range.overlaps((right.info or Any).range)


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
                or not may_alias(load_info[0], obj)
                # DeMorgan's law:
                # if load_info[1] == offset
                # and may_alias(load_info[0], obj)
            }
            compile_time_heap[store_info] = new_value
        elif op.name == "load":
            load_info = (op.arg(0), get_num(op, 1))
            if load_info in compile_time_heap:
                op.make_equal_to(compile_time_heap[load_info])
                continue
            compile_time_heap[load_info] = op
        else:
            # TODO(max): If this could write memory, invalidate compile_time_heap
            pass
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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""
    )


def test_store_to_same_object_offset_invalidates_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.store(var0, 0, 5)
    var3 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var3)
    opt_bb = optimize_load_store(bb)
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = store(var0, 0, 5)
var3 = escape(var1)
var4 = escape(5)"""
    )


def test_store_to_same_offset_different_heaps_does_not_invalidate_load():
    bb = Block()
    var0 = bb.getarg(0)
    var0.info = Array
    var1 = bb.getarg(1)
    var1.info = String
    var2 = bb.store(var0, 0, 3)
    var3 = bb.store(var1, 0, 4)
    var4 = bb.load(var0, 0)
    bb.escape(var4)
    opt_bb = optimize_load_store(bb)
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = store(var0, 0, 3)
var3 = store(var1, 0, 4)
var4 = escape(3)"""
    )


def test_store_to_same_object_different_offset_does_not_invalidate_load():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.store(var0, 4, 5)
    var3 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var3)
    opt_bb = optimize_load_store(bb)
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = store(var0, 4, 5)
var3 = escape(var1)
var4 = escape(var1)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = load(var0, 0)
var3 = store(var1, 0, 5)
var4 = load(var0, 0)
var5 = escape(var2)
var6 = escape(var4)"""
    )


def test_load_after_store_removed():
    bb = Block()
    var0 = bb.getarg(0)
    bb.store(var0, 0, 5)
    var1 = bb.load(var0, 0)
    var2 = bb.load(var0, 1)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = store(var0, 0, 5)
var2 = load(var0, 1)
var3 = escape(5)
var4 = escape(var2)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = store(var0, 0, 5)
var2 = store(var0, 0, 7)
var3 = escape(5)
var4 = escape(7)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = store(var0, 0, 5)
var3 = store(var1, 0, 7)
var4 = load(var0, 0)
var5 = escape(var4)
var6 = escape(7)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = store(var0, 0, 5)
var3 = store(var1, 1, 7)
var4 = escape(5)
var5 = escape(7)"""
    )


def test_two_loads():
    bb = Block()
    var0 = bb.getarg(0)
    var1 = bb.load(var0, 0)
    var2 = bb.load(var0, 0)
    bb.escape(var1)
    bb.escape(var2)
    opt_bb = optimize_load_store(bb)
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = load(var0, 0)
var2 = escape(var1)
var3 = escape(var1)"""
    )


def test_store_after_store():
    bb = Block()
    arg1 = bb.getarg(0)
    bb.store(arg1, 0, 5)
    bb.store(arg1, 0, 5)
    opt_bb = optimize_load_store(bb)
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = store(var0, 0, 5)"""
    )


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
    assert (
        bb_to_str(opt_bb)
        == """\
var0 = getarg(0)
var1 = getarg(1)
var2 = load(var0, 0)
var3 = load(var1, 0)
var4 = escape(var2)
var5 = escape(var3)"""
    )


def generate_program():
    bb = Block()
    args = [bb.getarg(i) for i in range(3)]
    num_ops = random.randint(0, 10)
    ops_with_values = args[:]
    for _ in range(num_ops):
        op = random.choice(["load", "store", "escape"])
        arg = random.choice(args)
        offset = random.randint(0, 4)
        if op == "load":
            v = bb.load(arg, offset)
            ops_with_values.append(v)
        elif op == "store":
            value = random.randint(0, 10)
            bb.store(arg, offset, value)
        elif op == "escape":
            a_value = random.choice(ops_with_values)
            bb.escape(a_value)
        else:
            raise NotImplementedError(f"Unknown operation {op}")
    return bb


def interpret_program(bb, args):
    heap = {}
    ssa = {}
    escaped = []
    for op in bb:
        if op.name == "getarg":
            ssa[op] = args[get_num(op, 0)]
        elif op.name == "store":
            obj = ssa[op.arg(0)]
            offset = get_num(op, 1)
            value = get_num(op, 2)
            heap[(obj, offset)] = value
        elif op.name == "load":
            obj = ssa[op.arg(0)]
            offset = get_num(op, 1)
            unknown = "unknown"
            value = heap.get((obj, offset), "unknown")
            ssa[op] = value
        elif op.name == "escape":
            value = op.arg(0)
            if isinstance(value, Constant):
                escaped.append(value.value)
            else:
                escaped.append(ssa[value])
        else:
            raise NotImplementedError(f"Unknown operation {op.name}")
    heap["escaped"] = escaped
    return heap


def verify_program(bb):
    before_no_alias = interpret_program(bb, ["a", "b", "c"])
    a = "a"
    before_alias = interpret_program(bb, [a, a, a])
    optimized = optimize_load_store(bb)
    after_no_alias = interpret_program(optimized, ["a", "b", "c"])
    after_alias = interpret_program(optimized, [a, a, a])
    assert before_no_alias == after_no_alias
    assert before_alias == after_alias


def test_random_programs():
    random.seed(0)
    num_programs = 100000
    for i in range(num_programs):
        program = generate_program()
        verify_program(program)
