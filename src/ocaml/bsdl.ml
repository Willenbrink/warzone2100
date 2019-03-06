(*Broad SDL Bindings*)

open Interface
open Tsdl

let (>>=) f g =
  match f with
  | Error (`Msg e) ->
    Sdl.log "Error: %s" e;
    Printexc.get_callstack 20 |> Printexc.raw_backtrace_to_string |> print_endline;
    exit 1
  | Ok x ->
    g x

let id x = x

let setAA () =
  match funer "getAntialiasing" (void @-> returning int) () with
  | 0 -> ()
  | x ->
    Sdl.(gl_set_attribute Gl.multisamplebuffers 1) >>= id;
    Sdl.(gl_set_attribute Gl.multisamplesamples x) >>= id

let setVsync () =
  funer "getVsync" (void @-> returning bool) ()
  |> funer "setVsync" (bool (*FIXME This is an int!*)@-> returning void)

let sdl_init () =
  Sdl.init Sdl.Init.(video + timer) >>= id;
  Sdl.(gl_set_attribute Gl.doublebuffer 1) >>= id;
  Sdl.(gl_set_attribute Gl.stencil_size 8) >>= id;

  setAA ();
  setVsync ();
  ()

let init () =
  sdl_init ();
  (*TODO find out the purpose of this*)
  let wzSdlAppEvent = match Sdl.register_event () with
  | None -> exit 1
  | Some x -> x
  in

  let displays = List.init (Sdl.get_num_video_displays () >>= id) id in
  let displayList =
    List.fold_left (fun acc x ->
        let numModes = Sdl.get_num_display_modes x >>= fun x -> x - 1 (*FIXME why decrement?*) in
        (Sdl.get_display_mode x numModes >>= id) :: acc
      ) [] displays
  in
  List.iteri (fun i (display : Sdl.display_mode) ->
      let push = funer "pushResolution" (int @-> int @-> int @-> int @-> returning void) in
      let hertz = match display.dm_refresh_rate with Some x -> x | None -> 0 in
      push display.dm_w display.dm_h hertz i
    ) displayList;
  (*List.map (fun i -> Sdl.get_current_display_mode i >>= id) displays*)
  let flags = Sdl.Window.(opengl + shown) in
  let flags =
    if funer "getFullscreen" (void @-> returning bool) ()
    then Sdl.Window.(flags + fullscreen)
    else Sdl.Window.(flags + resizable)
  in
  let window : Sdl.window = Sdl.create_window "Warzone 2100" ~w:800 ~h:600 flags >>= id in
  funer "setWindow" (nativeint @-> returning void) (Sdl.unsafe_ptr_of_window window);
  Sdl.set_window_position window ~x:Sdl.Window.pos_centered ~y:Sdl.Window.pos_centered;
  let x,y = Sdl.get_window_size window in
  let setBufferSize (x,y) =
    funer "pie_setVideoBufferWidth" (int @-> returning void) x;
    funer "pie_setVideoBufferHeight" (int @-> returning void) y;
  in
  setBufferSize (x,y);
  let context = Sdl.gl_create_context window >>= id in
  funer "sdlInitCursors" (void @-> returning void) ();
  funer "initGL" (int @-> int @-> returning void) x y;

  (*TODO Displayscale / High DPI*)
