# Crollo — guida allo sviluppo futuro

Aggiornato il 23 settembre 2026. Fino alla sessione 4 era un Claude Doc; da qui in avanti vive nel repository.

## Punto di partenza

Crollo oggi ha 10 livelli fatti a mano, la Sfida infinita procedurale, il replay deterministico e una build web. Tutto il codice sta in `src/` (circa 8.600 righe di C++17). Box3D e raylib 5.5 vengono scaricati da CMake con FetchContent. Box3D è fissato al commit `9e5a4cd`.

| File | Cosa contiene | Quando toccarlo |
| --- | --- | --- |
| `src/levels.cpp` | `Builder` (isole, torri, muri, ponti, mulini, pendoli, mongolfiere), i 10 livelli, il generatore della Sfida | nuovi livelli, nuovi pezzi riusabili |
| `src/game.cpp` | regole, cannone, proiettili, esplosioni, sconfitta dei re, telecamere, schermate, HUD, IA, replay | nuove regole, nuove munizioni, nuove schermate |
| `src/physics.cpp` | `Scene`: mondo Box3D, entità (corpo + parti da disegnare), eventi, corde, meccanismi | nuovi tipi di corpo o di evento |
| `src/render.cpp` | shader GLSL, ombre, materiali procedurali, cielo | nuovi materiali o nuovi biomi visivi |
| `src/audio.cpp` | effetti sintetizzati e musica generativa | nuovi suoni, musica per bioma |
| `src/ui.cpp` | font, pulsanti, pannelli, icone | nuove schermate |
| `web/crollo.html` | pagina di caricamento della versione web | solo per il web |

Concetti da conoscere prima di modificare:

- **Entità**: ogni corpo Box3D ha un'`Entity` con `kind` (Block, King, Projectile, Mechanism...), un materiale e una lista di `Part` da disegnare. Il corpo porta come nome il `serial` dell'entità: è ciò che permette al replay di ritrovarla.
- **Passo fisso**: la fisica avanza a 1/60 s con 4 sub-step (`Game::FixedStep`). Gli eventi (urti, contatti, giunti sovraccarichi) si gestiscono in `Game::HandleEvents`, subito dopo ogni step.
- **Meccanismi**: gli oggetti mossi nel tempo (scudi su guide, mulino) sono `Mechanism` in `Scene::mechanisms`. `FixedStep` li aggiorna usando `m_scene.time`, quindi restano sincronizzati con il replay.
- **Replay**: tutto ciò che cambia il mondo fisico va fatto con chiamate Box3D dentro `FixedStep` o negli eventi, così viene registrato. Gli effetti puramente visivi vanno annotati con `AddReplayEvent`.

