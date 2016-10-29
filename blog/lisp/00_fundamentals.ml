type stream =
  { mutable line_num: int; mutable chr: char list; chan: in_channel };;

let read_char stm =
    match stm.chr with
      | [] ->
              let c = input_char stm.chan in
              if c = '\n' then let _ = stm.line_num <- stm.line_num + 1 in c
              else c
      | c::rest ->
              let _ = stm.chr <- rest in c

let unread_char stm c =
  stm.chr <- c :: stm.chr;;

let is_white c =
  c = ' ' || c = '\t' || c = '\n';;

let rec eat_whitespace stm =
  let c = read_char stm in
  if is_white c then
    eat_whitespace stm
  else
    unread_char stm c;
    ();;

type lobject =
  | Fixnum of int

exception SyntaxError of string;;

let rec read_sexp stm =
  let is_digit c =
    let code = Char.code c in
    code >= Char.code('0') && code <= Char.code('9')
  in
  let rec read_fixnum acc =
    let nc = read_char stm in
    if is_digit nc
    then read_fixnum (acc ^ (Char.escaped nc))
    else
      let _ = unread_char stm nc in
      Fixnum(int_of_string acc)
  in
  eat_whitespace stm;
  let c = read_char stm in
  if is_digit c
  then read_fixnum (Char.escaped c)
  else raise (SyntaxError ("Unexpected char " ^ (Char.escaped c)));;

let rec repl stm =
  print_string "> ";
  flush stdout;
  let Fixnum(v) = read_sexp stm in
  print_int v;
  print_newline ();
  repl stm;;

let main =
  let stm = { chr=[]; line_num=1; chan=stdin } in
  repl stm;;
