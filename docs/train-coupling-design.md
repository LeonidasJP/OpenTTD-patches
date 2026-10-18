# Ontwerp: treinen koppelen en splitsen op een station (JGRPP)

Status: **concept v3, alle vragen beantwoord**. Er is nog geen code geschreven.
Branch: `feature/train-coupling`

## 1. Doel

Een trein die uit meerdere treinstellen bestaat, kan op een station gesplitst worden. Twee of meer treinen kunnen op een station tot één trein samengevoegd worden.

- Een trein die komt aankoppelen, mag met beperkte snelheid langs het sein voor het bezette perron. Hij rijdt tot achter de wachtende trein en koppelt daar. Er mag **nooit** een botsing ontstaan.
- Elk treinstel houdt zijn eigen orderlijst en dienstregeling. Na het splitsen gaat elk stel vanaf dat station verder met zijn eigen dienstregeling.

### Besluiten van de opdrachtgever

| Onderwerp | Besluit |
|---|---|
| Dienstregeling tijdens het gekoppelde traject | Beide dienstregelingen lopen door. Het passieve stel toont de orders die het met de leider deelt **grijs en alleen-lezen**. |
| Herkomst van die grijze orders | Ze worden **automatisch gespiegeld** van de leidende trein. |
| Welke trein leidt | Dit wordt **in de order** ingesteld. |
| Met wie wordt gekoppeld | Met een trein uit een opgegeven **groep of orderlijst** die op dat station een koppelorder heeft. Treinen uit dezelfde groep of orderlijst mogen ook met elkaar koppelen. |
| De partner is (te) laat | Wachten zolang de dienstregeling toestaat, daarna alleen verder. |
| Wat elk stel doet na het splitsen | Elk stel volgt zijn **eigen orderlijst**. Gebruiken twee stellen dezelfde lijst, dan bepaalt een conditionele order wie wat doet. |
| Te ondersteunen seinen | **Padseinen (PBS)** en **realistisch remmen**. Blokseinen vallen buiten de scope. |
| Seinbeeld | Het **rangeerbeeld** als het sein dat heeft. Anders **groen knipperend**. Lukt dat niet, dan **groen en geel tegelijk**. |
| Reizigers | Reizigers komen, voor zover mogelijk, terecht in het treinstel dat naar hun bestemming gaat. Op het gedeelde traject wisselen ze zo nodig van stel. |
| Depot | Service "in en uit" mag zonder melding. Echt stoppen in het depot mag, maar wordt **afgeraden** met een melding. Een vlag op de depotorder schakelt die melding uit. In het depot mag je alleen **ombouwen (refit) en starten of stoppen**. |
| Seinbeeld bij een NewGRF-sein zonder ondersteuning | **Geel** ("Prepare to stop"). |
| Aantal treinen per combinatie | **Hoogstens twee.** Een trein die al gekoppeld rijdt, kan geen tweede `Koppelen`-order krijgen. Elke trein mag zelf uit meerdere fysieke stellen bestaan, bijvoorbeeld 2-2 + 3 = 2-2-3 (§5.3). |
| Koppelsnelheid | **40 km/u** vanaf het sein en **15 km/u** op de laatste tegel. |

## 2. Begrippen

- **Treinstel / trein (unit):** een oorspronkelijke, zelfstandige trein (dienst) met een eigen voorste voertuig, orders en dienstregeling. Zo'n trein kan zelf uit meerdere fysieke stellen bestaan; die worden niet apart behandeld.
- **Combinatie:** twee gekoppelde treinen die als één `Train`-keten rijden.
- **Leider:** het stel waarvan het voorste voertuig het voorste voertuig van de combinatie is. Zijn orders bepalen de route.
- **Passief stel:** een stel in de combinatie dat niet leidt. Zijn eigen gegevens staan in een **koppelrecord** (§5).

## 3. Orders

### 3.1 Nieuwe vlaggen op de stationsorder

Een nieuw ordertype is niet handig, want er is nog maar één waarde vrij (`OT_END` = 15, type van 4 bits). Daarom komt alles als vlaggen op `OT_GOTO_STATION`. Bij stationsorders zijn bits 8–15 van `Order::flags` nog vrij.

