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

let countUpdate () =
  let setSatUplink i b = funer "setSatUplinkExists" (bool @-> int @-> returning void) b i in
  let setLasSat i b = funer "setLasSatExists" (bool @-> int @-> returning void) b i in
  let setNumDroids = funer "setNumDroids" (int @-> int @-> returning void) in
  let setNumMissionDroids = funer "setNumMissionDroids" (int @-> int @-> returning void) in
  let setNumConstructor = funer "setNumConstructorDroids" (int @-> int @-> returning void) in
  let setNumCommand = funer "setNumCommandDroids" (int @-> int @-> returning void) in
  let setNumTransporter = funer "setNumTransporterDroids" (int @-> int @-> returning void) in

  let count matches ({typ; _} : Droid.t) = if List.exists ( (=) typ ) matches then 1 else 0 in
  let sum list = List.fold_left (+) 0 list in

  Droid.iter (function ((1,id),droids) -> List.length droids |> setNumDroids id | _ -> ());
  Droid.iter (function ((2,id),droids) -> List.length droids |> setNumMissionDroids id | _ -> ());
  Droid.iter (function ((_,id),droids) -> List.map (count [Construct; CyborgConstruct]) droids |> sum |> setNumConstructor id | _ -> ());
  Droid.iter (function ((_,id),droids) -> List.map (count [Command]) droids |> sum |> setNumCommand id | _ -> ());
  (*FIXME Transporter [] = Transporter x? *)
  (*TODO This is not the amount of transporters but the units in the transporters.
           I do not know why this would be useful on its own in the two spots its used...*)
  Droid.iter (function ((_,id),droids) -> List.map (count [Transporter []; Supertransporter []]) droids |> sum |> setNumTransporter id | _ -> ());

  let uplinkExists blist =
    List.fold_left (fun acc ({typ; status; _} : Building.t) ->
      match typ,status with
      | SatUplink, Built -> true
      | _,_ -> acc) false blist
  in
  let lasSatExists blist = (*No check for build-progress because only one lasSat can exist at a time*)
    List.fold_left (fun acc ({pointer; _} : Building.t) ->
      funer "isLasSat" (ptr void @-> returning bool) pointer || acc) false blist
  in

  Building.iter (fun ((_,id),blist) -> uplinkExists blist |> setSatUplink id);
  Building.iter (fun ((_,id),blist) -> lasSatExists blist |> setLasSat id);

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
  let gameStateUpdate id = funer "gameStateUpdate" (int @-> returning void) id in
  let gameStatePostUpdate = funer "gameStatePostUpdate" vv in

  let updatePower id = (*FIXME TODO this does check all buildings, also those offworld*)
    let updateCurrentPower = funer "updateCurrentPower" (ptr void @-> int @-> int @-> returning void) in
    let buildings = Building.getList id 1 in
    List.iter (fun ({typ; status; pointer; _} : Building.t) -> match typ,status with
        | Generator,Built -> updateCurrentPower pointer id 1
        | _,_ -> ()) buildings
  in

  let rec innerLoop (renderBudget,lastUpdateRender) =
    recvMessage ();
    gameTimeUpdate (renderBudget > 0 || lastUpdateRender);
    (match getDeltaGameTime () with
    | 0 -> renderBudget
    | _ ->
      let before = wzGetTicks () in
      let () = gameStatePreUpdate () in
      countUpdate ();
      Player.iter updatePower;
      Player.iter gameStateUpdate;
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

