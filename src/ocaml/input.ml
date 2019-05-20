open Interface
open Tsdl
open Sdl.Event

type state =
  | Up
  | Pressed (* Pressed for one frame *)
  | Down (* Pressed for multiple frames *)
  | Released
  | PressRelease (* Pressed and released in one frame *)
    (* TODO Only mouse *)
  | Doubleclick
  | Drag

let stateAssoc =
  [
    0, Up;
    1, Pressed;
    2, Down;
    3, Released;
    4, PressRelease;
    5, Doubleclick;
    6, Drag;
  ]

let getKey key =
  funer "getKey" (int @-> returning int) key
  |> fun x -> List.assoc x stateAssoc

let setKey key (state : state) =
  (fun x -> List.map (fun (x,y) -> y,x) stateAssoc |> List.assoc x) state
  |> funer "setKey" (int @-> int @-> returning void) key

let handleKeyPress event =
  let virtualKey = get event keyboard_keycode |> funer "getSymKeySE" (int @-> returning int) in
  funer "inputAddBuffer" (int @-> int @-> returning void) virtualKey 0;
  let keycode = funer "sdlToKeyCode" (int @-> returning int) virtualKey in
  let newState = match getKey keycode with
    | Up | Released | PressRelease -> Pressed
    | x -> x
  in
  setKey keycode newState

let handleKeyRelease event =
  let virtualKey = get event keyboard_keycode in
  let keycode = funer "sdlToKeyCode" (int @-> returning int) virtualKey in
  let newState = match getKey keycode with
  | Pressed -> PressRelease
  | Down -> Released
  | x -> x
  in
  setKey keycode newState

let handleText event =
  let str = get event text_input_text in
  match String.length str with
  | 1 -> funer "inputAddBuffer" (string @-> returning void) str
  | 0 | _ -> ()

let getMouse key =
  let res = funer "getMouse" (int @-> returning int) key in
  List.assoc res stateAssoc

let setMouse key state =
  (fun x -> List.map (fun (x,y) -> y,x) stateAssoc |> List.assoc x) state
  |> funer "setMouse" (int @-> int @-> returning void) key

let handleMouseWheel event =
  let x = get event mouse_wheel_x in
  let y = get event mouse_wheel_y in
  if x > 0 || y > 0
  then setMouse 6 Pressed
  else setMouse 7 Pressed

let handleMouseMotion event =
  let mouse = get event mouse_motion_which in
  let x,y = get event mouse_motion_x, get event mouse_motion_y in
  let dragKey = funer "getDragKey" (void @-> returning int) () in
  let setPos x y =
    funer "setMouseX" (int @-> returning void) x;
    funer "setMouseY" (int @-> returning void) y
  in
  setPos x y;
  match getMouse dragKey with
  | Pressed | Down ->
    if abs ((funer "getx" (void @-> returning int) ()) - x) > 5
    || abs ((funer "gety" (void @-> returning int) ()) - y) > 5 (*TODO Drag Threshold*)
    then setMouse dragKey Drag
    else ()
  | _ -> ()
(*funer "handleMotionTmp" (int @-> int @-> returning void) x y*)

type vector2i
  let vector2i : vector2i structure typ = structure "Vector2i"
let x = field vector2i "x" int
let y = field vector2i "y" int
let () = seal vector2i

type mousePress
let mousePress : mousePress structure typ = structure "MousePress"
let key = field mousePress "key" int
let action = field mousePress "action" int
let pos = field mousePress "pos" vector2i
let () = seal mousePress

let setMousePos mouse b pos =
  funer "setMousePos" (int @-> bool @-> vector2i @-> returning void) mouse b pos

let pushMouses mousepress =
  funer "pushMouses" (mousePress @-> returning void) mousepress

let getMouseEvent event =
  let mouse = get event mouse_button_button in
  let xp,yp = get event mouse_button_x, get event mouse_button_y in
  let p = make vector2i in
  setf p x xp;
  setf p y yp;
  let mp = make mousePress in
  setf mp pos p;
  setf mp key mouse;
  mouse,p,mp

let handleMousePress event =
  let mouse,p,mp = getMouseEvent event in
  (*
  setf mp action 1;
  pushMouses mp;
  setMousePos mouse true p;

  match getMouse mouse with
  | Up | Released | PressRelease -> funer "setKeyDown" (int @-> returning void) mouse
  | _ -> ()
     *)
  funer "handleMouseTmp" (int @-> int @-> int @-> bool @-> returning void) mouse (getf p x) (getf p y) true

let handleMouseRelease event =
  let mouse,p,mp = getMouseEvent event in
  (*
  setf mp action 2;
  pushMouses mp;
  setMousePos mouse false p;

  match getMouse mouse with
  | Pressed -> setMouse mouse PressRelease
  | Down | Drag | Doubleclick -> setMouse mouse Released
  | PressRelease | Up | Released -> ()
     *)
  funer "handleMouseTmp" (int @-> int @-> int @-> bool @-> returning void) mouse (getf p x) (getf p y) false

let handleWindow window event =
  let setMouse = funer "setMouseInWindow" (bool @-> returning void) in
  match get event window_event_id |> window_event_enum with
  | `Focus_gained -> setMouse true
  | `Focus_lost -> setMouse false
  | `Size_changed | `Resized ->
    let sizex, sizey = window |> Sdl.get_window_size in
    let setWarSize (x,y) =
      funer "war_set_width" (int @-> returning void) x;
      funer "war_set_height" (int @-> returning void) y;
    in

    (*
    print_int sizex; print_endline "";
    funer "pie_setVideoBufferWidth" (int @-> returning void) sizex;
    funer "pie_setVideoBufferHeight" (int @-> returning void) sizey;
    funer "pie_update_surface_geometry" vv ();
    funer "screen_update_geometry" vv ();
    funer "glUpdate" vv ();

       *)
    funer "handleTmp" (int @-> int @-> int @-> int @-> returning void) sizex sizey sizex sizey;
    setWarSize (sizex,sizey)
  | _ -> ()
