Comandi utili:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/crollo                               # gioca
./build/crollo --autotest 12                 # ogni livello: stabile e vincibile?
./build/crollo --autotest-challenge 10 3     # lo stesso per la Sfida
./build/crollo --shot aim 3 60 out.png       # screenshot del livello 4
```

Per la versione web: `source ~/emsdk/emsdk_env.sh`, poi `emcmake cmake -S . -B build-web` e `cmake --build build-web`.

## Come creare nuovi livelli

Un livello è una funzione C++ che riceve un `Builder` e costruisce la scena, più una riga nella tabella `s_levels` in `src/levels.cpp`. Non servono file esterni né ricompilare altro: si scrive la funzione, la si registra, si lancia l'autotest.

### I passi

1. Scrivere `static void Level11( Builder& b )` in `src/levels.cpp`, accanto agli altri livelli.
2. Aggiungere la riga in fondo a `s_levels`: nome, sottotitolo, suggerimento, munizioni, colpi per 3 stelle (`par`), vento.
3. Compilare e lanciare `./build/crollo --autotest 12`: nessun re deve cadere da solo e l'IA deve riuscire a vincere.
4. Guardare il livello con `./build/crollo --shot aim 10 60 l11.png` (l'indice parte da 0) e con `--shot fire 10 400 l11.png`.
5. Giocarlo a mano e tarare `par` e munizioni: `par` dovrebbe essere almeno il numero di colpi usati dall'IA.

### I mattoni del Builder

Tutte le misure sono in metri, con l'asse Y verso l'alto. Il cannone sta nell'origine e spara verso +Z.

| Funzione | Cosa costruisce | Restituisce |
| --- | --- | --- |
| `PlayerIsland()` | isola del cannone con alberi e bandiera | — |
| `Island(top, raggio, profondità)` | isola sospesa, statica; `top` è il centro della superficie | entità |
| `Box(centro, mezze_misure, materiale, yaw)` / `Brick(...)` | blocco dinamico (mattone = 1 × 0,5 × 0,5 m) | entità |
| `Tower(base, piani, mezza_larghezza, altezza_piano, pilastri, solai)` | torre a pilastri e solai | quota della cima |
| `Column(base, blocchi, mezzo_lato, materiale)` | pila di cubi | quota della cima |
| `Wall(inizio, lungoX, mattoni, file, materiale)` | muro a mattoni sfalsati | quota della cima |
| `Pyramid(base, livelli, mezzo_lato, materiale)` | piramide di cubi | quota della cima |
| `Hut(base, mezza_larghezza, altezza, pareti, tetto)` | capanna con tetto | quota del tetto |
| `King(piedi, colore)` | re (guarda sempre il cannone) | entità |
| `Tnt(centro)`, `Barrel(base, materiale)` | cassa di TNT, barile | entità |
| `RopeBridge(a, b, assi, larghezza, abbassamento)` | ponte di corda che si spezza | punto più basso |
| `Pendulum(perno, lunghezza, raggio)` | palla d'acciaio su portale | entità |
| `Windmill(base, altezza, pale, velocità)` | mulino con motore | entità (pale) |
| `Slider(centro, mezze_misure, asse, ampiezza, velocità, fase, materiale)` | lastra che scorre su guide | entità |
| `BalloonBasket(centro, colore, veste)` | re in un cesto sotto un pallone | entità (pallone) |
| `Trees(...)`, `Flag(...)` | decorazioni senza fisica | — |
| `Fortress(centro, raggio)` | dove guardano telecamere e ombre (obbligatorio) | — |

Materiali disponibili per i blocchi: `Wood`, `Stone`, `Ice` (si frantuma), `Tnt` (esplode), `Metal`.

### Esempio completo

```
static void Level11( Builder& b )
{
	b.PlayerIsland();
	b.homeY = 1.0f;                       // quota delle isole su cui costruisco
	b.Island( { 0, 1, 36 }, 9.0f );

	// muro basso davanti, due torri dietro, TNT alla base della torre di sinistra
	b.Wall( { -3.0f, 1.0f, 31.5f }, true, 6, 2, Mat::Stone );
	b.Tnt( { -2.5f, 1.35f, 36.0f } );
	float t1 = b.Tower( { -2.5f, 1.0f, 36.0f }, 3, 1.0f, 1.2f, Mat::Wood, Mat::Wood );
	b.King( { -2.5f, t1, 36.0f }, kCrimson );
	float t2 = b.Tower( { 2.5f, 1.0f, 37.0f }, 2, 1.0f, 1.2f, Mat::Ice, Mat::Wood );
	b.King( { 2.5f, t2, 37.0f }, kBlue );

	b.Trees( { 0, 1, 36 }, 9.0f, 4, 6.5f );
	b.Fortress( { 0, 3, 36 }, 11.0f );
}

// in s_levels:
{ "Il Bastione", "Legno e ghiaccio", "Il TNT è nascosto sotto la torre di sinistra.",
  { 3, 1, 0, 0, 0 }, 2, { 0.5f, 0, 0 }, Level11 },
