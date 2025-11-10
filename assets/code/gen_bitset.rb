require 'set'
require 'yaml'

# Type represents not just a Ruby class but a named union of other types.
class Type
  attr_accessor :name, :subtypes

  def initialize name, subtypes=nil
    @name = name
    @subtypes = subtypes || []
  end

  def all_subtypes
    subtypes.flat_map { |subtype| subtype.all_subtypes } + subtypes
  end

  def add_subtypes types
    types.each do |(name, h)|
      sub = Type.new name
      @subtypes << sub
      sub.add_subtypes h.to_a
    end
  end
end

# Helper to generate graphviz.
def to_graphviz_rec type
  type.subtypes.each {|subtype|
    puts type.name + "->" + subtype.name + ";"
  }
  type.subtypes.each {|subtype|
    to_graphviz_rec subtype
  }
end

# Generate graphviz.
def to_graphviz type
  puts "digraph G {"
  to_graphviz_rec type
  puts "}"
end

# ===== Start generating the type DAG =====

any = nil

File.open("assets/code/repository.yml", "rb") do |file|
  tree = YAML::load(file.read())
  raise "Must have exactly one root" if tree.keys.length != 1
  root_name = tree.keys.first
  any = Type.new root_name
  any.add_subtypes tree[root_name].to_a
end

# Assign individual bits to type leaves and union bit patterns to nodes with subtypes
num_bits = 0
$bits = {"Empty" => ["0u64"]}
$numeric_bits = {"Empty" => 0}
Set[any, *any.all_subtypes].sort_by(&:name).each {|type|
  subtypes = type.subtypes
  if subtypes.empty?
    # Assign bits for leaves
    $bits[type.name] = ["1u64 << #{num_bits}"]
    $numeric_bits[type.name] = 1 << num_bits
    num_bits += 1
  else
    # Assign bits for unions
    $bits[type.name] = subtypes.map(&:name).sort
  end
}
[*any.all_subtypes, any].each {|type|
  subtypes = type.subtypes
  unless subtypes.empty?
    $numeric_bits[type.name] = subtypes.map {|ty| $numeric_bits[ty.name]}.reduce(&:|)
  end
}

# ===== Finished generating the DAG; write Rust code =====

puts "mod bits {"
$bits.keys.sort_by {|type_name| $numeric_bits[type_name]}.map {|type_name|
  subtypes = $bits[type_name].join(" | ")
  puts "  pub const #{type_name}: u64 = #{subtypes};"
}
puts "  pub const NumTypeBits: u64 = #{num_bits};
}"