| Bits | Betekenis |
|---|---|
| 8–9 | Koppelmodus: `Geen` / `Koppelen` / `Splitsen` |
| 10 | Leider bij koppelen: `Wachtende trein leidt` / `Aankomende trein leidt` |
| 11–15 | Gereserveerd |

Het **partnerfilter** (alleen bij `Koppelen`) staat in `OrderExtraInfo`:

- `xdata2` bits 0–1 geven het filtertype: `Elke` / `Groep` / `Orderlijst`.
- `xdata` bevat het `GroupID` of het `OrderListID`.

Een orderlijst kies je in de GUI door op een trein met die lijst te klikken, zoals bij "orders delen". Verdwijnt de groep of lijst, dan valt het filter terug op `Elke` en geeft `CheckOrders` een waarschuwing.

### 3.2 `Koppelen` werkt symmetrisch

Eén vlag dekt beide rollen, dus het werkt ook als beide stellen dezelfde orderlijst hebben.

- **Er staat nog geen geschikte partner:** de trein rijdt het perron op en **wacht op koppeling**.
  - Hij wacht maximaal de wachttijd uit de dienstregeling.
  - Is er geen wachttijd ingesteld, dan wacht hij één laad- en loscyclus.
  - Daarna vertrekt hij alleen (§3.5).
- **Er staat al een geschikte partner te wachten:** de trein rijdt het bezette perron op (§6) en koppelt.

Twee treinen passen bij elkaar als aan alle drie de voorwaarden is voldaan:

- **beide** hebben voor dit station een `Koppelen`-order als huidige order;
- elk valt binnen het filter van de ander;
- de partner staat stil in het perron, met `OT_LOADING` of `OT_WAITING` voor deze order.

Een **combinatie** is nooit een geschikte partner, en een combinatie koppelt zelf ook niet (§5.3).

**Wie leidt:** de leidervlag in de order van de **aankomende** trein beslist. Wijkt de wachtende trein daarvan af, dan geeft `CheckOrders` een waarschuwing.

### 3.3 `Splitsen`

- De combinatie stopt op een station.
- Heeft een passief stel daar een `Splitsen`-order als huidige order, dan wordt dat stel afgesplitst.
- Heeft de leider daar een `Splitsen`-order, dan worden **alle** stellen losgemaakt. Dat is nodig voor één gedeelde orderlijst.
- Het splitsen gebeurt **bij aankomst, vóór het laden en lossen**. Daarna laadt en lost elk stel volgens zijn eigen order, en gaat verder met de volgende order uit zijn eigen lijst. Dit is ook de basis voor de juiste verdeling van reizigers (§7).
- Voor het splitsen van een stel dat in het midden van de combinatie staat, zie §5.3.

### 3.4 Gespiegelde orders bij het passieve stel

- In de **opgeslagen** orderlijst van het passieve stel staan alleen zijn eigen orders, bijvoorbeeld `… B (Koppelen) → E (Splitsen) → …`.
- Zolang het stel gekoppeld is, toont het orderscherm daartussen de orders van de leider. Het gaat om de orders vanaf het koppelpunt tot het station van de `Splitsen`-order. Ze zijn **grijs en alleen-lezen**, met de tijden van de leider.
- Stopt de combinatie op een station dat de huidige order van het passieve stel is, maar zonder `Splitsen`, dan gaat ook de order van het passieve stel een stap verder. Zo werkt het ook als beide lijsten alle stops bevatten, zoals bij één gedeelde lijst.
- In het dienstregelingsscherm is `B → E` één rit, met de gespiegelde stops grijs eronder. De vertraging van het passieve stel wordt normaal berekend bij aankomst op E.

**Controle bij het koppelen:** het station van de volgende `Splitsen`-order of stationsorder van het passieve stel moet voorkomen in de komende orders van de leider (één ronde vooruit). Zo niet, dan wordt er niet gekoppeld, want het stel zou nooit meer loskomen. De speler krijgt dan een nieuwsbericht.

### 3.5 Nieuwe conditionele variabele

`OrderConditionVariable::CouplingRole` krijgt waarde 23. Daar is ruimte voor, want het veld heeft 5 bits. Mogelijke vragen aan deze variabele:

- is dit stel gekoppeld;
- leidt dit stel;
- was dit stel bij de laatste splitsing het afgesplitste deel;
- is het laatste koppelen mislukt (wachttijd verstreken).

Voorbeeld met één gedeelde orderlijst (A→B met twee stellen, B→C met één):

