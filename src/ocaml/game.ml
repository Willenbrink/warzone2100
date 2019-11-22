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
  Droid.iter_assoc (function
      | ((Main,id),droids) -> List.length droids |> setNumDroids id
      | ((Mission,id),droids) -> List.length droids |> setNumMissionDroids id
      | _ -> ());

  (* Count specific droids in all lists for each player *)
  Player.iter (fun id ->
      count [Construct; CyborgConstruct] @@ Droid.get_list [Main; Mission; Limbo] id
      |> setNumConstructor id);
  Player.iter (fun id ->
      count [Command] @@ Droid.get_list [Main; Mission; Limbo] id
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
  let setDeltaGameTime = funer "setDeltaGameTime" (int @-> returning void) in
  let gameStatePreUpdate = funer "gameStatePreUpdate" vv in
  let gameStateUpdate () =
    Droid.iter_assoc (function
        | ((Main,_),droids) -> List.iter (fun droid -> Droid.update droid) droids
        | _ -> ());
    Droid.iter_assoc (function
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
    if Time.current_time () - lastFlush >= 400
    then begin
      netflush ();
      Time.current_time ()
    end
    else lastFlush
  in
  let renderReturn = renderLoop () in
  (renderReturn,newLastFlush)

