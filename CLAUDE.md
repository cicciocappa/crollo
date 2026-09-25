# CROLLO: note per riprendere il lavoro

Gioco d'assedio fisico in 3D (C++17, Box3D + raylib 5.5, desktop e WebAssembly). Il README spiega gioco,
compilazione, strumenti di verifica e architettura: leggilo per primo. Questo file raccoglie solo ciò che serve a
riprendere lo sviluppo e che non si ricava dal codice.

## Con chi e come si lavora
- Si parla in **italiano**, anche nei commit (messaggi in italiano, stile "Sessione N: ..."). Testi del gioco in italiano.
- Il lavoro procede per **sessioni** della roadmap in `docs/sviluppo.md` ("Crollo — guida allo sviluppo futuro"):
  tabella "Sessione / Obiettivo / Perché in quest'ordine". A fine sessione la riga va aggiornata con
  "Fatto (commit xxxxx): ...". Fino alla sessione 4 era un Claude Doc di un altro account, ora non più usato.
- Il codice imita lo stile esistente: tab, graffe su riga propria, commenti in inglese, sobri.
- Repository pubblico: https://github.com/cicciocappa/crollo (licenza MIT). Committa e pusha a fine sessione.
- La versione web pubblicata come Artifact (https://claude.ai/artifact/E19DY3LRznvgcrRwYuZpJY, da settembre 2026; il
  vecchio HNRpbEuELhrBG6CQuAioQ5 era di un altro account) si aggiorna solo quando l'utente lo chiede: pagina
  `web/crollo.html` + `build-web/crollo.js` + `build-web/crollo.wasm` (pubblicati con `files`, il wasm come
  `application/wasm`); da un'altra conversazione passa l'URL come `url`, o nasce un Artifact nuovo.

## Stato (fine sessione 8, settembre 2026)
- Fatte le sessioni 1-4: scudi di cristallo a tempo; gomma, sacchi, Vortice, bomba adesiva; campagne, biomi, mappa
  dei regni, storie, salvataggi per id; Picchi Gelati a 8 livelli.
- 60 livelli. Campagne: Prati Alti 9, Valle dei Mulini 10, Picchi Gelati 11, Dune Sospese 10, Arcipelago delle Tempeste 10,
  Fucina del Vulcano 10. Tutte e sei le campagne sono giocabili.
