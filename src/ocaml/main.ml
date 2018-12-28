#require "ctypes"
#require "ctypes.foreign"
#require "tsdl"

open Ctypes
open Foreign
open Arg
open Tsdl
open Result


let todo _ = failwith "TODO"

let libOpener str = Dl.dlopen ~flags:[Dl.RTLD_LAZY] ~filename:("/opt/warzone2100/src/" ^ str)

let libwar = libOpener "/warzone2100"

let funer name params =
  foreign ~from:libwar
    ~check_errno:true
    ~release_runtime_lock:false
    name params

let vv = void @-> returning void
let sv = string @-> returning void

let specList =
    let cd = funer "setConfigdir" sv in
    let dd = funer "setDatadir" sv in
    let dbg = funer "setDebug" sv in
    let dbgf = funer "setDebugfile" sv in
    let dbgflush = funer "debugFlushStderr" vv in
    let fs () = (funer "setFullscreen" (bool @-> returning void)) true in
    let autogame = funer "autogame" vv in
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
    ("-autogame", Unit autogame, "Run games automatically for testing");
    ("-saveandquit", Unit todo, "Immediately save game and quit");
    ("-skirmish", Unit todo, "Start skirmish game with given settings file");
  ]

let init () =
  let debug_init = funer "debug_init" vv in
  let i18n_init = funer "initI18n" vv in
  let initPhysFS = funer "initPhysFS" vv in
  let wzMain = funer "wzMain" vv in
  let initMain = funer "init" vv in
  debug_init ();
  i18n_init ();
  initPhysFS();
  initMain();
  wzMain();
  ()

exception Halt
exception InvalidState

type gamemode =
  | Title
  | Game
  | SaveLoad

type loopState =
  | Running
  | Quitting
  | Loading
  | NewLevel (*TODO remove this probably*)
  | Viewing


let rec loop mode =
  let sdl = funer "SDLLoop" (void @-> returning bool) in
  let tmp = funer "inputNewFrame" vv in
  let frameUpdate = funer "frameUpdate" vv in
  let setGameMode mode =
    let iMode = match mode with Title -> 1 | Game -> 2 | SaveLoad -> 3 in
      funer "setGameMode" (int @-> returning void) iMode  in
  let gameLoop = funer "gameLoop" (void @-> returning int) in
  let titleLoop = funer "titleLoop" (void @-> returning int) in
  let getState = function
    | 1 -> Running
    | 2 -> Quitting
    | 3 -> Loading
    | 4 -> NewLevel
    | 5 -> Viewing
    | x -> raise InvalidState
  in
  let setState state = function
    | Running -> 1
    | Quitting -> 2
    | Loading -> 3
    | NewLevel -> 4
    | Viewing -> 5
  in
  let startGameLoop = funer "startGameLoop" vv in
  let stopGameLoop = funer "stopGameLoop" vv in
  let startTitleLoop = funer "startTitleLoop" vv in
  let stopTitleLoop = funer "stopTitleLoop" vv in
  let longOp f =
    let startOp = funer "initLoadingScreen" (bool @-> returning void) in
    let endOp = funer "closeLoadingScreen" vv in
    startOp true;
    List.iter (fun a -> a ()) f;
    endOp ();
  in

  let initSaveGameLoad = funer "initSaveGameLoad" vv in
  let realTimeUpdate = funer "realTimeUpdate" vv in

  match sdl () with true -> raise Halt | false -> ();
  frameUpdate ();
  let newMode = (match mode with
    | Title -> (match titleLoop () |> getState with
        | Running -> mode
        | Quitting -> stopTitleLoop(); raise Halt
        | Loading -> longOp [stopTitleLoop; initSaveGameLoad]; Game
        | NewLevel -> longOp [stopTitleLoop; startGameLoop]; Game
        | Viewing -> todo () )
    | Game -> (match gameLoop () |> getState with
        | Running | Viewing -> mode
        | Quitting -> longOp [stopGameLoop; startTitleLoop]; Title
        | Loading -> longOp [stopGameLoop; initSaveGameLoad]; Game
        | NewLevel -> longOp [stopGameLoop; startGameLoop]; Game)
    | x -> raise InvalidState ) in
  realTimeUpdate ();
  tmp ();
  let iMode mode = match mode with Title -> 1 | Game -> 2 | SaveLoad -> 3 in
  if mode <> newMode then Printf.printf "Mode changed: %i -> %i\n" (iMode mode) (iMode newMode);
  setGameMode newMode;
  loop newMode

let size = 8
let alignment = 8

let test : Sdl.window typ =
  abstract ~name:"test" ~size ~alignment

type sdlwindow = unit ptr
let window : sdlwindow typ = ptr void

let createWindow () : unit abstract ptr = match Sdl.init Sdl.Init.(video + timer) with
| Error (`Msg e) -> Sdl.log "Init error: %s" e; exit 1
| Ok () ->
  let set a b = match Sdl.gl_set_attribute a b with Error (`Msg e) -> Sdl.log "Error: %s" e | Ok () -> () in
  set Sdl.Gl.doublebuffer 1;
  set Sdl.Gl.stencil_size 8;
  set Sdl.Gl.multisamplebuffers 1;
  set Sdl.Gl.multisamplesamples 1;

  match Sdl.create_window ~w:640 ~h:480 "Warzone 2100" Sdl.Window.(opengl + shown + borderless + resizable + input_grabbed) with
    | Error (`Msg e) -> Sdl.log "Create window error: %s" e; exit 1
    | Ok w -> allocate_n test ~count:1

let _ =
  let setWindow = funer "setWindow" (window @-> returning void) in
  (*funer "_Z9getWindowv" (void @-> returning (ptr void))
  funer "getWindowSize" (void @-> returning int)
  *)
  createWindow()
(*
  parse specList (fun x -> Printf.fprintf stderr "Invalid argument") "Warzone2100:\nArguments";
  init ();
  try
    loop Title
  with Halt -> funer "halt" vv ()
   *)
