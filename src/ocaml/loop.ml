open Interface

type gamemode =
  | Menu
  | Game

type loop_state =
  | Running
  | Quitting
  | Loading
  | NewLevel (*TODO remove this probably*)
  | Viewing

let get_state = function
  | 1 -> Running
  | 2 -> Quitting
  | 3 -> Loading
  | 4 -> NewLevel
  | 5 -> Viewing
  | _ -> raise Not_found

let set_state = function
  | Running -> 1
  | Quitting -> 2
  | Loading -> 3
  | NewLevel -> 4
  | Viewing -> 5

let initLoadingScreen = funer "initLoadingScreen" (bool @-> returning void)
let closeLoadingScreen = funer "closeLoadingScreen" vv
let wrapLoad f = fun x ->
  initLoadingScreen true;
  let ret = f x in
  closeLoadingScreen ();
  ret
let init_frontend () =
  let worker () = funer "frontendInitialise" (string @-> returning bool) "wrf/frontend.wrf" in
  critical worker ()

let start_title_loop () =
  wrapLoad (init_frontend) ()
let stopTitleLoop () =
  let worker () = funer "frontendShutdown" vb () in
  critical worker ()
let startGameLoop = funer "startGameLoop" vv
let stopGameLoop = funer "stopGameLoop" vv

let initSaveGameLoad () =
  funer "restartBackDrop" vv ();
  let save_name = funer "getSaveGameName" (void @-> returning string) () in
  match funer "loadGameInit" (string @-> returning bool) save_name with
  | false -> stopGameLoop (); start_title_loop (); Menu
  | true ->
    funer "stopBackDrop" vv ();
    if funer "getTrapCursor" (void @-> returning bool) ()
    then funer "wzGrabMouse" vv ();
    if funer "getChallengeActive" (void @-> returning bool) ()
    then funer "addMissionTimerInterface" vv ();
    Game

let handleMenuChanges game_state = (* Check if the player starts or loads a game *)
  let new_mode = match Menu.handleMenu () with
  | Startgame ->
    wrapLoad (fun () -> stopTitleLoop (); startGameLoop ()) ();
    Game
  | LoadSaveGame ->
    wrapLoad (fun () -> stopTitleLoop (); initSaveGameLoad ()) ()
  | Intro -> todo ()
  | Quit -> stopTitleLoop (); raise Exit
  | _ -> Menu
  in
  game_state,new_mode

let handleGameChanges game_state =
  let (state,last_flush) = Game.gameLoop game_state in
  let new_mode = match state |> get_state with
  | Running | Viewing -> Game
  | Quitting ->
    stopGameLoop ();
    wrapLoad init_frontend ();
    Menu
  | Loading ->
    stopGameLoop ();
    initSaveGameLoad ()
  | NewLevel ->
    stopGameLoop ();
    startGameLoop ();
    Game
  in
  last_flush,new_mode

let execution_loop sdl_state =
  let setRunning newMode =
    let worker = funer "setRunning" (bool @-> returning void) in
    match newMode with
    | Menu -> worker false
    | Game -> worker true
  in

  (* This is the main-loop. This should never be exited unless we exit the game *)
  let rec loop mode state =
    Bsdl.handleEvents sdl_state; (* Events like user-input, changes to windows size and exit *)

    (* Handle mainmenu or gameworld depending on which we are currently in *)
    let state,mode = match mode with
      | Menu -> handleMenuChanges state
      | Game -> handleGameChanges state
    in
    Time.update ();
    setRunning mode; (* Synchronise current mode with C part of the game TODO remove once superfluous *)
    loop mode state
  in
  loop Menu 0
