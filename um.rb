PROGRAM_SEGMENT = 0
NUM_REGISTERS = 8
LOAD_VALUE_NUM_BITS = 25
LOAD_VALUE_MASK = (1 << LOAD_VALUE_NUM_BITS) - 1

OP_CONDITIONAL_MOVE = 0
OP_SEGMENTED_LOAD = 1
OP_SEGMENTED_STORE = 2
OP_ADDITION = 3
OP_MULTIPLICATION = 4
OP_DIVISION = 5
OP_BITWISE_NAND = 6
OP_HALT = 7
OP_MAP_SEGMENT = 8
OP_UNMAP_SEGMENT = 9
OP_OUTPUT = 10
OP_INPUT = 11
OP_LOAD_PROGRAM = 12
OP_LOAD_VALUE = 13

def um_run(program)
  segments = [program]
  registers = [0] * NUM_REGISTERS
  program_counter = 0
  free_segment_ids = []
  while true
    instruction = program[program_counter]
    program_counter += 1
    c = instruction & 0b111
    b = (instruction >> 3) & 0b111
    a = (instruction >> 6) & 0b111
    opcode = (instruction >> 28) & 0xff
    case opcode
    when OP_CONDITIONAL_MOVE
      if registers[c] != 0
        registers[a] = registers[b]
      end
    when OP_SEGMENTED_LOAD
      registers[a] = segments[registers[b]][registers[c]]
    when OP_SEGMENTED_STORE
      segment = segments[registers[a]]
      raise "Location out of bounds" if registers[b] >= segment.length
      segment[registers[b]] = registers[c]
    when OP_ADDITION
      registers[a] = (registers[b] + registers[c]) & 0xffffffff
    when OP_MULTIPLICATION
      registers[a] = (registers[b] * registers[c]) & 0xffffffff
    when OP_DIVISION
      registers[a] = registers[b] / registers[c]
    when OP_BITWISE_NAND
      # Something something NAND is weird on Ruby bignum
      registers[a] = ~(registers[b] & registers[c]) & ((1 << 32) - 1)
    when OP_HALT
      break
    when OP_MAP_SEGMENT
      size = registers[c]
      if free_segment_ids.empty?
        segments << [0] * size
        registers[b] = segment_id = segments.length - 1
      else
        registers[b] = segment_id = free_segment_ids.pop
        segments[segment_id].fill(0, 0, size)
      end
    when OP_UNMAP_SEGMENT
      free_segment_ids << registers[c]
    when OP_OUTPUT
      char = registers[c]
      raise "Char too small" if char < 0
      raise "Char too big" if char > 255
      printf "%c", char
    when OP_INPUT
      byte = STDIN.read(1)
      if byte
        registers[c] = byte.ord
      else
        registers[c] = 0xffffffff
      end
    when OP_LOAD_PROGRAM
      if registers[b] != 0
        program = segments[PROGRAM_SEGMENT]
        program.clear
        program.concat(segments[registers[b]])
      end
      program_counter = registers[c]
    when OP_LOAD_VALUE
      value = instruction & LOAD_VALUE_MASK
      a = (instruction >> LOAD_VALUE_NUM_BITS) & 0b111
      registers[a] = value
    else
      raise "Invalid opcode #{opcode}"
    end
  end
end

filename = ARGV[0]
file = File.open(filename, "rb")
program = []
loop do
  instruction = file.read(4)&.unpack("L>")&.[](0)
  break unless instruction
  program << instruction
end
puts "Done loading."
um_run(program)