```

### Regole imparate costruendo i primi 10

- **Distanze**: tenere la fortezza fra 25 e 45 m dal cannone. La velocità di lancio va da 12 a 34 m/s: oltre i 45 m servono tiri molto alti.
- **`homeY`**: impostarlo alla quota della superficie prima di costruire su un'isola. Serve a contare i blocchi caduti e a capire quando un re è precipitato.
- **I re vogliono appoggi piatti**: un solaio, un cubo di almeno 0,8 m o il terreno. Un re che parte inclinato viene contato come abbattuto.
- **Niente compenetrazioni con la roccia**: il primo ponte crollava da solo perché le prime assi nascevano dentro l'isola. Ancorare i pezzi al bordo, non dentro.
- **Le catene tese non reggono carichi**: ponti e corde vanno costruiti con un abbassamento (`sag`), altrimenti la tensione esplode e i giunti si spezzano.
- **Tutto ciò che si muove da solo** (motori, molle, palloni) deve avere lo sleep disattivato, come fanno già `Windmill`, `Slider` e `BalloonBasket`.
- **Colori dei re**: `kPurple`, `kCrimson`, `kBlue`, `kGreen`, `kOrange`, `kTeal`.

### Limiti attuali da sapere

- I progressi salvano al massimo 32 livelli (`Progress::stars[32]`): per andare oltre va allargato l'array.
- La schermata Livelli è una griglia 5 × 2 pensata per 10 livelli. Dall'undicesimo serve una schermata a pagine, che conviene fare insieme alle campagne (più sotto).
- I livelli sono scritti in C++. Un formato dati (per esempio JSON caricato all'avvio) permetterebbe un editor, ma oggi non serve: il Builder è già compatto e l'autotest fa da verifica.

## Nuove meccaniche

La meccanica da fare per prima sono gli scudi a tempo: aggiungono la dimensione del *quando* sparare e riusano quasi tutto quello che c'è già. Le altre proposte sono ordinate dalla più economica alla più costosa. Costo: S = una sessione, M = due o tre, L = di più.

### Scudi a tempo per i re

Oggi esiste già una forma semplice: le lastre su guide del livello 9 (`Builder::Slider`). Si possono aggiungere tre varianti, tutte come nuovi tipi di `Mechanism`.

| Variante | Come si comporta | Come si fa in Box3D |
| --- | --- | --- |
| Scudo a scomparsa | una barriera che c'è per N secondi e sparisce per M | corpo statico o cinematico; `b3Body_Disable` / `b3Body_Enable` a orario fisso |
| Scudo orbitante | una o più lastre che girano attorno al re | corpo cinematico mosso con `b3Body_SetTargetTransform` lungo un cerchio |
| Cupola a respiro | una cupola che si apre e si chiude a spicchi | spicchi su giunti rotoidali con molla, `b3RevoluteJoint_SetTargetAngle` a tempo |

Punti di progetto:

- **Il giocatore deve poter prevedere.** Mezzo secondo prima di sparire lo scudo lampeggia; prima di riapparire se ne vede la sagoma trasparente. In HUD, un piccolo anello attorno al re mostra quanto manca al cambio.
- **Il tempo di volo conta.** Un colpo impiega da 1 a 3 secondi. Nella mira assistita (tasto T) il punto d'impatto può diventare verde o rosso a seconda che lo scudo sarà aperto o chiuso quando il colpo arriva.
- **Tempi deterministici.** L'orario va calcolato da `m_scene.time` dentro `FixedStep`, come fanno già gli `Slider`. Così il replay riproduce lo scudo esattamente (Box3D registra `Enable`, `Disable` e i cambi di trasformata).
- **Uno scudo che riappare dentro un oggetto** lo spinge via con violenza. Prima di riattivarlo si controlla la zona con `b3World_OverlapShape`; se è occupata, si rimanda al passo successivo.
- **IA e autotest**: la funzione `AutoFireAtKing` deve imparare ad aspettare. Basta simulare in avanti l'orario dello scudo e sparare quando `tempo di volo + ora attuale` cade in una finestra aperta.

Costo: S per lo scudo a scomparsa, M per le altre due varianti con le relative indicazioni in HUD. Stato: lo scudo a scomparsa è fatto (\`Builder::Shield\`, \`MechType::Blinker\`), con test \`--test-shields\`; mancano lo scudo orbitante e la cupola a respiro.

### Altre proposte

| Meccanica | Cosa cambia nel gioco | Come si fa in Box3D | Costo |
| --- | --- | --- | --- |
| Materiale gomma | superfici che fanno rimbalzare i colpi: si può giocare di sponda | `restitution` alta sul materiale | S |
| Sacchi di sabbia | blocchi pesanti che assorbono gli urti invece di cadere | densità alta, damping alto, attrito alto | S |
| Bomba a implosione | risucchia i blocchi verso il centro invece di spingerli via | `b3World_Explode` con `impulsePerArea` negativo, già supportato | S |
| Bomba adesiva | si attacca a ciò che colpisce ed esplode dopo 3 secondi | giunto `Weld` creato all'evento di contatto | S |
| Arpione | una corda si aggancia a una torre; poi il cannone la tira giù | giunto di distanza con motore creato all'impatto (`b3DistanceJoint_EnableMotor`) | M |
| Ventole e correnti | zone d'aria che deviano i proiettili, visibili con particelle | shape sensore + `b3Shape_ApplyWind` sui corpi dentro | M |
| Leve e catapulte nemiche | colpendo un'estremità si lancia il re dall'altra | asse su giunto rotoidale con limiti | M |
| Isole che fluttuano | isole nemiche che salgono e scendono lentamente | corpo cinematico con `b3Body_SetTargetTransform` | M |
| Re che camminano | guardie e re che si spostano fra due punti e si riparano dietro i muri | il *character mover* di Box3D: `b3World_CastMover` e `b3World_CollideMover` | L |
| Re snodati (ragdoll) | cadute più comiche e credibili | corpi collegati da giunti sferici con limiti di cono; Box3D ne ha un esempio in `shared/human.c` | L |
| Pietra che si spacca | la pietra si rompe in frammenti irregolari come il ghiaccio | frammenti precalcolati con `b3CreateHull` su punti casuali | M |
| Terreno irregolare | isole con canyon, rampe e colline | shape `mesh` o `heightField` statiche | M |

### Livelli a meccanismo (idee di settembre 2026)

Tre famiglie di livelli in cui conta il *quando* e il *come*, non solo il *dove*. Ognuna diventa la meccanica di una delle campagne che mancano. L'ultimo livello di ogni campagna è un **boss**: può essere difficile fino a essere quasi frustrante, purché resti leggibile e vincibile.

| Famiglia | Esempi | Come si fa in Box3D | Campagna |
| --- | --- | --- | --- |
| Bersagli mobili | re su una piattaforma che scorre in orizzontale o sale e scende; re su un nastro che passa dietro barriere; doppia finestra con uno scudo a tempo | corpo cinematico mosso da `FixedStep` con `m_scene.time` (il re sta sopra per attrito); accelerazioni basse, o il re cade da solo | Dune Sospese |
| Tiri di sponda | re protetto da vetro, raggiungibile solo di rimbalzo; pannelli di gomma che scorrono o ruotano e danno l'angolo giusto solo per un attimo | `Bumper` su corpo cinematico, con velocità lineare o angolare costante; il pannello in moto aggiunge velocità alla palla | Arcipelago delle Tempeste |
| Meccanismi a catena | piattaforma girevole con una flangia esposta che porta il re sotto una pila di scatole; guida tenuta su da un perno che, abbattuto, fa scendere una sfera sulla dinamite; tronco appeso che si sgancia colpendo un bersaglio e oscilla sul nastro del re | giunti rotoidali e prismatici con **fine corsa** e attrito; `b3DestroyJoint` per sganciare; guide e imbuti fatti di assi (Box3D ha solo forme convesse) | Fucina del Vulcano |

Pezzi nuovi da aggiungere al Builder:

- **Vetro infrangibile** (`Mat::Glass`): statico e trasparente, lascia vedere il re ma ferma i colpi. Deve sembrare diverso dal cristallo a tempo (cornice di metallo, riflessi), altrimenti il giocatore aspetta che si spenga.
- **Piattaforma e nastro**: corpo cinematico con un percorso (andata e ritorno, con pause alle estremità).
- **Gomma mobile**: `Bumper` che scorre o ruota.
- **Bersaglio-interruttore**: un piccolo bersaglio che, colpito, esegue un'azione: sgancia un giunto, accende un motore, fa partire una piattaforma, spegne uno scudo. È il pezzo che permette di comporre le catene.
- **Parti di catena**: piattaforma girevole con fine corsa, guida incernierata su un perno, imbuto di pannelli.

Regole di progetto:

- **Ogni anello della catena ha un fermo.** Qualunque sia la forza del colpo, il pezzo si ferma sempre nello stesso punto. Senza fermi l'esito dipende dal caso e il livello diventa una lotteria.
- **Il meccanismo si deve capire guardandolo.** Le parti che si possono colpire hanno un colore comune (per esempio ottone); il volo d'apertura passa sui punti chiave; `hint` suggerisce il primo passo.
- **Il pendolo da colpire è poco prevedibile** (la spinta dipende da dove lo prendi): meglio un peso sganciato da un interruttore, che oscilla sempre allo stesso modo.

Verifica:

- **IA con anticipo**: il moto cinematico lo decidiamo noi, quindi la posizione futura del re si calcola esatta. L'IA stima il tempo di volo e mira dove il re *sarà*.
- **Soluzione scritta nel livello** per le catene (per esempio `b.Solution(...)`): la sequenza di colpi con le loro condizioni ("prima la flangia, poi la pila quando la piattaforma è ferma"). L'autotest la esegue; lo scanner controlla che non ci sia una scorciatoia a un colpo solo.
- **Modalità trucchi** (fatta): con F9 in gioco o `--cheat` i colpi sono infiniti, l'HUD conta i colpi sparati e la vittoria non viene salvata. Serve a provare a mano se un boss è davvero vincibile.

### Idee da scartare o rimandare

- **Fuoco che consuma il legno**: bello, ma richiede di cambiare massa e forma dei corpi nel tempo; con Box3D va ricreato lo shape a ogni stadio. Rimandare.
- **Acqua**: Box3D non simula fluidi; si può solo fingere con forze di galleggiamento. Vale solo per un bioma specifico (vedi campagne).

## Campagne, biomi e narrativa

La proposta è di organizzare il gioco in 6 campagne da 8 livelli, ognuna con un bioma, una meccanica nuova e un re antagonista. I 10 livelli attuali diventano la prima campagna più metà della seconda. La trama resta leggera: poche righe prima e dopo ogni campagna, e una battuta del re all'inizio di ogni livello.

### La trama in breve

Le isole del Regno di Sopra restano in cielo grazie alla **Corona dei Venti**. Sei re avidi l'hanno spezzata e se ne sono presi un frammento ciascuno. Senza la corona intera, le isole cominciano lentamente a scendere verso le nuvole. Il giocatore è **Mastra Bombarda**, l'ultima artigliera delle isole libere: con il suo cannone su un'isola volante va a riprendersi i frammenti, un regno alla volta.

Come si racconta, senza appesantire:

- **Carta d'apertura** della campagna: un'illustrazione fatta con la scena 3D (camera lenta sulla fortezza del re) e tre righe di testo.
- **Battuta del re** nell'intro di ogni livello, sotto il nome del livello (oggi c'è il sottotitolo: basta sostituirlo). Esempio: *«Le mie torri di ghiaccio non si sciolgono, figuriamoci sotto le tue palle di ferro.»*
- **Carta di chiusura**: il frammento della corona torna al suo posto e l'isola del giocatore sale un po' di quota (si vede nella mappa).
- Il re della campagna è riconoscibile: colore della veste, corona diversa, e nell'ultimo livello ha un modello più grande.

### Le campagne

| Campagna | Bioma e aspetto | Meccanica introdotta | Il re |
| --- | --- | --- | --- |
| 1. Prati Alti | l'aspetto di oggi: erba, legno, cielo azzurro | le basi: palla, bomba, ponti, TNT | Re Bernardo il Tondo, viola |
| 2. Valle dei Mulini | colline dorate al tramonto, mulini ovunque | mulini, pendoli, scudi su guide, vento | Regina Ottavia, arancio |
| 3. Picchi Gelati | neve e ghiaccio, cielo bianco, nevischio | ghiaccio che si frantuma, isole scivolose, **scudi a scomparsa** | Re Ghiacciolo III, azzurro |
| 4. Dune Sospese | arenaria e sabbia, cielo ocra | **bersagli mobili**: re su piattaforme e nastri, re che passano dietro il vetro infrangibile; vento che cambia a ogni turno | Sultana Zaira, oro |
| 5. Arcipelago delle Tempeste | cielo grigio, pioggia, isole che fluttuano | **tiri di sponda**: gomma fissa, che scorre o che ruota, re raggiungibili solo di rimbalzo; isole in movimento, correnti d'aria | Re Fulmine, blu scuro |
| 6. Fucina del Vulcano | ferro e basalto, lava al posto delle nuvole | **meccanismi a catena**: piattaforme girevoli, guide e perni, bersagli-interruttore, pesi da sganciare; scudi orbitanti, arpione | l'Imperatore di Ferro, rosso |

### Cosa serve nel codice

- **Struttura dati**: una `struct Campaign` con nome, testi di apertura e chiusura, `Biome` e l'elenco dei suoi `LevelDef`. `s_levels` diventa un array per campagna.
- **Biomi nel renderer**: oggi i colori di cielo, nebbia e nuvole e la quota delle nuvole (`cloudY = -22`) sono costanti scritte negli shader. Vanno trasformati in uniform e raccolti in una `struct Biome`, insieme a colori di erba e roccia, direzione del sole, particelle d'ambiente (neve, sabbia, pioggia) e scala e tempo della musica generativa.
- **Mappa**: una nuova schermata con le isole-campagna sospese e collegate da sentieri, che sostituisce la griglia 5 × 2. Le campagne si sbloccano con le stelle (per esempio 12 stelle su 24 per aprire la successiva).
- **Salvataggi**: le chiavi diventano campagna + livello; va migrato il file attuale (i livelli 1-10 corrispondono alle prime due campagne).
- **Sfida infinita**: può pescare a caso un bioma già sbloccato a ogni round, così la varietà cresce senza lavoro in più.

Costo: M per struttura dati, mappa e biomi nel renderer; poi ogni campagna nuova è soprattutto lavoro di level design (8 livelli per circa una sessione, grazie all'autotest).

## Multiplayer: coop e 1vs1

Il multiplayer è fattibile se resta **a turni**: un 1vs1 sullo stesso computer costa poco e va fatto per primo; l'online a turni è possibile ma richiede un piccolo server; il tempo reale online è da evitare. Il motivo è la fisica: sincronizzare centinaia di blocchi in tempo reale fra due computer è difficile, mentre a turni basta scambiarsi i colpi.

| Modalità | Com'è | Difficoltà tecnica | Costo | Consiglio |
| --- | --- | --- | --- | --- |
| Duello sullo stesso PC | due cannoni su isole opposte, ognuno con la sua fortezza e il suo re; si spara a turno | bassa: un solo mondo, cambia solo di chi è il turno | S–M | fare per primo |
| Coop sullo stesso PC | due giocatori alternano i colpi contro la stessa fortezza, con munizioni in comune | bassa | S | solo come variante del Duello |
| Schermo diviso in tempo reale | due cannoni che sparano insieme, due telecamere | media: doppio rendering (oggi \~5 ms a frame, regge) e serve un gamepad per il secondo giocatore | M | dopo il Duello, se piace |
| Online a turni (1vs1 o coop) | come il Duello, ma a distanza | media: si inviano solo i parametri dei colpi | M–L | possibile, con un server |
| Online in tempo reale | spari simultanei a distanza | alta: stato fisico da sincronizzare di continuo | L+ | sconsigliato |

### Il Duello (1vs1 locale)

- **Campo**: due isole-fortezza speculari a 40 m, ognuna con cannone, mura e 2-3 re. Vince chi abbatte per primo tutti i re avversari.
- **Turni**: un colpo a testa. Il turno passa quando la scena si è calmata (la stessa logica del «calmo» che oggi decide la sconfitta).
- **Variante con costruzione**: prima della battaglia ogni giocatore ha un budget (per esempio 40 blocchi) e piazza le sue difese su una griglia. È la parte che rende ogni partita diversa.
- **Cosa cambia nel codice**: il cannone diventa un array di due (`m_cannonPos`, `m_yaw`, `m_ammo`... dentro una `struct Player`), la telecamera di mira segue il cannone di turno, l'HUD mostra due punteggi. Il vento può cambiare a ogni turno.
- **Coop**: stesso impianto, ma entrambi i cannoni sulla stessa isola contro una fortezza più grande.

### L'online a turni

- **Si inviano gli input, non la fisica**: tipo di munizione, direzione, potenza e l'istante dell'abilità speciale. Entrambi i computer simulano lo stesso colpo. Box3D è progettato per essere deterministico anche fra piattaforme diverse, e il gioco compila già con `-ffp-contract=off` come richiede Box3D.
- **Controllo di coerenza**: a fine turno ognuno calcola un hash dello stato (posizioni dei corpi) e lo confronta con l'altro. Se diverge, l'host invia le posizioni di tutti i corpi e l'altro le applica con `b3Body_SetTransform`.
- **Da verificare prima**: che la versione desktop e quella web producano lo stesso risultato per lo stesso colpo. Si può testare con il replay già esistente: registrare un colpo su desktop e riprodurlo nel browser.
- **Trasporto**: il browser non può aprire connessioni dirette fra giocatori, e gli Artifact bloccano WebRTC. Serve un piccolo server relay WebSocket (per esempio in Node, poche centinaia di righe) con stanze da due e un codice partita. Va ospitato da qualche parte: è il costo vero di questa modalità.

### Rischi

- Il determinismo fra build diverse (desktop, web, compilatori diversi) è la cosa più incerta: senza il controllo di coerenza le partite online si separerebbero in silenzio.
- Il server relay va mantenuto e protetto (limiti di stanze, pulizia delle partite abbandonate).
- Il mouse catturato è pensato per un giocatore: nel Duello locale si passa la mano a ogni turno, quindi va bene; lo schermo diviso richiede il gamepad.

## Roadmap proposta

L'ordine consigliato parte dalle cose piccole che rendono il gioco più vario, poi costruisce la struttura delle campagne e lascia il multiplayer online per ultimo. Ogni passo si chiude con autotest verde e una prova a mano.

| Sessione | Obiettivo | Perché in quest'ordine |
| --- | --- | --- |
| 1 | Fatto (commit 0488537): scudi a scomparsa + indicatore in HUD + IA che aspetta la finestra giusta; livelli 11 e 12 | è la tua idea, costa poco e cambia davvero il modo di giocare |
| 2 | Fatto (commit 6a36993): gomma, sacchi di sabbia, Vortice (implosione), bomba adesiva; livelli 13 e 14 | quattro novità da una sessione, tutte con API Box3D già pronte |
| 3 | Fatto (commit 23ad3fe): `Campaign` e `Biome` (sei biomi come uniform, con meteo e musica propri), mappa dei regni, prologhi ed epiloghi, salvataggi per id con migrazione; livelli esistenti divisi in Prati Alti (8), Valle dei Mulini (3), Picchi Gelati (3) | sblocca tutto il lavoro sui contenuti che segue |
| 4 | Fatto (commit 2cd384d): Picchi Gelati a 8 livelli con Crepacci, Curling, Stalattiti, Valanga e il finale La Reggia di Ghiacciolo; nuovi pezzi del Builder (pietre da curling, palle di neve, diga di ghiaccio, tetti di neve) e suggerimenti di mira per l'IA | la prima campagna nuova mette alla prova la struttura |
| 4b | In corso: modifiche dal riscontro dei tester. Fatto: modalità trucchi (F9 o `--cheat`: colpi infiniti, vittorie non salvate) | prima di aggiungere, sistemare ciò che i tester hanno notato |
| 5 | Duello 1vs1 sullo stesso PC, solo scontro con fortezze pronte, con coop come variante | multiplayer al costo più basso |
| 6 | Dune Sospese: piattaforme e nastri cinematici, vetro infrangibile, IA che anticipa i bersagli mobili; 8 livelli con boss finale | i bersagli mobili sono la meccanica più semplice e servono anche dopo |
| 7 | Arcipelago delle Tempeste: gomma che scorre e ruota, ventole e correnti, isole che fluttuano; 8 livelli di sponda con boss finale | riusa i corpi cinematici della sessione 6 |
| 8 | Fucina del Vulcano: bersagli-interruttore, parti di catena, soluzione scritta nei livelli, scudi orbitanti, arpione; 8 livelli con boss finale | la più complessa: usa tutti i pezzi precedenti |
| dopo | Fase di costruzione del Duello; test di determinismo desktop contro web; poi online a turni con relay Node.js sul tuo server | solo se il Duello locale piace |

### Decisioni prese

- **Nomi e tono**: restano Mastra Bombarda e la Corona dei Venti.
- **Dimensione**: 6 campagne da 8 livelli, 48 in tutto.
- **Duello**: prima solo scontro, con fortezze già pronte. La fase di costruzione delle difese arriva in una sessione successiva.
- **Meccaniche per campagna**: Dune Sospese = bersagli mobili, Arcipelago delle Tempeste = tiri di sponda, Fucina del Vulcano = meccanismi a catena.
- **Boss**: l'ultimo livello di ogni campagna può essere molto difficile; si prova con la modalità trucchi.
- **Online**: il server disponibile può eseguire Node.js, quindi il relay WebSocket per l'online a turni può girare lì, accanto alla versione web del gioco.
