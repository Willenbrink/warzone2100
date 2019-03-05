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

(* list * player -> droid list*)
type entry = (int * int) * t list


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

let getList list id =
  let getList = funer "getDroidList" (int @-> int @-> returning (ptr_opt void)) in
  let rec f l =
    match getList id l with
    | Some x -> getDroid x :: f 0
    | None -> []
  in
  f list

let getAssoc () : entry list =
  let worker (acc : t list) (droid : t) =
    let nextEntry = match droid with
      | {typ = Transporter x; _} | {typ = Supertransporter x; _} -> droid::x
      | _ -> [droid]
    in
    nextEntry @ acc
  in
  Player.map (fun id -> (1,id), getList 1 id)
  :: Player.map (fun id -> (2,id), getList 2 id)
  :: Player.map (fun id -> (3,id), getList 3 id)
  :: []
  |> List.flatten
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
