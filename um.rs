const PROGRAM_SEGMENT: usize = 0;
const NUM_REGISTERS: usize = 8;
const LOAD_VALUE_NUM_BITS: u32 = 25;
const LOAD_VALUE_MASK: u32 = (1 << LOAD_VALUE_NUM_BITS) - 1;

const OP_CONDITIONAL_MOVE: u32 = 0;
const OP_SEGMENTED_LOAD: u32 = 1;
const OP_SEGMENTED_STORE: u32 = 2;
const OP_ADDITION: u32 = 3;
const OP_MULTIPLICATION: u32 = 4;
const OP_DIVISION: u32 = 5;
const OP_BITWISE_NAND: u32 = 6;
const OP_HALT: u32 = 7;
const OP_MAP_SEGMENT: u32 = 8;
const OP_UNMAP_SEGMENT: u32 = 9;
const OP_OUTPUT: u32 = 10;
const OP_INPUT: u32 = 11;
const OP_LOAD_PROGRAM: u32 = 12;
const OP_LOAD_VALUE: u32 = 13;

type Data = u32;

fn um_run(program: Vec<Data>) {
    let mut segments = vec![program];
    let mut registers: [Data; NUM_REGISTERS] = [0, 0, 0, 0, 0, 0, 0, 0];
    let mut program_counter = 0;
    let mut free_segment_ids: Vec<Data> = vec![];
    loop {
        let program: &[Data] = &segments[PROGRAM_SEGMENT];
        let instruction = program[program_counter as usize];
        program_counter += 1;
        let c: usize = (instruction & 0b111) as usize;
        let b: usize = ((instruction >> 3) & 0b111) as usize;
        let a: usize = ((instruction >> 6) & 0b111) as usize;
        let opcode = (instruction >> 28) & 0xff;
        match opcode {
            OP_CONDITIONAL_MOVE => if registers[c] != 0 { registers[a] = registers[b] },
            OP_SEGMENTED_LOAD => registers[a] = segments[registers[b] as usize][registers[c] as usize],
            OP_SEGMENTED_STORE => segments[registers[a] as usize][registers[b] as usize] = registers[c],
            OP_ADDITION => registers[a] = registers[b].wrapping_add(registers[c]),
            OP_MULTIPLICATION => registers[a] = registers[b].wrapping_mul(registers[c]),
            OP_DIVISION => registers[a] = registers[b] / registers[c],
            OP_BITWISE_NAND => registers[a] = !(registers[b] & registers[c]),
            OP_HALT => break,
            OP_MAP_SEGMENT => {
                let size = registers[c] as usize;
                let segment = free_segment_ids.pop().unwrap_or_else(|| {
                    segments.push(vec![]);
                    (segments.len() - 1) as u32
                });
                segments[segment as usize].resize(size, 0);
                registers[b] = segment;
            }
            OP_UNMAP_SEGMENT => {
                let segment = registers[c];
                segments[segment as usize].clear();
                free_segment_ids.push(segment);
            }
            OP_OUTPUT => print!("{}", (registers[c] & 0xff) as u8 as char),
            OP_INPUT => {
                use std::io::Read;
                registers[c] = std::io::stdin().bytes().nth(0).map_or(Data::MAX, |b| b.unwrap() as Data);
            }
            OP_LOAD_PROGRAM => {
                if registers[b] != 0 {
                    let (before, after) = segments.split_at_mut(1);
                    let segment = &after[registers[b] as usize - 1];
                    before[PROGRAM_SEGMENT].clear();
                    before[PROGRAM_SEGMENT].extend(segment);
                }
                program_counter = registers[c];
            }
            OP_LOAD_VALUE => registers[((instruction >> LOAD_VALUE_NUM_BITS) & 0b111) as usize] = (instruction & LOAD_VALUE_MASK) as Data,
            _ => panic!("Unhandled opcode {}", opcode),
        }
    }
}

fn main() {
    let program: Vec<Data> = std::fs::read(std::env::args().nth(1).expect("No file provided"))
        .expect("Failed to read file")
        .chunks_exact(4)
        .map(|chunk| {
            use std::convert::TryInto;
            let bytes: [u8; 4] = chunk.try_into().expect("Chunk is not 4 bytes");
            Data::from_be_bytes(bytes)
        })
        .collect();
    um_run(program);
}
