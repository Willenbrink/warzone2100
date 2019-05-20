open Interface

type typ =
  | HQ
  | Factory
  | FactoryModule
  | Generator
  | GeneratorModule
  | Pump
  | Defense
  | Wall
  | WallCorner
  | Generic
  | Research
  | ResearchModule
  | RepairFacility
  | CommandControl
  | Bridge
  | Demolish (*TODO why? what? "//the demolish structure type - should only be one stat with this type"*)
  | CyborgFactory
  | VTOLFactory
  | Lab (*TODO Difference to research? Some campaign object?*)
  | RearmPad
  | MissileSilo
  | SatUplink
  | Gate

type status =
  | BeingBuilt
  | Built
  | BP_Valid
  | BP_Invalid
  | BP_Planned
  | BP_Planned_Ally

type t = {id : int; typ : typ; pointer : (unit Ctypes_static.ptr); status : status}

let getStatus t =
  let state = funer "getBuildingStatus" (ptr void @-> returning int) t in
  match state with
  | 0 -> BeingBuilt
  | 1 -> Built
  | 2 -> BP_Valid
  | 3 -> BP_Invalid
  | 4 -> BP_Planned
  | 5 -> BP_Planned_Ally
  | _ -> raise Not_found

let rec getType t =
  let f = funer "getBuildingType" (ptr void @-> returning int) in
  match f t with
  | 0 -> HQ
  | 1 -> Factory
  | 2 -> FactoryModule
  | 3 -> Generator
  | 4 -> GeneratorModule
  | 5 -> Pump
  | 6 -> Defense
  | 7 -> Wall
  | 8 -> WallCorner
  | 9 -> Generic
  | 10 -> Research
  | 11 -> ResearchModule
  | 12 -> RepairFacility
  | 13 -> CommandControl
  | 14 -> Bridge
  | 15 -> Demolish (*TODO why? what? "//the demolish structure type - should only be one stat with this type"*)
  | 16 -> CyborgFactory
  | 17 -> VTOLFactory
  | 18 -> Lab (*TODO Difference to research? Some campaign object?*)
  | 19 -> RearmPad
  | 20 -> MissileSilo
  | 21 -> SatUplink
  | 22 -> Gate
  | _ -> raise Not_found
and getBuilding t =
  let id = funer "getBuildingId" (ptr void @-> returning int) t in
  let typ = getType t in
  let status = getStatus t in
  {id; typ; pointer = t; status}

let getList id list =
  let getList = funer "getBuildingList" (int @-> int @-> returning (ptr_opt void)) in
  let rec f l =
    match getList id l with
    | Some x -> getBuilding x :: f 0
    | None -> []
  in
  f list

let _privateGetList () =
  let get list id = getList id list in
  let buildings = Player.map (fun id -> (1,id),get 1 id) :: Player.map (fun id -> (2,id),get 2 id) :: [] in
  buildings
|> List.flatten

let applyAssoc f =
  let buildings = _privateGetList () in
  buildings |> f

let mapAssoc f = applyAssoc (List.map f)
let iterAssoc f = applyAssoc (List.iter f)
let foldAssoc f acc = applyAssoc (List.fold_left f acc)

let apply f =
  let buildings = _privateGetList () in
  buildings
|> List.map (fun (_,buildings) -> buildings)
|> List.flatten
|> f

let map f = apply (List.map f)
let iter f = apply (List.iter f)
let fold f acc = apply (List.fold_left f acc)

let update {pointer; _} is_mission =
  funer "structureUpdate" (ptr void @-> bool @-> returning void) pointer is_mission