```
1  A  laden
2  B  Splitsen
3  Ga naar 6 als "was afgesplitst deel"
4  C  laden
5  B  Koppelen (orderlijst: deze, aankomende leidt)  → daarna naar 7
6  B  Koppelen (orderlijst: deze)                    [afgesplitst deel wacht hier]
7  A
```

### 3.6 Depotorder (zie ook §8)

Er komt een nieuwe vlag in `OrderDepotExtraFlags`: **"Gekoppelde combinatie toegestaan"**.

## 4. Fysiek koppelen en splitsen

### 4.1 Volgorde in de keten en rijrichting

- **Zelfde richting** (de aankomende trein rijdt tegen de achterkant van de wachtende): de keten wordt `wachtende.kop … wachtende.staart, aankomende.kop … aankomende.staart`. Er hoeft geen voertuig om.
- **Kop tegen kop:** de wachtende trein, of combinatie, wordt eerst omgedraaid met de bestaande omkeerfuncties. Dat mag, want hij staat stil in het station.
- Daarna wordt de kop van de leider de kop van de combinatie. Ligt die aan de "verkeerde" kant, dan regelt de bestaande JGRPP-logica dat: `DrivingBackwards` of omkeren.
- Per stel slaat het koppelrecord op welk voertuig het eerste en welk het laatste is, en of het stel omgedraaid is. Bij het splitsen wordt dat precies teruggezet.

### 4.2 `CoupleTrains(leader, passive)`

Dit is gebaseerd op de relinkcode van `CmdMoveRailVehicle`, maar zonder de eis dat de trein in een depot staat.

1. Controleer de voorwaarden:
   - de lengte is hoogstens `max_train_length`;
   - geen van beide treinen is al gekoppeld (§5.3);
   - de NewGRF-callback `CheckTrainAttachment` staat het toe;
   - beide treinen hebben dezelfde eigenaar.
2. Haal de passieve kop uit het station: verwijder hem uit `loading_vehicles`, reken `cargo_payment` af en voer `LeaveStation` uit **zonder** de dienstregeling bij te werken.
3. Maak het koppelrecord aan (§5) en verplaats de eigen gegevens van de passieve kop daarheen.
4. Koppel de ketens:
   - `SetNext`, `NormaliseSubtypes` en `NormaliseDualHeads`;
   - daarna `NormaliseTrainHead`, `ConsistChanged(CCF_TRACK)`, `CheckBreakdownFlags` en `InvalidateVehicleTickCaches`;
   - tel de groepsstatistieken opnieuw;
   - zet `LastLoadStationSeparate`, zodat elke wagon zijn eigen laadstation houdt.
5. Leg de spoorreserveringen opnieuw met `ReserveTrackUnderConsist` voor de hele combinatie, en reset `lookahead`.
6. De leider gaat verder met laden en lossen op dit station volgens zijn huidige order.

Dit gebeurt atomair binnen één tick. Alle voorwaarden worden vooraf gecontroleerd, zodat er nooit iets teruggedraaid hoeft te worden.

### 4.3 `SplitTrain(combination, unit)`

1. Knip de keten op de grenzen uit het record.
2. Zet `GVSF_FRONT` en de gegevens uit het record terug.
3. Draai het stel terug als het bij het koppelen omgedraaid is.
4. Elk deel krijgt `NormaliseTrainHead`, reserveert het spoor onder zich opnieuw en doet `BeginLoading` met zijn eigen order.
5. PBS regelt de volgorde van vertrekken.

## 5. Datamodel: koppelrecord

### 5.1 Record

Een nieuwe pool `CoupledUnit`, naar het voorbeeld van `OrderBackup` (`order_backup.h`). Het record erft van `BaseConsist`, dat al de naam, dispatch-records, `current_order_time`, `lateness_counter`, orderindexen en `vehicle_flags` bevat.

