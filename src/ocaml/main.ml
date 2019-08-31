open Interface

let init () =
  let debug_init = funer "debug_init" vv in
  let i18n_init = funer "initI18n" vv in
  let initPhysFS = funer "initPhysFS" vv in
  let wzMain = funer "wzMain" vv in
  let initMain1 = funer "init" vv in
  let initMain2 = funer "init2" vv in
  let init_frontend () = funer "frontendInitialise" (string @-> returning bool) "wrf/frontend.wrf" in
  debug_init ();
  i18n_init ();
  initPhysFS();
  initMain1();
  let state = Bsdl.init () in
  initMain2();
  (match init_frontend () with false -> raise Halt | true -> ());
  wzMain();
  state

let () = (* Entry point *)
  Parser.parse ();
  let sdlState = init () in
  let halt = funer "halt" vv in
  try
    Loop.loop sdlState
  with
  | Halt | Pervasives.Exit -> halt ()
  | e -> halt (); raise e
