#require "ctypes"
#require "ctypes.foreign"
#mod_use "interface.ml"

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

type player = {id : int}

module Droid = struct
  type typ =
    | Weapon
    | Sensor
    | ECM
    | Construct
    | Person
    | Cyborg
    | Transporter of t list
    | Supertransporter of t list
    | Command
    | Repair
    | Default
    | CyborgConstruct
    | CyborgRepair
    | CyborgSuper
    | Any

  and t = {id : int; typ : typ; pointer : (unit Ctypes_static.ptr)}

  let rec getGroup t =
    match funer "getDroidGroup" (ptr void @-> returning (ptr_opt void)) t with
    | Some x -> getDroid x :: getGroup (from_voidp void null)
    | None -> []
  and getType t =
    let f = funer "getDroidType" (ptr void @-> returning int) in
    match f t with
    | 0 -> Weapon
    | 1 -> Sensor
    | 2 -> ECM
    | 3 -> Construct
    | 4 -> Person
    | 5 -> Cyborg
    | 6 -> Transporter (getGroup t)
    | 7 -> Command
    | 8 -> Repair
    | 9 -> Default
    | 10 -> CyborgConstruct
    | 11 -> CyborgRepair
    | 12 -> CyborgSuper
    | 13 -> Supertransporter (getGroup t)
    | 14 -> Any
    | _ -> raise (Invalid_state "getType")
  and getDroid t =
    let id = funer "getDroidId" (ptr void @-> returning int) t in
    let typ = getType t in
    {id; typ; pointer = t}

  let getList ({id} : player) list =
    let getList = funer "getDroidList" (int @-> int @-> returning (ptr_opt void)) in
    let rec f l =
      match getList id l with
      | Some x -> getDroid x :: f 0
      | None -> []
    in
    f list
end

module Building = struct
  type typ =
    | HQ
    | Factory
    | FactoryModule
    | Generator
    | GeneratorModule
    | Pump
    | Defense
    | Wall
    | WallCorner
    | Generic
    | Research
    | ResearchModule
    | RepairFacility
    | CommandControl
    | Bridge
    | Demolish (*TODO why? what? "//the demolish structure type - should only be one stat with this type"*)
    | CyborgFactory
    | VTOLFactory
    | Lab (*TODO Difference to research? Some campaign object?*)
    | RearmPad
    | MissileSilo
    | SatUplink
    | Gate

  and t = {id : int; typ : typ; pointer : (unit Ctypes_static.ptr)}

  let rec getType t =
    let f = funer "getBuildingType" (ptr void @-> returning int) in
    match f t with
    | 0 -> HQ
    | 1 -> Factory
    | 2 -> FactoryModule
    | 3 -> Generator
    | 4 -> GeneratorModule
    | 5 -> Pump
    | 6 -> Defense
    | 7 -> Wall
    | 8 -> WallCorner
    | 9 -> Generic
    | 10 -> Research
    | 11 -> ResearchModule
    | 12 -> RepairFacility
    | 13 -> CommandControl
    | 14 -> Bridge
    | 15 -> Demolish (*TODO why? what? "//the demolish structure type - should only be one stat with this type"*)
    | 16 -> CyborgFactory
    | 17 -> VTOLFactory
    | 18 -> Lab (*TODO Difference to research? Some campaign object?*)
    | 19 -> RearmPad
    | 20 -> MissileSilo
    | 21 -> SatUplink
    | 22 -> Gate
    | _ -> raise (Invalid_state "getType: Building")
  and getBuilding t =
    let id = funer "getBuildingId" (ptr void @-> returning int) t in
    let typ = getType t in
    {id; typ; pointer = t}

  let getList ({id} : player) list =
    let getList = funer "getBuildingList" (int @-> int @-> returning (ptr_opt void)) in
    let rec f l =
      match getList id l with
      | Some x -> getBuilding x :: f 0
      | None -> []
    in
    f list

end

let rec countUpdate {id} =
  let countUpdateSingleNew () =
    let setSatUplink = funer "setSatUplinkExists" (bool @-> int @-> returning void) in
    let getSatUplink = funer "getSatUplinkExists" (void @-> returning bool) in
    let setLasSat= funer "setLasSatExists" (bool @-> int @-> returning void) in
    let getLasSat= funer "getLasSatExists" (void @-> returning bool) in
    let setNumDroids = funer "setNumDroids" (int @-> int @-> returning void) id in
    let setNumMissionDroids = funer "setNumMissionDroids" (int @-> int @-> returning void) id in
    let setNumConstructor = funer "setNumConstructorDroids" (int @-> int @-> returning void) id in
    let setNumCommand = funer "setNumCommandDroids" (int @-> int @-> returning void) id in
    let setNumTransporter = funer "setNumTransporterDroids" (int @-> int @-> returning void) id in

    let droidsMain = Droid.getList {id} 1 in
    let droidsMission = Droid.getList {id} 2 in
    let droidsLimbo = Droid.getList {id} 3 in

    let rec count f droids =
      let mapAll (droid : Droid.t) = match droid with
        | {typ = Transporter x} | {typ = Supertransporter x} -> f droid + count f x
        | _ -> f droid
      in
      List.map mapAll droids |>
      List.fold_left (+) 0
    in

    let f_total list = count (fun x -> 1) list in
    let f_builder list = count (function {typ=Construct} | {typ=CyborgConstruct} -> 1 | _ -> 0) list in
    let f_command list = count (function {typ = Command} -> 1 | _ -> 0) list in
    let f_transporter list =
      (*TODO This is not the amount of transporters but the units in the transporters.
             I do not know why this would be useful in the two spots its used...*)
      count (function {typ = Transporter x} | {typ = Supertransporter x} -> List.length x | _ -> 0) list
    in

    let total = f_total droidsMain in
    let missionTotal = f_total droidsMission in
    let builder = f_builder (droidsMain @ droidsMission @ droidsLimbo) in
    let command = f_command (droidsMain @ droidsMission @ droidsLimbo) in
    let transporter = f_transporter (droidsMain @ droidsMission) in

    setNumDroids total;
    setNumMissionDroids missionTotal;
    setNumConstructor builder;
    setNumCommand command;
    setNumTransporter transporter;

    ()
  in
  let countUpdateSingle = funer "countUpdateSingle" (bool @-> int @-> returning void) in
  countUpdateSingleNew ();
  countUpdateSingle false id

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
      let players = List.init (getMaxPlayers ()) (fun id -> {id}) in
      let () = List.iter countUpdate players in
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
      match getState state with
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