```cpp
struct CoupledUnit : CoupledUnitPool::PoolItem<&_coupled_unit_pool>, BaseConsist {
	VehicleID leader;          ///< Kop van de combinatie waarin dit stel zit
	VehicleID head;            ///< Oorspronkelijke kop van dit stel (nu niet-voorste voertuig)
	VehicleID last;            ///< Laatste voertuig van dit stel
	uint8_t position;          ///< Volgorde in de combinatie (0 = direct achter de leider)
	bool reversed;             ///< Stel is bij het koppelen omgedraaid
	OrderList *orders;         ///< Eigen orderlijst (gedeeld of niet)
	Order current_order;
	UnitID unitnumber;         ///< Blijft gereserveerd zolang het stel gekoppeld is
	GroupID group_id;
	TileIndex dest_tile;
	StationID last_station_visited;
	Money profit_this_year, profit_last_year, profit_lifetime;
	/* speed_restriction, SpeedAdaptationExempt, unbunch_state, ... */
};
```

### 5.2 Koppeling met de rest van het spel

- **Gedeelde orderlijst:** de passieve kop moet uit `next_shared`, want hij is geen voorste voertuig meer. Het record registreert zich daarom apart bij de `OrderList`. Alle code die `FirstShared` of `NextShared` gebruikt, moet daarop nagelopen worden. Het gaat om afstand houden tussen treinen, vertrekborden, de lijst met gedeelde voertuigen en scheduled dispatch. **Dit is het grootste risico** (§10).
- **Slots en tellers:** een stel blijft lid van zijn slots, op basis van het `VehicleID` van de oorspronkelijke kop.
- **Tick:** de leider verhoogt bij elke tick ook `current_order_time` van al zijn records.
- **Op `Train`:**
  - `coupling_target` (`VehicleID`): de partner tijdens de aanrijding;
  - vlag `VehicleRailFlag::CouplingApproach`;
  - vlag `VehicleRailFlag::CoupledCombination`: snelle controle of een trein een combinatie is.

### 5.3 Hoogstens twee treinen per combinatie

**Besluit:** een combinatie bestaat voorlopig uit hoogstens **twee treinen**: één leider en één passieve trein.

- **Trein = dienst.** Een "trein" in dit ontwerp is een `Train` met een eigen orderlijst. Hoe die fysiek is opgebouwd, maakt niet uit.
  - Voorbeeld: dienst 1 bestaat uit twee stellen van twee wagons, die in het depot als één trein zijn samengesteld. Dienst 2 is één stel van drie wagons. Samen vormen ze **2-2-3**.
  - Bij het splitsen komen precies **2-2** en **3** terug.
  - Grenzen tussen fysieke stellen binnen één dienst worden niet bijgehouden en nooit gesplitst.
- **Een trein die al gekoppeld is, kan niet nog een keer koppelen.** Dat wordt op drie niveaus afgedwongen:
  1. **Command (`CmdInsertOrder` / `CmdModifyOrder`):** zolang een trein gekoppeld rijdt, weigert het spel om in zijn orderlijst een order met `Koppelen` in te voegen of een order daarin te veranderen. De foutmelding is: "Trein rijdt gekoppeld: kan geen tweede koppeling krijgen". Dit geldt voor de leider en voor het passieve stel.
  2. **Statische controle (`PreInsertOrderCheck` / `CheckOrders`):** tussen een `Koppelen`-order en de eerstvolgende `Splitsen`-order in dezelfde lijst mag geen tweede `Koppelen`-order staan. Zo'n order wordt geweigerd bij het invoegen. Een bestaande lijst met dit probleem geeft een waarschuwing.
  3. **Tijdens het spel:** komt een combinatie toch bij een `Koppelen`-order, bijvoorbeeld omdat de leider geen `Splitsen`-order heeft, dan geldt die order als **gewone stop zonder koppelen**, met een nieuwsbericht. Een combinatie is ook nooit een geschikte partner voor een aankomende trein (§3.2).
- **Datamodel:** het record (§5.1) blijft per trein opgezet. Een latere uitbreiding naar meer treinen kan dus zonder dat de savegame verandert. Het veld `position` is altijd 0.

## 6. Bezet perron oprijden (PBS en realistisch remmen)

### 6.1 Beslissing bij het sein

De normale reservering mislukt omdat het perron bezet is. Dan wordt in `ChooseTrainTrack` / `TryPathReserveWithResultFlags` gecontroleerd:

1. Is de huidige order `Koppelen` voor dit station?
2. Staat er een geschikte partner (§3.2) bereikbaar in een perron?
3. Zo ja, dan plant YAPF met als bestemming het perron van de partner, in de nieuwe modus **"gedeeltelijke perronreservering"**:
   - `ReserveRailStationPlatform` reserveert alleen de vrije perrontegels, tot de tegel vóór de partner;
   - de tegel met de achterkant van de partner is al door die partner gereserveerd en telt als eindpunt.
