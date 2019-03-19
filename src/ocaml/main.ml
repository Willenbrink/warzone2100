open Interface

let todo _ = failwith "TODO"

let init () =
  let debug_init = funer "debug_init" vv in
  let i18n_init = funer "initI18n" vv in
  let initPhysFS = funer "initPhysFS" vv in
  let wzMain = funer "wzMain" vv in
  let initMain1 = funer "init" vv in
  let initMain2 = funer "init2" vv in
  debug_init ();
  i18n_init ();
  initPhysFS();
  initMain1();
  let state = Bsdl.init () in
  initMain2();
  wzMain();
  state

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

  let count matches droids =
    let count ({typ; _} : Droid.t) = if List.exists ( (=) typ ) matches then 1 else 0 in
    let sum list = List.fold_left (+) 0 list in
    List.map count droids
    |> sum
  in

  Droid.iterAssoc (function ((1,id),droids) -> List.length droids |> setNumDroids id | _ -> ());
  Droid.iterAssoc (function ((2,id),droids) -> List.length droids |> setNumMissionDroids id | _ -> ());
  Player.iter (fun id ->
      count [Construct; CyborgConstruct] (Droid.getList 1 id @ Droid.getList 2 id @ Droid.getList 3 id)
      |> setNumConstructor id);
  Player.iter (fun id ->
      count [Command] (Droid.getList 1 id @ Droid.getList 2 id @ Droid.getList 3 id)
      |> setNumCommand id);

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

  Building.iterAssoc (fun ((_,id),blist) -> uplinkExists blist |> setSatUplink id);
  Building.iterAssoc (fun ((_,id),blist) -> lasSatExists blist |> setLasSat id);
  ()

let gameLoop (lastFlush,renderBudget,lastUpdateRender) =
  let recvMessage = funer "recvMessage" vv in
  let gameTimeUpdate = funer "gameTimeUpdate" (bool @-> returning void) in
  let renderLoop = funer "renderLoop" (void @-> returning int) in
  let wzGetTicks = funer "wzGetTicks" (void @-> returning int) in
  let netflush = funer "NETflush" vv in
  let getDeltaGameTime = funer "getDeltaGameTime" (void @-> returning int) in
  let getRealTime = funer "getRealTime" (void @-> returning int) in
  let setDeltaGameTime = funer "setDeltaGameTime" (int @-> returning void) in
  let gameStatePreUpdate = funer "gameStatePreUpdate" vv in
  let gameStateUpdate () =
      Droid.iterAssoc (function ((1,_),droids) -> List.iter (fun ({pointer; _} : Droid.t ) -> funer "droidUpdate" (ptr void @-> returning void) pointer) droids | _ -> ());
      Droid.iterAssoc (function ((2,_),droids) -> List.iter (fun ({pointer; _} : Droid.t ) -> funer "missionDroidUpdate" (ptr void @-> returning void) pointer) droids | _ -> ());
      Building.iterAssoc (function ((l,_),buildings) -> List.iter (fun ({pointer; _} : Building.t ) -> funer "structureUpdate" (ptr void @-> bool @-> returning void) pointer (l = 2)) buildings);
  in
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
      gameStateUpdate ();
      let () = gameStatePostUpdate () in
      let after = wzGetTicks () in
      innerLoop ((renderBudget - (after - before) * 2),false))
  in

  let renderBudget = innerLoop (renderBudget, lastUpdateRender) in
  let newLastFlush = if getRealTime () - lastFlush >= 400 then (netflush(); getRealTime ()) else lastFlush in
  let before = wzGetTicks () in
  let renderReturn = renderLoop () in
  let after = wzGetTicks () in
  (renderReturn,newLastFlush, (renderBudget + (after - before) * 3),true)

let rec loop sdlState mode gameLoopState =
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

  Bsdl.loop sdlState;
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
  loop sdlState newMode newState

let () =
  let state = (0,0,false) in
  Arg.parse Parser.specList (fun _ -> Printf.fprintf stderr "Invalid argument") "Warzone2100:\nArguments";
  let sdlState = init () in
  try
    loop sdlState Title state
  with e -> funer "halt" vv (); raise e

