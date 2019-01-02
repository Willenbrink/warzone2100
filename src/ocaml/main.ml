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
exception Invalid_state of string

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

let getState = function
  | 1 -> Running
  | 2 -> Quitting
  | 3 -> Loading
  | 4 -> NewLevel
  | 5 -> Viewing
  | x -> raise (Invalid_state "getState")

let setState state = function
  | Running -> 1
  | Quitting -> 2
  | Loading -> 3
  | NewLevel -> 4
  | Viewing -> 5

let rec countUpdate player =
  let coundUpdateSingleNew =
    let setSatUplink = funer "setSatUplinkExists" (bool @-> int @-> returning void) in
    let getSatUplink = funer "getSatUplinkExists" (void @-> returning bool) in
    let setLasSat= funer "setLasSatExists" (bool @-> int @-> returning void) in
    let getLasSat= funer "getLasSatExists" (void @-> returning bool) in
    ()
  in
    let countUpdateSingle = funer "countUpdateSingle" (bool @-> int @-> returning void) in
    if player >= 0
    then
      (countUpdateSingle true player;
       countUpdate (player-1))
    else ()

let gameLoop (lastFlush,renderBudget,lastUpdateRender) =
  let getMaxPlayers = funer "getMaxPlayers" (void @-> returning int) in
  let recvMessage = funer "recvMessage" vv in
  let gameTimeUpdate = funer "gameTimeUpdate" (bool @-> returning void) in
  let renderLoop = funer "renderLoop" (void @-> returning int) in
  let wzGetTicks = funer "wzGetTicks" (void @-> returning int) in
  let netflush = funer "NETflush" vv in
  let getDeltaGameTime = funer "getDeltaGameTime" (void @-> returning int) in
  let getRealTime = funer "getRealTime" (void @-> returning int) in
  let setDeltaGameTime = funer "setDeltaGameTime" (int @-> returning void) in
  let gameStateUpdate = funer "gameStateUpdate" vv in

  let rec innerLoop (renderBudget,lastUpdateRender) =
    recvMessage ();
    gameTimeUpdate (renderBudget > 0 || lastUpdateRender);
    (match getDeltaGameTime () with
    | 0 -> renderBudget
    | _ ->
      let before = wzGetTicks () in
      let () = countUpdate (getMaxPlayers ()) in
      let () = gameStateUpdate () in
      let after = wzGetTicks () in
      innerLoop ((renderBudget - (after - before) * 2),false)
    ) in

  let renderBudget = innerLoop (renderBudget, lastUpdateRender) in
  let newLastFlush = if getRealTime () - lastFlush >= 400 then (netflush(); getRealTime ()) else lastFlush in
  let before = wzGetTicks () in
  let renderReturn = renderLoop () in
  let after = wzGetTicks () in
  (renderReturn,newLastFlush, (renderBudget + (after - before) * 3),true)

let rec loop mode gameLoopState =
  let sdl = funer "SDLLoop" (void @-> returning bool) in
  let tmp = funer "inputNewFrame" vv in
  let frameUpdate = funer "frameUpdate" vv in
  let getGameMode () =
    match funer "getGameMode" (void @-> returning int) () with
    | 1 -> Title
    | 2 -> Game
    | _ -> raise (Invalid_state "getGameMode")
  in
  let setGameMode newMode =
    let worker = funer "setGameMode" (int @-> returning void) in
    match newMode with
    | Title -> worker 1
    | Game -> worker 2
    | _ -> todo ()
  in
  let startTitleLoop = funer "startTitleLoop" vv in
  let titleLoop = funer "titleLoop" (void @-> returning int) in
  let stopTitleLoop = funer "stopTitleLoop" vv in
  let startGameLoop = funer "startGameLoop" vv in
  let stopGameLoop = funer "stopGameLoop" vv in
  let initSaveGameLoad = funer "initSaveGameLoad" vv in
  let initLoadingScreen = funer "initLoadingScreen" (bool @-> returning void) in
  let closeLoadingScreen = funer "closeLoadingScreen" vv in
  let realTimeUpdate = funer "realTimeUpdate" vv in

  (match sdl () with true -> raise Halt | false -> ());
  frameUpdate ();
  let newMode,newState = match getGameMode () with
   | Title -> (match titleLoop () |> getState with
       | Running -> Title,gameLoopState
       | Quitting -> stopTitleLoop (); raise Halt
       | Loading -> initLoadingScreen true; stopTitleLoop (); initSaveGameLoad (); closeLoadingScreen (); Game,gameLoopState
       | NewLevel -> initLoadingScreen true; stopTitleLoop (); startGameLoop (); closeLoadingScreen (); Game,gameLoopState
       | Viewing -> todo ())
  | Game -> (
      let (state,lastFlush,renderBudget,lastUpdateRender) =
        gameLoop gameLoopState in
      match state |> getState with
       | Running | Viewing -> Game,(lastFlush,renderBudget,lastUpdateRender)
       | Quitting -> stopGameLoop (); startTitleLoop (); Title,(lastFlush,renderBudget,lastUpdateRender)
       | Loading -> stopGameLoop (); initSaveGameLoad (); Game,(lastFlush,renderBudget,lastUpdateRender)
       | NewLevel -> stopGameLoop (); startGameLoop (); Game,(lastFlush,renderBudget,lastUpdateRender)
     )
   | x -> raise (Invalid_state "mainLoop")
  in
  realTimeUpdate ();
  tmp ();
  setGameMode newMode;
  loop newMode newState

let () =
  let state = (0,0,false) in
  parse specList (fun x -> Printf.fprintf stderr "Invalid argument") "Warzone2100:\nArguments";
  init ();
  try
    loop Title state
  with Halt -> funer "halt" vv ()