4. Zet `coupling_target` en `CouplingApproach`. Markeer het ingangssein als **"aankoppelen toegestaan"** (§6.2).

### 6.2 Seinbeeld

Dit volgt de wens van de opdrachtgever. Per sein is één extra bit nodig: **"aankoppelen toegestaan"**.

- **Opslag:** `m3` bits 0–3 van rail-tegels lijken vrij, één bit per seinpositie, net als de "sein aanwezig"-bits 4–7. Dit nog verifiëren met `docs/landscape_grid.html`.
- **Bit zetten:** bij het maken van de koppelreservering (§6.1).
- **Bit wissen:** zodra het sein weer rood wordt. Dat gebeurt op dezelfde plekken als waar het seinbeeld nu op 0 wordt gezet, zoals `pbs.cpp:95`.
- Het seinbeeld (de waarde van het aspect) zelf wordt **niet** hergebruikt. Remberekening en het doorgeven van seinbeelden lezen die waarde, en die mogen niet veranderen.

| Situatie | Wat het sein toont | Hoe |
|---|---|---|
| Seinstijl met `COMBINED_NORMAL_SHUNT` (realistisch remmen) | **Rangeerbeeld** | In `DetermineCombineNormalShuntModeWithLookahead`: als de reservering eindigt in een koppelreservering, roep `SetCombinedNormalShuntModeSignalToShunt` aan en zet `TRSLAI_COMBINED_SHUNT`. Dit hergebruikt alles wat er al is. |
| NewGRF-sein dat de nieuwe functie ondersteunt | **Groen knipperend**, of een eigen beeld van de GRF | Nieuwe bit 25 in variabele 0x18 (`param2`): "aankoppelen toegestaan". De GRF schakelt dit in via een nieuwe feature- of control-vlag, net als bit 24 voor beperkte seinen. De GRF laat het sein knipperen met de bestaande animatieteller (globale variabele 0x0A). |
| Ingebouwde seinen | **Groen knipperend** | `DrawSingleSignal` wisselt op de tick-teller tussen het normale groene sprite en een groen sprite met een "lamp uit"-recolour. Die recolour komt als extra sprite in `tracerestrict.grf` of een kleine nieuwe basis-GRF. |
| NewGRF-sein zonder ondersteuning voor bit 25 | **Geel ("Prepare to stop")** | Alleen bij het tekenen wordt aspect 2 aan de GRF doorgegeven, in plaats van de opgeslagen waarde. Het opgeslagen seinbeeld en de remberekening veranderen niet. Heeft de GRF geen extra aspecten (alleen rood en groen), dan wordt groen getoond. |
| Ingebouwd sein waarbij de speler knipperen heeft uitgezet | **Groen en geel tegelijk** | Groen sprite met een gele recolour-overlay op de lamp. |

**Laten knipperen:** er komt een globale set "knipperende seinen" (tegel + trackdir). Elke ~16 ticks wordt `MarkSingleSignalDirty` aangeroepen voor elk sein in de set, naast `AnimateAnimatedTiles`. Dit raakt alleen het tekenen, dus het veroorzaakt geen desync. Er komt een clientinstelling "knipperende seinen" (aan of uit), onder andere voor lichtgevoeligheid. Staat die uit, dan geldt de groen-geel-variant.

### 6.3 Snelheid

Er komen twee nieuwe instellingen:

| Instelling | Standaard | Bereik | Geldt |
|---|---|---|---|
| `train_coupling_speed` | 40 km/u | 10–80 | vanaf het sein tot de laatste tegel |
| `train_coupling_final_speed` | 15 km/u | 5–30 | op de laatste tegel (de laatste 16 eenheden voor het koppelpunt) |

Beide worden toegepast in `GetCurrentMaxSpeedInfoInternal`, zolang `CouplingApproach` actief is.

### 6.4 Stoppen op het koppelpunt

