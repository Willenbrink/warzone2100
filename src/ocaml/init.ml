let init () =
  let debug_init = funer "debug_init" vv in
  let i18n_init = funer "initI18n" vv in
  let initPhysFS = funer "initPhysFS" vv in
  let wzMain = funer "wzMain" vv in
  let initMain = funer "init" vv in
  debug_init ();
  i18n_init ();
  initPhysFS();
  initMain();
  wzMain();

  ()

