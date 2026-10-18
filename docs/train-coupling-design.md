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

Geïmplementeerd in fase 1 (`train_coupling.cpp`):

- **Welke treinen passen aan elkaar:** de keten van de combinatie bestaat uit een eerste deel en een tweede deel. Twee treinen kunnen koppelen als het laatste voertuig van de ene trein en het eerste voertuig van de andere precies zo staan als twee opeenvolgende voertuigen in één keten. Dat betekent: dezelfde richting, op één lijn, en de afstand tussen hun middelpunten zoals `Train::CalcNextVehicleOffset` die verwacht.
- **Zelfde richting** (de aankomende trein rijdt tegen de achterkant van de wachtende): de keten wordt `wachtende + aankomende`. Er hoeft geen voertuig om.
- **Kop tegen kop of staart tegen staart:** dit wordt in fase 1 nog geweigerd. In fase 2 wordt de aankomende trein in dat geval eerst omgedraaid. Dat mag, want hij staat dan stil.
- Een combinatie krijgt de rijrichting (`DrivingBackwards`) van het eerste deel.

### 4.2 Identiteit van de treinen

- **Waar de identiteit staat:** onder "identiteit" valt alles wat bij de dienst hoort: orders, orderindexen, dienstregeling, unitnummer, naam, groep, winst, slots en de laadstatus. De identiteit van het passieve stel **blijft op zijn eigen kopvoertuig staan**. Dat voertuig verliest alleen de status "voorste voertuig" (`GVSF_FRONT`) en krijgt de vlag `CoupledPassiveHead`.
  - Voordeel: het blijft gewoon lid van zijn gedeelde orderlijst. Wordt de orderlijst bewerkt terwijl de trein gekoppeld rijdt, dan worden zijn orderindexen automatisch bijgewerkt.
- **Leider fysiek achteraan:** het voorste voertuig van de keten draagt altijd de identiteit van de leider. Zit de leider fysiek in het tweede deel, dan worden de identiteiten van de twee kopvoertuigen verwisseld (`SwapTrainIdentities`). Bij het splitsen worden ze teruggewisseld. Fysieke eigenschappen blijven bij het voertuig, zoals `DrivingBackwards`, prototype en laadstation per wagon.
- **NewGRF-koppelcallback:** de callback `CBID_TRAIN_ALLOW_WAGON_ATTACH` wordt bij koppelen bewust **niet** gecontroleerd. Die callback is bedoeld voor het samenstellen van treinen in het depot. Hij zou ook het koppelen van twee verschillende treinstellen weigeren, terwijl dat in de werkelijkheid gewoon kan. Alleen de maximale treinlengte wordt gecontroleerd.

### 4.3 `CoupleTrains(waiting, arriving, arriving_leads)`

1. Geef de padreserveringen van beide treinen vrij.
2. Het passieve stel verlaat het station: `LeaveStation`, gaat naar de volgende order, en de huidige order wordt leeggemaakt.
3. Het laadstation wordt per wagon vastgelegd (`LastLoadStationSeparate`).
4. Het passieve stel telt niet meer mee in de groepsstatistiek. Zijn groep wordt bewaard in het record.
5. Zo nodig worden de identiteiten verwisseld. Daarna worden de ketens gekoppeld: `SetNext` en `NormaliseSubtypes`.
6. Daarna volgen `UpdateTrainGroupID`, `ConsistChanged(CCF_TRACK)`, `CheckBreakdownFlags`, `InvalidateVehicleTickCaches` en `ReserveTrackUnderConsist`.

### 4.4 `SplitCombination(head)`

1. De keten wordt geknipt vóór de passieve kop. Die krijgt weer `GVSF_FRONT`.
2. Zijn groep wordt hersteld. Waren de identiteiten verwisseld, dan worden ze teruggewisseld.
3. Daarna: groepsstatistiek, `ConsistChanged`, en de reserveringen voor beide treinen.
4. De passieve trein krijgt een lege huidige order. Hoe hij daarna verdergaat (de `Splitsen`-order, laden), komt in fase 3.

