open Interface

type typ =
  | Weapon
  | Sensor
  | ECM
  | Construct
  | Person
  | Cyborg
  | Transporter of t list
  | Supertransporter of t list
  | Command
  | Repair
  | Default
  | CyborgConstruct
  | CyborgRepair
  | CyborgSuper
  | Any
and t = {id : int; typ : typ; pointer : (unit Ctypes_static.ptr)}

type list_type =
  | Main
  | Mission
  | Limbo

let map_list_types : list_type -> int = function
  | Main -> 1
  | Mission -> 2
  | Limbo -> 3

(* Used in the assoc list:
   key: list * player
   value: droid list
   with list being either 1: currently active droids 2: mission droids 3: limbo droids (?) *)
type entry = (list_type * int) * t list


let rec get_group t =
  match funer "getDroidGroup" (ptr void @-> returning (ptr_opt void)) t with
  | Some x -> get_droid x :: get_group (from_voidp void null)
  | None -> []
and get_type t =
  let f = funer "getDroidType" (ptr void @-> returning int) in
  match f t with
  | 0 -> Weapon
  | 1 -> Sensor
  | 2 -> ECM
  | 3 -> Construct
  | 4 -> Person
  | 5 -> Cyborg
  | 6 -> Transporter (get_group t)
  | 7 -> Command
  | 8 -> Repair
  | 9 -> Default
  | 10 -> CyborgConstruct
  | 11 -> CyborgRepair
  | 12 -> CyborgSuper
  | 13 -> Supertransporter (get_group t)
  | 14 -> Any
  | _ -> raise Not_found
and get_droid t =
  let id = funer "getDroidId" (ptr void @-> returning int) t in
  let typ = get_type t in
  {id; typ; pointer = t}

let get_list lists id =
  let get_list = funer "getDroidList" (int @-> int @-> returning (ptr_opt void)) in
  let rec f l =
    match get_list id l with
    | Some x -> get_droid x :: f 0
    | None -> []
  in
  List.map map_list_types lists
  |> List.map f
  |> List.flatten

let get_assoc () : entry list =
  let worker (acc : t list) (droid : t) =
    let next_entry = match droid with
      | {typ = Transporter x; _} | {typ = Supertransporter x; _} -> droid::x
      | _ -> [droid]
    in
    next_entry @ acc
  in
  Player.map (fun id -> (Main,id), get_list [Main] id)
  @ Player.map (fun id -> (Mission,id), get_list [Mission] id)
  @ Player.map (fun id -> (Limbo,id), get_list [Limbo] id)
  |> List.map (fun (key,droids) -> key,List.fold_left worker [] droids)

let apply_assoc (f : entry list -> 'a) =
  get_assoc ()
  |> f

let map_assoc f = apply_assoc (List.map f)
let iter_assoc f = apply_assoc (List.iter f)
let fold_assoc f acc = apply_assoc (List.fold_left f acc)

let apply f =
  get_assoc ()
  |> List.map (fun (_,droids) -> droids)
  |> List.flatten
  |> f

let map f = apply (List.map f)
let iter f = apply (List.iter f)
let fold f acc = apply (List.fold_left f acc)

let update {pointer; _} =
  funer "syncDebugDroid" (ptr void @-> char @-> returning void) pointer '<';
  funer "droidUpdate" (ptr void @-> returning void) pointer

let update_mission {pointer; _} =
  funer "missionDroidUpdate" (ptr void @-> returning void) pointer
