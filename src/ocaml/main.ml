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

let main = funer "main" (int @-> ptr string @-> returning int)

let test = funer "test" (void @-> returning void)

let _ =
  let str = allocate string "../warzone2100 --datadir=/opt/warzone2100/data" in
  main 2