## 5. Datamodel

### 5.1 Koppelrecord

Een eenvoudige tabel `_train_couplings`, met als sleutel het kopvoertuig van de combinatie. Er is geen aparte pool, omdat de identiteit van het passieve stel op het voertuig zelf blijft staan.

```cpp
struct TrainCoupling {
	VehicleID head;             ///< Kop van de combinatie, draagt de identiteit van de leider
	VehicleID passive_head;     ///< Kop van het tweede deel, draagt de identiteit van het passieve stel
	GroupID passive_group;      ///< Groep van het passieve stel
	bool identities_swapped;    ///< Identiteiten van de twee koppen zijn verwisseld bij het koppelen
};
```

- **Savegame:** chunk `TCPL`, feature `XSLFI_TRAIN_COUPLING`. De vlaggen `CoupledCombination` en `CoupledPassiveHead` gaan mee in de bestaande 32-bit treinvlaggen. Na het laden controleert `ValidateTrainCouplings` de consistentie.

### 5.2 Koppeling met de rest van het spel

- **Gedeelde orderlijst:** het passieve kopvoertuig blijft in `next_shared`, ook al is het geen voorste voertuig meer. Code die over gedeelde voertuigen loopt, moet hiermee overweg kunnen. Het gaat om afstand houden tussen treinen, vertrekborden, de lijst met gedeelde voertuigen en scheduled dispatch. Dat wordt nagelopen in fase 4, en blijft **het grootste risico** (§10).
- **Slots en tellers:** een stel blijft lid van zijn slots, op basis van het `VehicleID` van zijn identiteit. Bij een identiteitswissel worden de slots meegewisseld (`TraceRestrictSwapVehicleOccupantInAllSlots`).
- **Tick:** in fase 4 gaat de leider bij elke tick ook `current_order_time` van het passieve stel verhogen.
- **Op `Train`:**
  - vlag `VehicleRailFlag::CoupledCombination` (kop van de combinatie);
  - vlag `VehicleRailFlag::CoupledPassiveHead` (kop van het passieve stel);
  - in fase 2: `coupling_target` (`VehicleID`) en vlag `CouplingApproach`.

### 5.3 Hoogstens twee treinen per combinatie

**Besluit:** een combinatie bestaat voorlopig uit hoogstens **twee treinen**: één leider en één passieve trein.

- **Trein = dienst.** Een "trein" in dit ontwerp is een `Train` met een eigen orderlijst. Hoe die fysiek is opgebouwd, maakt niet uit.
  - Voorbeeld: dienst 1 bestaat uit twee stellen van twee wagons, die in het depot als één trein zijn samengesteld. Dienst 2 is één stel van drie wagons. Samen vormen ze **2-2-3**.
  - Bij het splitsen komen precies **2-2** en **3** terug.
  - Grenzen tussen fysieke stellen binnen één dienst worden niet bijgehouden en nooit gesplitst.
- **Een trein die al gekoppeld is, kan niet nog een keer koppelen.** Dat wordt op twee niveaus afgedwongen. De orderlijst blijft altijd vrij aan te passen, ook terwijl een trein gekoppeld rijdt.
  1. **Statische controle (`PreInsertOrderCheck` / `CmdModifyOrder` / `CheckOrders`):** tussen een `Koppelen`-order en de eerstvolgende `Splitsen`-order in dezelfde lijst mag geen tweede `Koppelen`-order staan. Zo'n order wordt geweigerd bij het invoegen of wijzigen, met de foutmelding "Eerst een Splitsen-order nodig voor een volgende Koppelen-order". Een bestaande lijst met dit probleem geeft een waarschuwing.
  2. **Tijdens het spel:** komt een combinatie toch bij een `Koppelen`-order, bijvoorbeeld omdat de leider geen `Splitsen`-order heeft, dan geldt die order als **gewone stop zonder koppelen**, met een nieuwsbericht. Een combinatie is ook nooit een geschikte partner voor een aankomende trein (§3.2).