- **Koppelpunt:** de positie van de achterkant van de partner, gemeten vanaf de perroningang in 1/16 tegel.
- `GetTrainStopLocation` en `PredictStationStoppingLocation` gebruiken `min(normaal stoppunt, koppelpunt)`.
- **Realistisch remmen:** `reservation_end_position` wordt het koppelpunt. Een extra lookahead-snelheidsbeperking zorgt dat de trein op de grens van de laatste tegel ten hoogste 15 km/u rijdt. De bestaande remcurve brengt hem precies op het koppelpunt tot stilstand.
- In `VehicleEnter_Station`: bereikt de trein het koppelpunt (afstand 0, met een marge van 1 eenheid), dan volgt `CoupleTrains`. De bestaande controle op doorschieten wordt in dit geval overgeslagen.

### 6.5 Nooit een crash

- `CheckTrainCollision` (`train_cmd.cpp:5277`) krijgt een uitzondering voor het paar `coupling_target`, in beide richtingen. Dit is een vangnet: normaal stopt de trein op het koppelpunt.
- Raakt de trein de partner toch terwijl `CouplingApproach` actief is, dan wordt er gekoppeld of gestopt. Er volgt **nooit** `TrainsCrashed`.
- Een partner waar een trein met `coupling_target` op afrijdt, blijft **staan**. Ook als zijn wachttijd verloopt, mag hij pas vertrekken als de aanrijding afgebroken is.
- Voor alle andere treinen gelden de normale botsregels.

### 6.6 Afbreken

`coupling_target`, `CouplingApproach` en het seinbit worden gewist, en de reservering wordt vrijgegeven, in deze gevallen:

- de order verandert;
- de speler gebruikt "force proceed";
- de trein keert om;
- de partner verdwijnt;
- de trein gaat naar een depot.

## 7. Laden, lossen, reizigers en opbrengst

### 7.1 Hoe cargodist reizigers routeert

- De linkgraph rekent met **vraag per paar herkomst–bestemming**, zowel symmetrisch als asymmetrisch.
- Het resultaat wordt omgezet in **stromen** (`FlowStat`) per station: per herkomst staat daar welk deel via welk volgend station gaat. Op dat punt valt de eindbestemming weg.
- Een vrachtpakket (`CargoPacket`) kent alleen zijn **herkomststation** (`first_station`) en, bij een overstap, zijn volgende station (`next_hop`).
- Op elk station wordt de volgende halte **geloot** volgens de stromen van dat station voor die herkomst (`FlowStat::GetVia`).
- De bestemmingen in het venster "Wachtende goederen" zijn een **schatting**. Het venster loopt de stromen recursief af (`EstimateDestinations`, `station_gui.cpp:1711`).

### 7.2 Aanpak: vooraf loten bij het instappen

De loting op splitsstation B (stroom op B voor herkomst X) wordt **vervroegd naar het instappen**. Statistisch is dat gelijk aan de normale loting op B. Het verschil is dat de reiziger dan al bij het instappen weet welk stel hij moet nemen.

1. **Elk stel heeft zijn eigen route na het splitsen.**
   - `PrepareUnload` en `LoadUnloadVehicle` bepalen `next_station` per stel, uit de eigen orderlijst en orderindex van dat stel (het record of de leider).
   - Per stel wordt ook het **vervolgstation na het splitsstation** bepaald. Dat is de eerste stop na B in de orders van dat stel.
2. **Instappen op een gedeelde stop vóór B:**
   - Voor een pakket waarvan de volgende halte op het gedeelde traject ligt, wordt vooraf geloot: `FlowStat` op B voor `first_station` → `split_hop`.
   - Het pakket gaat bij voorkeur in een wagon van het stel waarvan het vervolgstation gelijk is aan `split_hop`.
   - Zit dat stel vol, dan gaat het in een ander stel. Het pakket wisselt dan later van stel (stap 3).
   - `split_hop` wordt **niet opgeslagen**, niet in het pakket en niet in de savegame. Het wordt deterministisch berekend:
     - de basis is een hash van de pool-index van het pakket, `first_station` en het splitsstation;
     - die hash wordt omgezet naar een keuze volgens de stromen op B voor `first_station`;
     - bij het instappen en op B komt daaruit dezelfde uitkomst, zolang de stromen gelijk blijven;
     - zo is er geen desync bij het inloggen in multiplayer, en blijft het savegame-formaat van `CargoPacket` ongewijzigd.
   - De uitkomst kan veranderen als de stromen opnieuw berekend zijn, of als het pakket gesplitst of samengevoegd is (nieuwe index). Dan volgt op B een wissel van stel, of een overstap via het station.
   - Reizigers die vóór of op B uitstappen, krijgen geen `split_hop` en stappen in elk stel in.
