open Interface

(* 1 unit = 1 second / this_value *)
let units_per_second = 1000
(* Update the gameworld this_value times per second *)
let ticks_per_second = 30
(* How long is each update in time-units? *)
let units_per_update = units_per_second / ticks_per_second

let current_time () = Tsdl.Sdl.get_ticks () |> Int32.to_int

let update () =
  funer "realTimeUpdate" vv ();
  funer "countFps" vv ();
  ()