- **Datamodel:** het record (§5.1) blijft per trein opgezet. Een latere uitbreiding naar meer treinen kan dus zonder dat de savegame verandert. Het veld `position` is altijd 0.

## 6. Bezet perron oprijden (PBS en realistisch remmen)

### 6.1 Beslissing bij het sein

Geïmplementeerd in fase 2. Zolang er nog geen ordervlaggen zijn (fase 3), wordt het koppeldoel gezet met het commando `SetTrainCouplingTarget` (console: `couple_approach`). Dat vult `Train::coupling_target` en de vlag `CouplingArrivingLeads`.

- **Geldig doel** (`GetCouplingApproachTarget`): het doel staat stil en volledig in een perron. Het is van dezelfde eigenaar, niet gekoppeld, en de oprijdende trein heeft een order naar dat station.
- **YAPF:**
  - Alleen het perron van het doel geldt als bestemming.
  - De eerste perrontegel op dat perron telt als "veilige" eindpositie.
  - De controle "eindpositie moet vrij zijn" wordt daar overgeslagen.
- **Gedeeltelijke perronreservering:** YAPF reserveert een perron normaal vanaf het verre einde, achteruit. Bij koppelen wordt alleen het stuk van de perroningang tot de eerste tegel waarop het doel staat gereserveerd. Bij het terugdraaien worden de tegels van het doel overgeslagen. Daarbij wordt `Train::coupling_dir` (de rijrichting het perron in) vastgelegd.
- **`ExtendTrainReservation`:** stopt normaal bij een bezette perroningang. Voor het koppelperron laat het de pathfinder het overnemen.
- **Toestemming pas bij lage snelheid:** de koppelreservering wordt alleen toegekend als de trein ten hoogste `train_coupling_speed` rijdt. Een snellere trein ziet het sein als rood en remt af. Zodra hij langzaam genoeg is, krijgt hij toestemming. Dit werkt met beide remmodellen.

### 6.2 Seinbeeld

Dit volgt de wens van de opdrachtgever. Per sein is één extra bit nodig: **"aankoppelen toegestaan"**.

- **Opslag:** `m3` bits 0–3 van rail-tegels lijken vrij, één bit per seinpositie, net als de "sein aanwezig"-bits 4–7. Dit nog verifiëren met `docs/landscape_grid.html`.
- **Bit zetten:** bij het maken van de koppelreservering (§6.1).
- **Bit wissen:** zodra het sein weer rood wordt. Dat gebeurt op dezelfde plekken als waar het seinbeeld nu op 0 wordt gezet, zoals `pbs.cpp:95`.
- Het seinbeeld (de waarde van het aspect) zelf wordt **niet** hergebruikt. Remberekening en het doorgeven van seinbeelden lezen die waarde, en die mogen niet veranderen.

| Situatie | Wat het sein toont | Hoe |
|---|---|---|
| Seinstijl met `COMBINED_NORMAL_SHUNT` (realistisch remmen) | **Rangeerbeeld** | **Geïmplementeerd in fase 2:** in `DetermineCombineNormalShuntModeWithLookahead` krijgt het laatste sein vóór het koppelperron het rangeerbeeld. Een routeringsbeperking die expliciet "normaal" afdwingt, gaat voor. Nog niet getest met een NewGRF die een rangeerbeeld heeft. |
| NewGRF-sein dat de nieuwe functie ondersteunt | **Groen knipperend**, of een eigen beeld van de GRF | Nieuwe bit 25 in variabele 0x18 (`param2`): "aankoppelen toegestaan". De GRF schakelt dit in via een nieuwe feature- of control-vlag, net als bit 24 voor beperkte seinen. De GRF laat het sein knipperen met de bestaande animatieteller (globale variabele 0x0A). |
| Ingebouwde seinen | **Groen knipperend** | `DrawSingleSignal` wisselt op de tick-teller tussen het normale groene sprite en een groen sprite met een "lamp uit"-recolour. Die recolour komt als extra sprite in `tracerestrict.grf` of een kleine nieuwe basis-GRF. |
| NewGRF-sein zonder ondersteuning voor bit 25 | **Geel ("Prepare to stop")** | Alleen bij het tekenen wordt aspect 2 aan de GRF doorgegeven, in plaats van de opgeslagen waarde. Het opgeslagen seinbeeld en de remberekening veranderen niet. Heeft de GRF geen extra aspecten (alleen rood en groen), dan wordt groen getoond. |
| Ingebouwd sein waarbij de speler knipperen heeft uitgezet | **Groen en geel tegelijk** | Groen sprite met een gele recolour-overlay op de lamp. |

