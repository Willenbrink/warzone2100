open Interface

type t = {id : int}

let amount () = funer "getMaxPlayers" (void @-> returning int) ()

let apply f =
  List.init (amount ()) (fun id -> id)
  |> f

let map f = apply @@ List.map @@ f
let iter f = apply @@ List.iter @@ f
