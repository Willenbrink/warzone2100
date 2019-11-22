open Interface

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
  Loop.init_frontend ();
  state

let () = (* main/Entry point *)
  Parser.parse (); (* Parse commandline parameters *)
  let sdl_state = init () in (* Initialise everything that is only initialised once per execution *)
  let halt = funer "halt" vv in (* Handle the exit gracefully *)

  try
    Loop.execution_loop sdl_state
  with
  | Exit -> halt ()
  | e -> halt (); raise e
