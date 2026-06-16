class UnionFind
  def initialize
    @forwarded = {}
  end

  def makeset(x)
    raise "Already exists" if @forwarded.key?(x)
    @forwarded[x] = x
  end

  def find(x)
    result = x
    while @forwarded[result] != result
      result = @forwarded[result]
    end
    result
  end

  def union(x, y)
    x = find(x)
    y = find(y)
    if x != y
      @forwarded[y] = x
    end
  end
end

require "minitest/autorun"

class TestUnionFind < Minitest::Test
  def test_find_with_empty_set_returns_nil
    uf = UnionFind.new
    assert_nil(uf.find(1))
  end

  def test_makeset_adds_element_to_set
    uf = UnionFind.new
    uf.makeset(1)
    assert_equal(1, uf.find(1))
  end

  def test_union_merges_two_sets
    uf = UnionFind.new
    uf.makeset(1)
    uf.makeset(2)
    uf.union(1, 2)
    assert_equal(uf.find(1), uf.find(2))
  end

  def test_union_is_transitive
    uf = UnionFind.new
    uf.makeset(1)
    uf.makeset(2)
    uf.makeset(3)
    uf.union(2, 3)
    uf.union(1, 2)
    assert_equal(uf.find(1), uf.find(3))
  end

  def test_makeset_raises_error_if_element_already_exists
    uf = UnionFind.new
    uf.makeset(1)
    assert_raises(RuntimeError) { uf.makeset(1) }
  end
end