3. **Wisselen van stel onderweg:**
   - Op elke gedeelde tussenstop, en uiterlijk op B vóór het splitsen, worden pakketten in het "verkeerde" stel direct verplaatst naar het juiste stel als daar ruimte is.
   - Dit gebeurt met een nieuwe cargo-actie, naar het voorbeeld van `CargoShift` en `VehicleCargoReroute`.
   - Er wordt niets betaald. Het aandeel van de aanvoerlijn (`feeder_share`), de reistijd en de herkomst blijven behouden.
4. **Op B:**
   - Bij gekoppelde treinen gebruikt de stap `Stage` dezelfde deterministische keuze `split_hop` in plaats van de gewone willekeurige loting. Daardoor komt de keuze overeen met die bij het instappen. Wie toch in het verkeerde stel zit, wisselt binnen de trein.
   - Daarna wordt gesplitst (§3.3). Elk stel lost en laadt volgens zijn eigen orders.
   - Wie niet kon wisselen omdat het stel vol was, stapt uit op B en neemt het andere stel als daar nog plek is, of een volgende trein. Dat is het normale gedrag bij overstappen.
   - Instappers op B stappen direct in het juiste stel.

Deze aanpak werkt voor alle soorten lading en voor elke verdeling (symmetrisch, asymmetrisch of handmatig), omdat hij alleen de stromen gebruikt. Bij een handmatige verdeling zijn er geen stromen. Dan stapt iedereen in elk stel in.
5. **Linkgraph:** cargodist moet weten dat het passieve stel na B naar D gaat. Zonder dat zijn er op B geen stromen naar D, en werkt het vooraf loten niet.
   - `LinkRefresher` loopt nu over de **hele keten**, met alleen de orders van de leider.
   - Er komt een versie per stel: een bereik van wagons en de orderlijst van dat stel.
   - Daarnaast `LastLoadStationSeparate` voor gekoppelde wagons.
   - Zonder deze aanpassing plant cargodist te weinig capaciteit voor B→D.

### 7.3 Opbrengst en kosten

- Bij aflevering worden de opbrengsten per wagon toegerekend aan het stel waar die wagon bij hoort (het record of de leider). Daarvoor komt een toewijzing per stel in `CargoPayment`.
- De lopende kosten worden per stel geboekt.

## 8. Depot

- **Een depotorder zonder de vlag "Gekoppelde combinatie toegestaan":**
  - Bij het **invoegen of wijzigen** van zo'n order in de lijst van een trein die op dat moment gekoppeld is, toont de GUI een waarschuwing. De order wordt wel opgeslagen.
  - Voert een combinatie zo'n order uit, dan volgt een **nieuwsbericht** "Gekoppelde combinatie rijdt depot binnen". De combinatie gaat wel naar binnen.
  - Gebruikt de speler de knop "naar depot sturen" voor een combinatie, dan volgt eerst een bevestigingsvraag.
- **Met de vlag:** er komt geen melding.
- **In het depot is de combinatie vergrendeld.** Alleen ombouwen (refit) en starten of stoppen mag.
  - Geweigerd: `CmdMoveRailVehicle`, verkopen, autoreplace en autorenew, template-vervanging en een losse wagon omdraaien.
  - Foutmelding: "Gekoppelde combinatie: alleen ombouwen en starten/stoppen mogelijk".
- **Automatisch onderhoud** (onderhoudsinterval, pech) en depotorders met alleen "service" zijn een bezoek "in en uit". Dat mag **zonder melding** en zonder vlag. De combinatie blijft daarbij gekoppeld.
- De melding en de vlag gelden alleen voor bezoeken waarbij de combinatie echt **stopt** in het depot: de order "stop in depot" of de knop "naar depot sturen".

## 9. GUI

- **Orderscherm:**
  - instellen van `Koppelen` of `Splitsen`, het partnerfilter en de leider;
  - de depotvlag;
  - de gespiegelde orders, grijs weergegeven.
- **Treinscherm:**
  - een statusregel, bijvoorbeeld "Gekoppeld aan *Trein 12* tot *Station E*" of "Wacht op koppeling (nog 1:20)";
  - een combinatie-overzicht met beide treinen.
