(*
#require "ctypes"
#require "ctypes.foreign"
   *)

open Ctypes
open Foreign

let libOpener str = Dl.dlopen ~flags:[Dl.RTLD_LAZY] ~filename:("/opt/warzone2100/src/ocaml/" ^ str ^ "")

let libsimple = libOpener "simple.so"
let libwar = libOpener "../warzone2100"

let adder_ = foreign ~from:libsimple
    "adder" (int @-> int @-> returning int)

let main = foreign ~from:libwar
    "main" (int @-> (ptr string) @-> returning int)

let test = foreign ~from:libwar
    "test" (void @-> returning void)

let () =
  print_endline (string_of_int (adder_ 1 2));
  test ();
  let str = allocate string "../warzone2100 --datadir /opt/warzone2100/data" in
  ignore(main 1 str);
