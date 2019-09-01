open Interface

let updateCounts () =
  let setSatUplink i b = funer "setSatUplinkExists" (bool @-> int @-> returning void) b i in
  let setLasSat i b = funer "setLasSatExists" (bool @-> int @-> returning void) b i in
  let setNumDroids = funer "setNumDroids" (int @-> int @-> returning void) in
  let setNumMissionDroids = funer "setNumMissionDroids" (int @-> int @-> returning void) in
  let setNumConstructor = funer "setNumConstructorDroids" (int @-> int @-> returning void) in
  let setNumCommand = funer "setNumCommandDroids" (int @-> int @-> returning void) in

  let count pattern droids =
    (* Check whether the passed object matches at least one element of the pattern list*)
    let matchesPattern ({typ; _} : Droid.t) = if List.exists ( (=) typ ) pattern then 1 else 0 in
    let sum list = List.fold_left (+) 0 list in
    List.map matchesPattern droids
    |> sum
  in

  (* Count droids in the lists main and mission *)
  Droid.iterAssoc (function
      | ((Main,id),droids) -> List.length droids |> setNumDroids id
      | ((Mission,id),droids) -> List.length droids |> setNumMissionDroids id
      | _ -> ());

  (* Count specific droids in all lists for each player *)
  Player.iter (fun id ->
      count [Construct; CyborgConstruct] @@ Droid.getList [Main; Mission; Limbo] id
      |> setNumConstructor id);
  Player.iter (fun id ->
      count [Command] @@ Droid.getList [Main; Mission; Limbo] id
      |> setNumCommand id);

  let uplinkExists buildings =
    let isUplink = function
      | ({typ = SatUplink; status = Built; _} : Building.t) -> true
      | _ -> false
    in
    List.exists isUplink buildings
  in
  let lasSatExists buildings = (* No check for build-progress because only one lasSat can exist at a time *)
    let isLasSat ({pointer; _} : Building.t) = funer "isLasSat" (ptr void @-> returning bool) pointer in
    List.exists isLasSat buildings
  in

  (* Check and set for every player both uplink and lassat *)
  Building.iterAssoc (fun ((_,id),buildings) -> uplinkExists buildings |> setSatUplink id);
  Building.iterAssoc (fun ((_,id),buildings) -> lasSatExists buildings |> setLasSat id);
  ()

(* Renderbudget represents the balance between gamelogic and rendering. Every gameworldupdate reduces the budget until empty and every render increases it.*)
let gameLoop (lastFlush) : int * int =
  let recvMessage = funer "recvMessage" vv in
  let gameTimeUpdate = funer "gameTimeUpdate" (bool @-> returning void) in
  let renderLoop = funer "renderLoop" (void @-> returning int) in
  let netflush = funer "NETflush" vv in
  let getDeltaGameTime = funer "getDeltaGameTime" (void @-> returning int) in
  let getRealTime = funer "getRealTime" (void @-> returning int) in
  let setDeltaGameTime = funer "setDeltaGameTime" (int @-> returning void) in
  let gameStatePreUpdate = funer "gameStatePreUpdate" vv in
  let gameStateUpdate () =
    Droid.iterAssoc (function
        | ((Main,_),droids) -> List.iter (fun droid -> Droid.update droid) droids
        | _ -> ());
    Droid.iterAssoc (function
        | ((Mission,_),droids) -> List.iter (fun droid -> Droid.update_mission droid) droids
        | _ -> ());
    Building.iterAssoc (function
        | ((l,_),buildings) -> List.iter (fun building -> Building.update building (l = 2)) buildings);
  in
  let gameStatePostUpdate = funer "gameStatePostUpdate" vv in

  let updatePower id = (*FIXME this does check all buildings, also those offworld :-: Why? It only uses list 1 *)
    let updateCurrentPower = funer "updateCurrentPower" (ptr void @-> int @-> int @-> returning void) in
    let buildings = Building.getList id 1 in
    List.iter (fun ({typ; status; pointer; _} : Building.t) -> match typ,status with
        | Generator,Built -> updateCurrentPower pointer id 1
        | _,_ -> ()) buildings
  in

  let worldUpdate () =
    recvMessage ();
    gameTimeUpdate true;
    gameStatePreUpdate ();
    updateCounts ();
    Player.iter updatePower;
    gameStateUpdate ();
    gameStatePostUpdate ();
    gameTimeUpdate false;
    recvMessage ()
  in

  worldUpdate ();
  let newLastFlush = (* Flush at least every 400 ms *)
    if Time.currentTime () - lastFlush >= 400
    then begin
      netflush ();
      getRealTime ()
    end
    else lastFlush
  in
  let renderReturn = renderLoop () in
  (renderReturn,newLastFlush)

type gamemode =
  | Title
  | Game
  | SaveLoad

let rec loop sdlState =
  let tmp = funer "inputNewFrame" vv in
  let countFps = funer "countFps" vv in
  let setRunning newMode =
    let worker = funer "setRunning" (bool @-> returning void) in
    match newMode with
    | Title -> worker true
    | Game -> worker false
    | _ -> todo ()
  in
  let init_frontend () =
    match funer "frontendInitialise" (string @-> returning bool) "wrf/frontend.wrf" with
    | false -> raise Halt
    | true -> ()
  in
  let stopTitleLoop = funer "stopTitleLoop" vv in
  let startGameLoop = funer "startGameLoop" vv in
  let stopGameLoop = funer "stopGameLoop" vv in
  let initSaveGameLoad = funer "initSaveGameLoad" vv in
  let initLoadingScreen = funer "initLoadingScreen" (bool @-> returning void) in
  let closeLoadingScreen = funer "closeLoadingScreen" vv in
  let realTimeUpdate = funer "realTimeUpdate" vv in
  let wrapLoad f = fun x ->
    initLoadingScreen true;
    let ret = f x in
    closeLoadingScreen ();
    ret
  in

  let mode = ref Title in
  let gameLoopState = ref (0) in
  (* This is the main-loop. This should never be exited unless we exit the game *)
  while true do
    Bsdl.handleEvents sdlState;

    let (newMode : gamemode),newState = match !mode with
      | Title -> Menu.(match handleMenu () with
          | Running -> Title,!gameLoopState
          | Quitting -> stopTitleLoop (); raise Halt
          | Loading ->
            wrapLoad (fun () -> stopTitleLoop (); initSaveGameLoad ()) ();
            Game,!gameLoopState
          | NewLevel ->
            wrapLoad (fun () -> stopTitleLoop (); startGameLoop ()) ();
            Game,!gameLoopState
          | Viewing -> todo ())
      | Game -> (
          let (state,lastFlush) =
            gameLoop !gameLoopState
          in
          Menu.(match getState state with
          | Running | Viewing -> Game,lastFlush
          | Quitting ->
            stopGameLoop ();
            wrapLoad init_frontend ();
            Title,lastFlush
          | Loading ->
            stopGameLoop ();
            initSaveGameLoad ();
            Game,lastFlush
          | NewLevel ->
            stopGameLoop ();
            startGameLoop ();
            Game,lastFlush)
        )
      | _ -> raise Not_found
    in
    mode := newMode;
    gameLoopState := newState;
    realTimeUpdate ();
    tmp ();
    setRunning !mode;
    countFps ();
  done
