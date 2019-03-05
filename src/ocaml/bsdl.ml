(*Broad SDL Bindings*)

open Interface
open Tsdl

let (>>=) f g =
  match f with
  | Error (`Msg e) ->
    Sdl.log "Error: %s" e;
    exit 1
  | Ok x ->
    g x

let id x = x

let (>>|) f () = (>>=) f id

let init () =
  Sdl.init Sdl.Init.(video + timer)
  >>= id;
  let wzSdlAppEvent = match Sdl.register_event () with
  | None -> exit 1
  | Some x -> x
  in
  Sdl.(gl_set_attribute Gl.doublebuffer 1)
  >>= id;
  Sdl.(gl_set_attribute Gl.stencil_size 8)
  >>= id;
  (match funer "getAntialiasing" (void @-> returning int) () with
  | 0 -> ()
  | x ->
    Sdl.(gl_set_attribute Gl.multisamplebuffers 1) >>| ();
    Sdl.(gl_set_attribute Gl.multisamplesamples x) >>| ());

  List.init (Sdl.get_num_video_displays () >>= id) id
  |> List.map (fun x ->
      Sdl.get_num_display_modes x >>= id
      |> (fun y ->
          ()

        )

    )