**Laten knipperen:** er komt een globale set "knipperende seinen" (tegel + trackdir). Elke ~16 ticks wordt `MarkSingleSignalDirty` aangeroepen voor elk sein in de set, naast `AnimateAnimatedTiles`. Dit raakt alleen het tekenen, dus het veroorzaakt geen desync. Er komt een clientinstelling "knipperende seinen" (aan of uit), onder andere voor lichtgevoeligheid. Staat die uit, dan geldt de groen-geel-variant.

### 6.3 Snelheid

Er zijn twee nieuwe instellingen, te vinden onder "Beperkingen":

| Instelling | Standaard | Bereik | Geldt |
|---|---|---|---|
| `train_coupling_speed` | 40 km/u | 10–80 | vanaf de koppelreservering bij het sein tot de laatste tegel |
| `train_coupling_final_speed` | 15 km/u | 5–30 | op de laatste tegel (de laatste 16 eenheden voor het koppelpunt) |

- **Strikte limiet:** beide snelheden zijn een harde limiet in `GetCurrentMaxSpeedInfoInternal`. De vlag `CouplingSpeedLimit` wordt gezet bij het passeren van het padsein.
- **Realistisch remmen:** `ApplyCouplingLookAhead` zorgt dat de trein het perron met ten hoogste 40 km/u oprijdt, de laatste tegel met ten hoogste 15 km/u nadert, en precies op het koppelpunt stilstaat.
- **Gemeten in de test:**
  - Oorspronkelijk remmodel: de trein vertraagt voor het rode sein en passeert het met 37–40 km/u. Op het perron rijdt hij 40 km/u en op de laatste tegel 15 km/u.
  - Realistisch remmen: de snelheid zakt geleidelijk van 70 naar 40 km/u vóór het sein. Op de laatste tegel loopt hij geleidelijk terug van 40 naar 15 km/u.

### 6.4 Stoppen op het koppelpunt

- **Koppelpunt** (`GetCouplingStopLocation`): de plaats waar het midden van het voorste voertuig moet staan, zodat het precies als volgend voertuig in een keten achter het dichtstbijzijnde uiteinde van het doel staat. Het wordt gemeten vanaf het begin van het perron, net als bij `GetTrainStopLocation`.
- `GetTrainStopLocation` gebruikt `min(normaal stoppunt, koppelpunt)`.
- **Bij het stoppen** (`TrainEnterStation`) wordt de trein aangemeld voor koppelen. Het koppelen zelf gebeurt ná de tick van alle treinen (`ProcessPendingTrainCouplings`). Halverwege de bewegingslus van een trein de keten aanpassen is namelijk onveilig.
- **Kop tegen kop of staart tegen staart:** de aankomende trein wordt eerst omgedraaid (`FlipTrainForCoupling`), net zoals OpenTTD een trein omdraait. Dit is nog niet getest.
- **Mislukt het koppelen,** dan wordt het koppeldoel gewist, zodat het doel niet eindeloos blijft wachten.

### 6.5 Nooit een crash