- Sessione 4b fatta: riscontro dei test (l'utente prova i livelli di persona), modalità trucchi (F9 o `--cheat`),
  esplosioni fermate dai corpi statici, sacchi che assorbono, vento variabile, macigno che sfonda, 3 livelli nuovi.
- Sessione 5 fatta: Valle dei Mulini a 8 livelli, barriera magica e sfere magiche.
- Sessione 5c fatta: alberi solidi che solo la palla incatenata taglia (prima la catena era un doppione della palla),
  livello Il Boschetto nei Prati Alti; la telecamera che segue la catena oscilla sempre meno.
- Sessione 6 fatta: Dune Sospese a 8 livelli, palme e cactus, tappeti volanti e ascensori (`Mover`), vetro, IA con anticipo.
- Sessione 6b fatta (richiesta dell'utente: grappolo e Vortice non servivano mai): Il Bazar e Il Frutteto (grappolo
  indispensabile), Le Teche e La Cupola Stregata (Vortice indispensabile); Dune e Valle a 10 livelli.
- Sessione 6d fatta: re sparsi estesi a Prati Alti, Valle dei Mulini e Picchi Gelati.
- Sessione 6c fatta (riscontro sulle Dune): granelli di sabbia al posto delle foglie, vento fisso in tutte le Dune (niente
  più vento variabile nemmeno in Tappeti e Tempesta), re sparsi a caso a ogni tentativo e munizioni più generose.
- Sessione 7 fatta: Arcipelago delle Tempeste a 10 livelli (Il Faro ... La Rocca di Re Fulmine): gomma che scorre e
  gira, ventole, soffioni, isole che fluttuano, giostra; mira assistita che segue rimbalzi e correnti; lampi e tuoni.
- Sessione 8 fatta: Fucina del Vulcano a 10 livelli con le idee dell'utente (leva, quintana, guinzaglio, carrello) più
  bersagli d'ottone che sganciano pesi, scudi orbitanti e il trabocchetto Tre Corde. L'arpione è rimandato.
- Sessione 8b fatta (riscontro sulla Fucina: colpi tesi troppo facili con la mira assistita): maglio, paratie, ruote a
  pale, bersagli su rotaia e bersagli dietro parapetti.
- Prossima: sessione 9 (campagne a 10 livelli, livelli bonus; lì anche il
  taglio delle munizioni in eccesso). Poi versione mobile (10); multiplayer alla fine (11+).
- Sessioni 6-8: Dune Sospese (deserto con cactus e palme, bersagli mobili, barriera magica con sfere magiche), Arcipelago,
  Fucina. Ogni campagna fa debuttare una meccanica, ma le meccaniche si usano in tutte. Dettagli in `docs/sviluppo.md`.
- Decisioni già prese dall'utente: 6 campagne da **almeno** 8 livelli (più ce ne sono meglio è, poi si punta a 10); Duello prima solo scontro; il suo server può far girare
  Node.js (servirà per l'online a turni, più avanti).

## Verifica: da rifare dopo ogni modifica al gameplay
```bash
./build/crollo --autotest 12            # tutti i livelli vincibili (oggi 60/60); --autotest 12 N per il solo livello N
./build/crollo --scan-shots N           # "!!" = un colpo solo abbatte tutti i re (voluto solo nei livelli 7 e 18; oggi lo
                                        # scanner trova solo il 7, ma la Valanga si vince comunque con un colpo)
./build/crollo --autotest-challenge     # 30/30
./build/crollo --test-shields           # 16/16
./build/crollo --test-ammo              # nessun "FALLITO"
./build/crollo --test-campaigns         # "tutto ok"
./build/crollo --shot <modo> <livello> <frame> out.png   # guarda sempre il risultato, livello 0-based
```
`CROLLO_DEBUG=1` stampa perché cadono i re e l'esito di ogni tiro dello scanner; `CROLLO_BIOME=0..5` forza un bioma.
Con `--shot` metti `--debug` **dopo** gli altri argomenti (prima fa partire il gioco normale, che non termina).

## Cose imparate (non ovvie dal codice)
- **Aggiungere un livello**: funzione `LevelXX(Builder&)` + riga in `s_levels` con un **id stabile** (è la chiave del
  salvataggio: non cambiarlo mai) + id nella lista della campagna in `BuildCampaigns()`. L'ordine di `s_levels` non va
  cambiato (serve alla migrazione dei vecchi salvataggi per indice); l'ordine di gioco lo decide la campagna.
- Il sottotitolo del livello è la **battuta del re** della campagna; `hint` è il consiglio al giocatore.
- Se un re si abbatte **indirettamente** (pietra, colonna, diga...), aggiungi `b.AimHint(re, oggetto, offset)`,
  altrimenti l'IA dell'autotest mira al re e il livello risulta "non vincibile".
- **Box3D**: le soglie di forza dei giunti (`forceThreshold`) scattano solo se il carico passa davvero dal giunto. Una
  paratia saldata e spinta contro ciò che trattiene non si rompe mai: per questo la diga della Valanga è di ghiaccio.
- Il ghiaccio (`Mat::Ice`, `Kind::Block`) si frantuma sopra 3,4 m/s d'urto, ma non per le proprie schegge.
- `Entity::lethal` (pietre da curling, palle di neve, tetti di neve) abbatte il re che colpisce sopra 3 m/s.
- Bombe: occhio alle esplosioni che raggiungono più gruppi di re: tieni i gruppi a più di ~5 m, o togli la bomba.
- Le esplosioni non passano i corpi **statici** (roccia, `Ledge`, gomma, cristallo acceso): `Game::Explode` rimette
  la velocità di prima ai corpi coperti. Muri di `Ledge` = barriere indistruttibili (casamatta della Polveriera).
- I sacchi di sabbia "inghiottono" i colpi: la palla perde l'85% della velocità, il sacco il 75% della spinta; il
  macigno passa a metà. La Polveriera si vince con un colpo solo nella feritoia, ma lo scanner non lo trova.
- `b.AimHint( re, via, offset, lob )`: con `lob` > 0 l'IA prova solo archi alti (velocità orizzontale fino a `lob`),
  come serve per i tiri di sponda sulla gomma inclinata di Sponde di Gomma.
- `b.ShiftingWind( forza )`: il vento cambia direzione e forza dopo ogni colpo (Cittadella; servirà per le Dune).
- **Neve Fresca** è un trabocchetto voluto: stessa scena della Valanga, ma le "palle di neve" sono `Puffball` innocue
  e un argine ferma la palla che torna giù dalla diga; il test delle munizioni controlla 3 re contro 0.
- **Barriera magica** (`b.MagicBarrier`, categoria `CatBarrier`): ferma palle, blocchi ed esplosioni; le **sfere magiche**
  (`b.MagicOrb`, letali) la attraversano perché la loro maschera la esclude. Lo shader la disegna a reticolo con buchi
  (`discard`), per vedere il re dietro. La sfera va sulla linea cannone-re e `AimHint` sulla sfera; più è vicina alla
  barriera, più il colpo è tollerante (Sfere Magiche ±0,2 m, Sponda Magica ±0,1 m).
- Il vento variabile va nei livelli a tiro diretto, non in quelli a pallonetto (Due Mulini era troppo difficile).
- Sotto un tetto il re vuole almeno 1,5 m liberi sopra i piedi (corona compresa), o resta incastrato.
- Un regno in cui il giocatore ha già stelle resta aperto anche se il precedente cresce di livelli (e di stelle).
- Il **macigno** è l'unico colpo che sfonda `b.Reinforced(...)` (muratura cerchiata di ferro, statica: ferma anche le
  esplosioni) e che spezza le pale dei mulini; attraversa e prosegue al 70% della velocità. L'IA sceglie da sola il
  macigno contro il ferro e prevede dove saranno gli scudi mobili (`Slider`) quando passa il colpo.
- Nei test (headless) `FxRng` riparte dallo stesso seme a ogni livello: un livello dà lo stesso esito da solo
  (`--autotest 12 N`) o nella serie completa. L'IA sceglie il re a caso con `FxRng`: i boss (Reggia) sono al limite.
- Stabilità: il livello deve stare in piedi da solo (l'autotest controlla i primi 5 s); lascialo anche 15 s con `--shot`.
- **Alberi**: tutti quelli del Builder (`Tree`, `Trees`, isola del cannone) sono corpi statici (`Entity::tree` > 0): fermano
  palle, macigni ed esplosioni; solo la catena li taglia (`Game::FellTree`: ceppo statico + albero dinamico, letale, che
  per 0,5 s lascia passare i colpi). `Builder::Tree` non crea l'albero se toccherebbe blocchi o re (restituisce nullptr:
  controllalo prima di `AimHint`). Palme e cactus delle Dune (`TreeKind`) funzionano allo stesso modo.
- Riparo per un re a terra (`GroveKing` nel Boschetto): pino davanti a 1,85 m (i rami bassi sporgono 1,4 m a 0,9 m
  d'altezza), chioma di quercia sopra la testa contro i pallonetti, catasta sotto i rami. Contro la palla regge in una
  griglia di 33.000 tiri; lo 0,3% abbatte un re *diverso* di rimbalzo, rotolando sull'isola. Provato dall'utente: i re
  tagliato il pino restano appoggiati al ceppo e si finiscono con una palla; gli va bene così.
- **Mover** (tappeti, ascensori, vetro mobile): corpi cinematici guidati da `DriveMovers` con `b3Body_SetTargetTransform`,
  anche nei 30 passi di assestamento di `LoadLevel` (altrimenti al primo passo saltano e travolgono il re). Il re che ci
  sta sopra parte con la loro velocità e cade solo sotto il punto più basso della corsa. Accelerazione di punta
  6·distanza/andata² sotto ~3,5 m/s², o il re si ribalta alle curve (l'autotest ora controlla 20 s nei livelli con Mover).
- IA e bersagli mobili: mira dove sarà il re, ferma il controllo della traiettoria al punto d'incontro, usa solo tiri tesi
  (≥ 16 m/s in orizzontale) e aspetta il varco. Il controllo della traiettoria guarda anche il fondo della palla (e i
  fianchi, per i bersagli mobili): prima i pallonetti sfioravano i bordi. La catena parte al 95% della velocità: l'IA ora
  ne tiene conto (prima i tiri di catena arrivavano corti).
- **Vetro** (`Mat::Glass`): statico, fermo come la roccia per colpi ed esplosioni; disegnato dopo gli opachi, con fusione
  alfa e senza scrivere la profondità. Una bomba che esplode *sopra* il bordo del vetro raggiunge i re dietro.
- Palme e cactus: `Biome::desert`; il cactus tagliato che cade contro la palma dietro il re rotola di lato: nell'Oasi
  spesso serve un secondo colpo (voluto: più munizioni e par 5). Gli alberi tagliati cadono lontano dal cannone.
- **Grappolo indispensabile**: re a coppie su colonne fisse a ~1,6 m, meno colpi che re; l'IA (autotest e scanner) apre il
  grappolo a 6 m dal punto mirato (`ClusterSplitDue`) e con `AimHint` fra i due re mira al centro della coppia.
- **Vortice indispensabile**: `Implode` non ha la protezione di `Explode`, quindi risucchia *attraverso* vetro, roccia e
  barriera magica. Re chiusi dentro (tetto compreso) si prendono solo così; l'IA sceglie il Vortice se il bersaglio del
  suggerimento è di vetro o di barriera. Tieni le teche a più di ~6 m, o un Vortice solo le svuota tutte.
- **Re sparsi** (`b.Scatter(p, rx, rz)`): il seme della disposizione cambia a ogni tentativo (`Game::m_layoutSeed`, 0 nei
  test e nelle demo = disposizione disegnata). Sposta insieme il re e ciò che lo regge. L'autotest prova ogni livello
  sparso anche in 5 disposizioni (`CROLLO_DEBUG=1` stampa dove stanno i re). Idea dell'utente: così il pallonetto si
  cerca sempre un po' per tentativi e le munizioni possono essere generose (una palla in più a ciascuno). Oggi in 46
  livelli su 60; fissi solo quelli a colpo preciso (curling, Valanga, Neve Fresca, Crepacci, Polveriera, re centrale
  delle Sponde e del Granaio, torri davanti al pendolo) e i re presi di sponda. Sulle isole piccole tieni il perimetro
  stretto: un re spostato può finire dietro un albero dell'anello di `Trees` (Mongolfiere).
- Vento delle Dune: fisso, verso +x (a sinistra dal cannone); i granelli (`PType::Grain`) seguono il vento vero.
- IA: il fondo della palla si controlla perpendicolare alla traiettoria (sui pallonetti ripidi tocca lo spigolo con la
  parte davanti); per i bersagli mobili conta solo colpire il re, non "qualcosa entro 1,2 m" (era il muro del Montacarichi).
- `BackTrees(b, centro, raggio)`: alberi solo dietro e ai lati, quando quelli a caso di `Trees` finirebbero sulla linea di tiro.
- Web: `EXPORTED_RUNTIME_METHODS=HEAPF32`; nei test di input via browser tieni premuti tasti e click ~120 ms.
- **Previsione del volo** (`Game::PredictArc`): gravità, vento, correnti e rimbalzi sulla gomma, anche mobile. Box3D ferma
  la palla sul muro nei sub-step e aggiunge il rimbalzo solo a fine passo: la previsione fa lo stesso (scarto < 20 cm,
  controllato da `--test-ammo`). Il rimbalzo sulla gomma non conta come impatto (`hasHit`): vento e correnti continuano.
- **Tiri di sponda**: `b.BankHint(re)` fa cercare all'IA (`FireBank`) archi puntati su una griglia delle facce di gomma
  entro 13 m dal re, poi affina yaw, alzo e potenza. La faccia va giudicata rivolta al cannone *all'arrivo* della palla,
  non allo sparo: con la pala che gira in un solo verso una garitta restava sempre esclusa. Cerca ogni 4 frame (5-25 ms a
  ricerca); nella demo del titolo non cerca, per non far scattare il browser.
- Garitte e vetrine aperte su un lato (`Booth`, `ShopWindow`): statiche, quindi fermano anche le esplosioni; sopra le
  garitte e i pozzi una bandierina del colore del re, se no il giocatore non sa che lì dentro c'è qualcuno.
- **Correnti** (`Scene::currents`, `Fan`, `Updraft`): forza sui proiettili in volo dentro una scatola, anche a intermittenza
  (soffioni). Vanno azzerate in `Scene::Destroy` (prima passavano al livello dopo e l'autotest rallentava). Con correnti
  `FireAt` segue il volo passo per passo e corregge la mira; se nessuna mira funziona (soffione acceso) aspetta.
- **Isole che fluttuano e giostre** (`Mechanism::carry`, `reach`): `MoverUnder` riconosce anche i re sulle torri sopra;
  `LoadDef` fa partire torri e re alla velocità di ciò che li porta (se l'isola parte a metà corsa, la torre ferma crolla).
  Un'isola mobile non deve passare dentro una fissa: la barca del Porto sbatteva la torre contro il bordo dell'isola.
  L'IA prevede dove saranno i muri fissati sulla giostra (`CarouselWall`).
- Una colonna d'aria larga quanto il pozzo non ferma i pallonetti: arrivando ripidi ci passano solo l'ultimo decimo di
  secondo. Il soffio si allarga sopra il pozzo (3,2 m) con 40 m/s²; `--test-ammo` tira pallonetti veri (con il vento, senza
  l'aria) a soffione acceso e spento, e contro l'Occhio del Ciclone.
- Cornici e fasce decorative (vetro, gomma mobile, barriera) devono sporgere di ~1 cm dalle facce che bordano: con facce
  complanari c'è z-fighting (il vetro, disegnato dopo e senza scrivere la profondità, sfarfalla).
- Debug: `printf` finisce in un buffer quando l'uscita va in una pipe, e un blocco sembrava nel livello 29 mentre era
  nel 44: per capire dove si ferma usa `stderr` (`CROLLO_DEBUG`) o gdb con `kill -INT`.
- `Progress::kMaxLevels` è 128: allargalo prima di superarlo (il file salva per id).
- **Fucina, meccanismi**: ogni pezzo ha un fermo (limiti dei giunti; motore a velocità 0 con coppia/forza bassa come
  attrito), così l'esito non dipende dalla forza del colpo. Le parti da colpire sono d'ottone. I re si proteggono con
  gabbie di barriera magica (`Cage`): la mazza della quintana e le sfere le attraversano (maschera senza `CatBarrier`).
- I suggerimenti di mira (`AimHint`) valgono finché il pezzo resta a meno di 0,6 m da dove stava, **nell'ordine in cui
  sono dati**: così si scrive una sequenza (Carrello: prima il carrello, poi il fermo). Su un meccanismo che gira
  (quintana) l'offset gira con lui; un bersaglio già scattato non vale più. Un pezzo che deve fare tutta la corsa con un
  colpo solo va reso leggero (il carrello pesante si fermava a 0,8 m su 1,4 e l'IA passava oltre).
- Bersagli (`Target`, `Scene::triggers`): colpiti da un proiettile sopra 3 m/s distruggono i giunti affidati con
  `Trigger()`; con `shatter` il bersaglio stesso va in schegge (il fermo dello scivolo: se cadeva nello scivolo bloccava la
  sfera). Il replay li riproduce senza divergenze.
- Gli scudi orbitanti (`Mechanism::boxes`) non portano nessuno: `MoverUnder` li salta, se no l'IA crede che il re al
  centro ci viaggi sopra e li ignora come ostacolo. Senza tetto l'IA li scavalca di pallonetto.
- Uno scivolo va chiuso in cima: il colpo al fermo spingeva la sfera in salita e fuori.
- **Mira assistita**: mostra tutto il volo fino al punto d'impatto, quindi un bersaglio fermo e in vista si prende sempre
  al primo colpo (riscontro dell'utente sulla Fucina). La sfida viene da ciò che si muove (maglio, paratia, ruota a pale,
  scudi orbitanti: la previsione guarda la scena di adesso) o da bersagli nascosti (dietro un parapetto, di pallonetto).
- Niente traverse sopra il varco di una paratia: un pallonetto che scende ci passa sotto e la tocca con la parte alta,
  che l'IA non controlla (controlla solo il fondo della palla). `AimHint` con `lob` < 0 vuole solo tiri tesi (fantocci).

## Nuovo PC
- Build desktop: vedi README (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, poi `cmake --build build -j`).
  Il primo configure scarica Box3D e raylib da internet.
- Build web: serve Emscripten (`emsdk`), poi `emcmake cmake -S . -B build-web ...` come nel README.
