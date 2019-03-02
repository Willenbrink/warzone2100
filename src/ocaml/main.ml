open Interface

let todo _ = failwith "TODO"

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
  | _ -> raise Not_found

let setState = function
  | Running -> 1
  | Quitting -> 2
  | Loading -> 3
  | NewLevel -> 4
  | Viewing -> 5

type player = {id : int}

let rec countUpdate {id} =
  let setSatUplink = funer "setSatUplinkExists" (bool @-> int @-> returning void) in
  let setLasSat= funer "setLasSatExists" (bool @-> int @-> returning void) in
  let setNumDroids = funer "setNumDroids" (int @-> int @-> returning void) id in
  let setNumMissionDroids = funer "setNumMissionDroids" (int @-> int @-> returning void) id in
  let setNumConstructor = funer "setNumConstructorDroids" (int @-> int @-> returning void) id in
  let setNumCommand = funer "setNumCommandDroids" (int @-> int @-> returning void) id in
  let setNumTransporter = funer "setNumTransporterDroids" (int @-> int @-> returning void) id in

  let droidsMain = Droid.getList id 1 in
  let droidsMission = Droid.getList id 2 in
  let droidsLimbo = Droid.getList id 3 in

  let rec count f droids =
    let mapAll (droid : Droid.t) = match droid with
      | {typ = Transporter x; _} | {typ = Supertransporter x; _} -> f droid + count f x
      | _ -> f droid
    in
    List.map mapAll droids |>
    List.fold_left (+) 0
  in

  let f_total list = count (fun _ -> 1) list in
  let f_builder list = count (function {typ=Construct; _} | {typ=CyborgConstruct; _} -> 1 | _ -> 0) list in
  let f_command list = count (function {typ = Command; _} -> 1 | _ -> 0) list in
  let f_transporter list =
    (*TODO This is not the amount of transporters but the units in the transporters.
           I do not know why this would be useful in the two spots its used...*)
    count (function {typ = Transporter x; _} | {typ = Supertransporter x; _} -> List.length x | _ -> 0) list
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

  let buildings = Building.getList id 1 in
  let buildingsMission = Building.getList id 2 in
  let satUplink = List.fold_left (fun acc ({typ; status; _} : Building.t) ->
      match typ,status with
      | SatUplink, Built -> true
      | _,_ -> acc) false (buildings @ buildingsMission)
  in
  setSatUplink satUplink id;
  let lasSat = List.map (fun ({pointer; _} : Building.t) -> funer "isLasSat" (ptr void @-> returning bool) pointer) (buildings @ buildingsMission) |> List.fold_left (||) false in
  setLasSat lasSat id;
  ()

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
  let gameStatePreUpdate = funer "gameStatePreUpdate" vv in
  let gameStateUpdate = funer "gameStateUpdate" (int @-> returning void) in
  let gameStatePostUpdate = funer "gameStatePostUpdate" vv in

  let rec innerLoop (renderBudget,lastUpdateRender) =
    recvMessage ();
    gameTimeUpdate (renderBudget > 0 || lastUpdateRender);
    (match getDeltaGameTime () with
    | 0 -> renderBudget
    | _ ->
      let before = wzGetTicks () in
      let players = List.init (getMaxPlayers ()) (fun id -> {id}) in
      let () = List.iter countUpdate players in
      let () = gameStatePreUpdate () in
      let () = List.map (fun {id} -> id) players |> List.iter gameStateUpdate in
      let () = gameStatePostUpdate () in
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
    | _ -> raise Not_found
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
   | _ -> raise Not_found
  in
  realTimeUpdate ();
  tmp ();
  setGameMode newMode;
  loop newMode newState

let () =
  let state = (0,0,false) in
  Arg.parse Parser.specList (fun _ -> Printf.fprintf stderr "Invalid argument") "Warzone2100:\nArguments";
  init ();
  try
    loop Title state
  with Halt -> funer "halt" vv ()

