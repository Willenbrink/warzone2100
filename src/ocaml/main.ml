#require "ctypes"
#require "ctypes.foreign"

open Ctypes
open Foreign

let libOpener str = Dl.dlopen ~flags:[Dl.RTLD_LAZY] ~filename:("/opt/warzone2100/src/" ^ str)

let libwar = libOpener "/warzone2100"

let funer name params =
  foreign ~from:libwar
    ~release_runtime_lock:false
    name params

let vv = void @-> returning void

let test = funer "test" (void @-> returning void)

let str = "/opt/warzone2100/src/warzone2100"
type filepath = Path of string

exception PhysFSFailure

let init () =
  let debug_init = funer "debug_init" vv in
  let i18n_init = funer "initI18n" vv in
  let physFS_init = funer "PHYSFS_init" (string @-> returning int) in
  debug_init ();
  i18n_init ();
  match physFS_init (str) with
  (*"Error was %s", WZ_PHYSFS_getLastError());*)
  | 0 -> raise PhysFSFailure
  | x -> print_int x

let () =
  let main = funer "main" (int @-> ptr void @-> returning int) in
  let str = "/opt/warzone2100/src/warzone2100" in
  let str2 = "--datadir=/opt/warzone2100/data" in
  try
    init();
    CArray.of_list string [str;str2]
    |> CArray.start
    |> to_voidp
    |> main 2
    |> print_int
  with e -> print_endline "Exception raised!"; raise e
