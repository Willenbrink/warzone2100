#require "ctypes"
#require "ctypes.foreign"

open Ctypes
open Foreign
open Arg

let todo _ = failwith "TODO"

let libOpener str = Dl.dlopen ~flags:[Dl.RTLD_LAZY] ~filename:("/opt/warzone2100/src/" ^ str)

let libwar = libOpener "/warzone2100"

let funer name params =
  foreign ~from:libwar
    ~release_runtime_lock:false
    name params

let vv = void @-> returning void
let sv = string @-> returning void

let test = funer "test" (void @-> returning void)

exception PhysFSFailure


let specList =
    let cd = funer "setConfigdir" sv in
    let dd = funer "setDatadir" sv in
    let dbg = funer "setDebug" sv in
    let dbgf = funer "setDebugfile" sv in
    let dbgflush = funer "debugFlushStderr" vv in
    let fs () = (funer "setFullscreen" (bool @-> returning void)) true in

  [
    ("-configdir", String cd, "Set configuration directory");
    ("-datadir", String dd, "Add data directory");
    ("-debug", String dbg, "Show debug for given level");
    ("-debugfile", String dbgf, "Log debug output to given file");
    ("-flushdebug", Unit dbgflush, "Flush all debug output written to stderr");
    ("-fullscreen", Unit fs, "Play in fullscreen mode");
    ("-gamemode", Unit todo, "Load a specific gamemode");
    ("-mod", Unit todo, "Enable a global mod (Most likely incompatible)");
    ("-camod", Unit todo, "Enable a campaign-only mod");
    ("-mpmod", Unit todo, "Enable a multiplayer-only mod");
    ("-noassert", Unit todo, "Disable asserts");
    ("-loadskirmish", Unit todo, "Load a saved skirmish game");
    ("-loadcampaign", Unit todo, "Load a saved campaign game");
    ("-window", Unit todo, "Play in windowed mode");
    ("-version", Unit todo, "Show version information and exit");
    ("-resolution", Unit todo, "Set size of window");
    ("-nosound", Unit todo, "Disable sound");
    ("-join", Unit todo, "Connect directly to IP/hostname");
    ("-host", Unit todo, "Go directly to host screen");
    ("-autogame", Unit todo, "Run games automatically for testing");
    ("-saveandquit", Unit todo, "Immediately save game and quit");
    ("-skirmish", Unit todo, "Start skirmish game with given settings file");
  ]

let parseOptions = todo

let init () =
  let debug_init = funer "debug_init" vv in
  let i18n_init = funer "initI18n" vv in
  let str = "/opt/warzone2100/src/warzone2100" in
  let initPhysFS = funer "initPhysFS" vv in
  debug_init ();
  i18n_init ();
  initPhysFS()

let () =
  let main = funer "main" (int @-> ptr void @-> returning int) in
  parse specList (fun x -> Printf.fprintf stderr "Invalid argument") "Warzone2100:\nArguments";
  let params = [
    "/opt/warzone2100/src/warzone2100";
  ] in
  try
    init();
    CArray.of_list string params
    |> CArray.start
    |> to_voidp
    |> main (List.length params)
    |> print_int
  with e -> print_endline "Exception raised!"; raise e