- **Voertuiglijst:** passieve stellen blijven zichtbaar met hun eigen nummer en de status "gekoppeld". Een klik opent de combinatie.
- **Nieuwsberichten:**
  - koppelen mislukt omdat de partner te laat is;
  - koppelen geweigerd: te lang, te veel stellen, of het stel zou nooit meer loskomen;
  - een combinatie rijdt een depot binnen.

## 10. Risico's

| Gebied | Risico | Maatregel |
|---|---|---|
| Gedeelde orderlijst | Veel code loopt over `next_shared` en gaat uit van voorste voertuigen | Het record apart registreren, en alle aanroepers inventariseren |
| Scheduled dispatch | Een slot wordt alleen in `UpdateVehicleTimetable` opgeëist | Die functie expliciet voor het record aanroepen bij gespiegelde stops en bij splitsen |
| Cargo en linkgraph | Laden en de linkgraph werken per keten en met de orders van de leider | Een `next_station` per stel, een `LinkRefresher` per stel, en een nieuwe cargo-actie |
| Seinen | Het seinbeeld wordt ook gebruikt voor remmen en voor het doorgeven aan andere seinen | Een apart bit; het seinbeeld zelf niet veranderen |
| Desync (multiplayer) | Alles moet deterministisch zijn | Knipperen alleen in het tekenen; de records toevoegen aan de desync-checks |
| Savegame | Nieuwe pool en nieuwe velden | `XSLFI_TRAIN_COUPLING`, een chunk `TCPL`, het seinbit in `m3`, en controles in `AfterLoadVehicles` |
| Aannames over "voorste voertuig" | Bijvoorbeeld `vehicle_sl.cpp:622`, dat het unitnummer van niet-voorste voertuigen op 0 zet | Het unitnummer staat in het record |

## 11. Fasering en tests

1. **Fase 1 – kern:** het record, `CoupleTrains` en `SplitTrain` (hoogstens twee treinen). Een debugcommando om handmatig te koppelen en te splitsen. Savegame-ondersteuning. Het depot vergrendelen.
2. **Fase 2 – aanrijden:** de gedeeltelijke perronreservering, de snelheden van 40 en 15 km/u, het koppelpunt, de uitzondering voor botsingen, realistisch remmen en het rangeerbeeld.
3. **Fase 3 – orders:** de vlaggen, de depotvlag en de meldingen, `CheckOrders`, de conditionele variabele, de GUI en de JSON-export van orders.
4. **Fase 4 – dienstregeling en cargo:** gespiegelde orders, de tick en vertraging van het record, scheduled dispatch, vertrekborden, `next_station` per stel, overstappen in de trein, de linkgraph en de opbrengst per stel.
5. **Fase 5 – seinbeeld:** knipperende seinen, de recolour-sprites, de groen-geel-variant en de NewGRF-bit 25 met documentatie in `newgrf-additions.html`.

**Testscenario's:**

- Twee orderlijsten: koppelen op B, splitsen op E, en beide gaan verder volgens hun eigen dienstregeling.
- Eén gedeelde orderlijst: A→B met twee stellen, B→C met één stel.
- A–B gedeeld, daarna B–C en B–D, met cargodist: reizigers voor C en D eindigen na B in het juiste stel.
- De partner is te laat: beide treinen rijden alleen verder.
- Kop tegen kop koppelen en splitsen in een kopstation.
- Een trein van 2-2 koppelt met een trein van 3 tot 2-2-3. Bij het splitsen komen precies 2-2 en 3 terug.
- Een tweede `Koppelen`-order invoegen bij een gekoppelde trein wordt geweigerd. Een combinatie bij een `Koppelen`-order stopt gewoon en geeft een nieuwsbericht.
- Realistisch remmen met snelle treinen: niet doorschieten, geen crash, en ten hoogste 15 km/u op de laatste tegel.
- Depot met en zonder de vlag, en controleren dat de vergrendeling werkt.
- Opslaan en laden midden in een aanrijding en midden in een gekoppeld traject.
- Multiplayer: controle op desync.

## 12. Open vragen

Er zijn geen open vragen meer.

- **V1 (opgelost):** een NewGRF-sein zonder ondersteuning toont geel (§6.2).
- **V2 (opgelost):** service "in en uit" mag zonder melding. Stoppen in het depot valt onder de regel met melding en vlag (§8).
