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

let getList id list =
  let getList = funer "getDroidList" (int @-> int @-> returning (ptr_opt void)) in
  let rec f l =
    match getList id l with
    | Some x -> getDroid x :: f 0
    | None -> []
  in
  f list
