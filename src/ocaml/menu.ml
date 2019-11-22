(* This file contains everything related to the main menu of the game. E.g. Singleplayer, Multiplayer, Options etc. *)
open Interface

type currMenu =
  | Title
  | Singleplayer
  | Multiplayer
  | Options
  | Game (* TODO what is this? *)
  | Tutorial
  | Credits
  | Protocol
  | Multiplayer_Option
  | Multiplayer_ForceSelect
  | Multiplayer_GameFinder
  | Multiplayer_Limit
  | Startgame
  | Intro
  | Quit
  | LoadSaveGame
  | Options_Keymap
  | Options_Graphics
  | Options_Audio_Zoom
  | Options_Video
  | Options_Mouse
  | Campaigns

let getTitleMode () =
  match funer "getTitleMode" (void @-> returning int) () with
  | 0 -> Title
  | 1 -> Singleplayer
  | 2 -> Multiplayer
  | 3 -> Options
  | 4 -> Game (* TODO what is this? *)
  | 5 -> Tutorial
  | 6 -> Credits
  | 7 -> Protocol
  | 8 -> Multiplayer_Option
  | 9 -> Multiplayer_ForceSelect
  | 10 -> Multiplayer_GameFinder
  | 11 -> Multiplayer_Limit
  | 12 -> Startgame
  | 13 -> Intro
  | 14 -> Quit
  | 15 -> LoadSaveGame
  | 16 -> Options_Keymap
  | 17 -> Options_Graphics
  | 18 -> Options_Audio_Zoom
  | 19 -> Options_Video
  | 20 -> Options_Mouse
  | 21 -> Campaigns
  | _ -> raise Not_found

let setTitleMode titleMode =
  match titleMode with
  | Title -> 0
  | Singleplayer -> 1
  | Multiplayer -> 2
  | Options -> 3
  | Game (* TODO what is this? *) -> 4
  | Tutorial -> 5
  | Credits -> 6
  | Protocol -> 7
  | Multiplayer_Option -> 8
  | Multiplayer_ForceSelect -> 9
  | Multiplayer_GameFinder -> 10
  | Multiplayer_Limit -> 11
  | Startgame -> 12
  | Intro -> 13
  | Quit -> 14
  | LoadSaveGame -> 15
  | Options_Keymap -> 16
  | Options_Graphics -> 17
  | Options_Audio_Zoom -> 18
  | Options_Video -> 19
  | Options_Mouse -> 20
  | Campaigns -> 21

let handleMenu () =
  funer "beforeHandleMenu" vv ();
  let titleMode = getTitleMode () in
  begin
    match titleMode with
    | Title -> funer "runTitleMenu" vv ()
    | Singleplayer -> funer "runSinglePlayerMenu" vv ()
    | Multiplayer -> funer "runMultiPlayerMenu" vv ()
    | Options -> funer "runOptionsMenu" vv ()
    | Game -> funer "runGameOptionsMenu" vv ()
    | Tutorial -> funer "runTutorialMenu" vv ()
    | Credits -> funer "runCreditsScreen" vv ()
    | Protocol -> funer "runConnectionScreen" vv ()
    | Multiplayer_Option -> funer "runMultiOptions" vv ()
    | Multiplayer_ForceSelect -> () (* TODO *)
    | Multiplayer_GameFinder -> funer "runGameFind" vv ()
    | Multiplayer_Limit -> funer "runLimitScreen" vv ()
    | Startgame -> ()
    | Intro -> funer "showIntro" vv ()
    | Quit -> ()
    | LoadSaveGame -> ()
    | Options_Keymap -> funer "runKeyMapEditor" vv ()
    | Options_Graphics -> funer "runGraphicsOptionsMenu" vv ()
    | Options_Audio_Zoom -> funer "runAudioAndZoomOptionsMenu" vv ()
    | Options_Video -> funer "runVideoOptionsMenu" vv ()
    | Options_Mouse -> funer "runMouseOptionsMenu" vv ()
    | Campaigns -> funer "runCampaignSelector" vv ()
  end;
  funer "afterHandleMenu" vv ();
  titleMode
