include Ctypes
open Foreign

let libOpener str = Dl.dlopen ~flags:[Dl.RTLD_LAZY] ~filename:("/opt/warzone2100/src/" ^ str)

let libwar = libOpener "warzone2100"

let funer name params =
  try
    foreign ~from:libwar
      (*    ~check_errno:true *)
      ~release_runtime_lock:false
      name params
  with
  e -> print_endline ("Failed to find " ^ name); raise e

let vv = void @-> returning void
let sv = string @-> returning void

let todo _ = failwith "TODO"
exception Halt