- `CheckTrainCollision` heeft een uitzondering voor het paar oprijdende trein en doel, in beide richtingen (`IsCouplingApproachCollision`).
- Een trein waar een andere trein op afrijdt om te koppelen, blijft laden en vertrekt niet (`IsTrainBeingApproachedForCoupling` in `HandleLoading`).
- Voor alle andere treinen gelden de normale botsregels.

### 6.6 Afbreken

- Het koppeldoel wordt gewist bij koppelen, bij mislukt koppelen, en als het doel verdwijnt.
- Afbreken bij een orderwijziging, bij "force proceed", bij omkeren en bij een depotbezoek volgt in fase 3, samen met de ordervlaggen.

### 6.8 Een perron gedeeld door twee treinen

Bij koppelen (en direct na splitsen) staan twee treinen in hetzelfde perron. Op een aantal plekken ging OpenTTD ervan uit dat een perron altijd in zijn geheel door één trein is gereserveerd. Die plekken zijn aangepast:

- **`FollowReservation`:** het volgen van een reservering stopt bij de eerste perrontegel waarop een andere trein staat of die niet gereserveerd is. Anders begon YAPF bij de andere trein.
- **`IsWaitingPositionFree`:** de tegel ná een sein telt als vrij als daar het koppelperron begint. Dit speelt als het sein direct voor het perron staat.
- **`IsSafeWaitingPosition`:** perrontegels van het koppelperron gelden als veilige eindpositie. Zo probeert de trein niet voorbij het koppelpunt verder te reserveren. Dezelfde regel geldt voor de long-reserve-logica.
- **`SetRailStationPlatformReservation(false)`:** tegels waarop nog een andere trein staat, blijven gereserveerd.

Daarnaast:

- **Stoppunt:** bij koppelen geldt altijd het koppelpunt als stoppunt, ook als de order een andere stoplocatie heeft (bijvoorbeeld "dichtbij"). Bij realistisch remmen vervangt de koppelberekening de normale remberekening voor dat station.
- **Na het splitsen:** het afgesplitste stel vertrekt via de normale vertrekprocedure. Zijn order schuift voorbij het splitsstation, en het mag niet in zijn partner rijden zolang die direct vóór hem staat. Dat wordt afgedwongen in de treinlogica, vóór elke beweging.
- **Getest met de savegame van de opdrachtgever** (T-lay-out, treinstellen met cabines aan beide kanten, realistisch remmen): koppelen, splitsen, beide stellen hun eigen kant op, opnieuw koppelen bij terugkeer, en samen terug.

### 6.7 Orderlijsten van passieve stellen

Op een paar plekken doorloopt de code orderlijsten via "voorste voertuigen die eerste in hun gedeelde lijst zijn". Daar viel de lijst van een gekoppeld passief stel buiten. Het gaat om de referentietelling van orderbestemmingen en om het verwijderen van orders als een station of depot verdwijnt of infrastructuur-delen stopt. Deze plekken gebruiken nu `GetFrontAndCoupledPassiveVehicles`.

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
- Een tweede `Koppelen`-order vóór een `Splitsen`-order wordt geweigerd. De orderlijst van een gekoppelde trein blijft wel vrij te bewerken. Een combinatie bij een `Koppelen`-order stopt gewoon en geeft een nieuwsbericht.
- Realistisch remmen met snelle treinen: niet doorschieten, geen crash, en ten hoogste 15 km/u op de laatste tegel.
- Depot met en zonder de vlag, en controleren dat de vergrendeling werkt.
- Opslaan en laden midden in een aanrijding en midden in een gekoppeld traject.
- Multiplayer: controle op desync.

## 12. Open vragen

Er zijn geen open vragen meer.

- **V1 (opgelost):** een NewGRF-sein zonder ondersteuning toont geel (§6.2).
- **V2 (opgelost):** service "in en uit" mag zonder melding. Stoppen in het depot valt onder de regel met melding en vlag (§8).
