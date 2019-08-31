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

type listType =
  | Main
  | Mission
  | Limbo

let mapListTypes : listType -> int = function
  | Main -> 1
  | Mission -> 2
  | Limbo -> 3

(* Used in the assoc list:
   key: list * player
   value: droid list
   with list being either 1: currently active droids 2: mission droids 3: limbo droids (?) *)
type entry = (listType * int) * t list


let rec getGroup t =
  match funer "getDroidGroup" (ptr void @-> returning (ptr_opt void)) t with
  | Some x -> getDroid x :: getGroup (from_voidp void null)
  | None -> []
and getType t =
  let f = funer "getDroidType" (ptr void @-> returning int) in
  match f t with
  | 0 -> Weapon
  | 1 -> Sensor
  | 2 -> ECM
  | 3 -> Construct
  | 4 -> Person
  | 5 -> Cyborg
  | 6 -> Transporter (getGroup t)
  | 7 -> Command
  | 8 -> Repair
  | 9 -> Default
  | 10 -> CyborgConstruct
  | 11 -> CyborgRepair
  | 12 -> CyborgSuper
  | 13 -> Supertransporter (getGroup t)
  | 14 -> Any
  | _ -> raise Not_found
and getDroid t =
  let id = funer "getDroidId" (ptr void @-> returning int) t in
  let typ = getType t in
  {id; typ; pointer = t}

let getList lists id =
  let getList = funer "getDroidList" (int @-> int @-> returning (ptr_opt void)) in
  let rec f l =
    match getList id l with
    | Some x -> getDroid x :: f 0
    | None -> []
  in
  List.map mapListTypes lists
  |> List.map f
  |> List.flatten

let getAssoc () : entry list =
  let worker (acc : t list) (droid : t) =
    let nextEntry = match droid with
      | {typ = Transporter x; _} | {typ = Supertransporter x; _} -> droid::x
      | _ -> [droid]
    in
    nextEntry @ acc
  in
  Player.map (fun id -> (Main,id), getList [Main] id)
  @ Player.map (fun id -> (Mission,id), getList [Mission] id)
  @ Player.map (fun id -> (Limbo,id), getList [Limbo] id)
  |> List.map (fun (key,droids) -> key,List.fold_left worker [] droids)

let applyAssoc (f : entry list -> 'a) =
  getAssoc ()
  |> f

let mapAssoc f = applyAssoc (List.map f)
let iterAssoc f = applyAssoc (List.iter f)
let foldAssoc f acc = applyAssoc (List.fold_left f acc)

let apply f =
  getAssoc ()
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
