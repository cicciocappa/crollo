# CROLLO — un assedio fisico tra le nuvole

**Crollo** è un gioco d'assedio in 3D costruito attorno a [Box3D](https://github.com/erincatto/box3d), il motore fisico 3D di Erin Catto.
Su isole sospese sopra un mare di nuvole i re nemici si sono barricati in torri di legno, pietra e ghiaccio.
Hai un cannone e poche munizioni: falli cadere, ribaltare o colpiscili in pieno.

![Titolo](docs/titolo.jpg)

| | |
|---|---|
| ![Polveriera](docs/polveriera.jpg) | ![Replay](docs/replay.jpg) |
| ![Mongolfiere](docs/mongolfiere.jpg) | ![Sfida infinita](docs/sfida.jpg) |

## Il gioco

- **10 livelli** fatti a mano, ognuno costruito attorno a una caratteristica diversa di Box3D:
  1. *Primo Colpo*: tutorial, una torre di legno.
  2. *Mura di Pietra*: muro a mattoni sfalsati; arriva la bomba.
  3. *Il Ponte*: ponte di corda fatto di assi e giunti sferici che **si spezzano** se sovraccaricati.
  4. *Palazzo di Ghiaccio*: blocchi di ghiaccio scivolosi che si **frantumano** in otto schegge.
  5. *Mongolfiere*: re in cesti appesi a palloni con gravità negativa, ancorati con corde; buca i palloni.
  6. *Il Mulino*: pale mosse da un **giunto rotoidale motorizzato** che deviano i colpi.
  7. *Il Pendolo*: una palla d'acciaio appesa a un **giunto di distanza**, con il vento contro.
  8. *Polveriera*: casse di TNT che esplodono a catena.
  9. *Scudi Mobili*: lastre di pietra su **giunti prismatici** con molla che scorrono avanti e indietro.
  10. *La Cittadella*: tre isole, sei re, tutto l'arsenale.
- **Sfida infinita**: fortezze generate proceduralmente, sempre più difficili; i punti si sommano round dopo round e il record viene salvato.
- **5 munizioni**: palla di ferro, bomba (esplode all'impatto o con SPAZIO), grappolo (si divide in 7 con SPAZIO), palla incatenata (due sfere legate che ruotano e spazzano), macigno (convex hull irregolare, enorme e pesante).
- **Replay del colpo decisivo**: dopo ogni vittoria il colpo viene *ri-simulato* dalla registrazione deterministica di Box3D e mostrato al rallentatore con una telecamera cinematografica (vedi sotto).
- Stelle in base ai colpi usati, bonus per le munizioni avanzate, progressi salvati.
- Tutto l'audio è **sintetizzato al volo**: effetti (cannone, legno, pietra, ghiaccio, esplosioni, "wooo" del re...) generati all'avvio, e una musica generativa per liuto (sintesi Karplus-Strong su una cadenza andalusa) con vento ambientale.
- Grafica: shadow mapping con PCF, materiali procedurali nello shader (venature del legno, pietra, ghiaccio con fresnel, TNT), cielo con mare di nuvole procedurale in cui gli oggetti "affondano", particelle, slow motion, screen shake.

## Comandi

| Tasto | Azione |
|---|---|
| Mouse | ruota il cannone |
| Rotellina / W-S | potenza (SHIFT per regolazioni fini) |
| Click sinistro | spara (in volo: torna al cannone) |
| Click destro (tenuto) | cannocchiale |
| 1-5, Q/E | scegli la munizione |
| SPAZIO | abilità speciale del proiettile in volo |
| TAB | panoramica libera della fortezza |
| T | mira assistita (traiettoria completa con punto d'impatto) |
| R | ricomincia · O ombre · M musica · F11 schermo intero · ESC pausa |

## Compilare ed eseguire

Serve un compilatore C/C++ (C17/C++17), CMake ≥ 3.22 e una connessione internet al primo configure:
Box3D (a un commit fissato) e raylib 5.5 vengono scaricati automaticamente con `FetchContent`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/crollo
```

Su Linux servono gli header di X11/OpenGL/ALSA per raylib (su Ubuntu:
`sudo apt install libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev libasound2-dev`).
Il gioco è stato sviluppato e verificato su Linux; su Windows e macOS lo stesso procedimento CMake dovrebbe bastare, ma non l'ho provato.

Gli asset (un solo font, Lilita One, licenza OFL) vengono copiati accanto all'eseguibile dopo la build.

### Versione web (WebAssembly)

Con [Emscripten](https://emscripten.org) installato e attivato (`source emsdk_env.sh`):

```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j
cd build-web && python3 -m http.server 8000   # poi apri http://localhost:8000
```

Escono tre file da pubblicare insieme: `index.html` (la pagina, generata da `web/crollo.html`), `crollo.js` e
`crollo.wasm` (~1,2 MB, con il font incorporato). Va servita da un server HTTP qualsiasi, non aperta come file.
La build web usa WebGL2 e gira su un solo thread (niente `SharedArrayBuffer`, quindi nessun header speciale lato server);
i salvataggi finiscono nel `localStorage` del browser. Nel browser ESC libera il mouse e mette in pausa; un click sulla
scena lo riaggancia.

### Strumenti di verifica inclusi

```bash
./build/crollo --autotest [colpi]             # headless: ogni livello è stabile e vincibile?
./build/crollo --autotest-challenge [round] [seed]  # lo stesso per le fortezze procedurali
./build/crollo --shot <modo> <livello> <frame> out.png   # screenshot (aim, fire, intro, title, select, pause, howto, challenge)
./build/crollo --export-audio <cartella>      # esporta in WAV tutti i suoni sintetizzati e 30 s di musica
```

L'autotest carica ogni livello senza finestra, lascia assestare le strutture per 5 secondi (nessun re deve cadere da solo)
e poi fa giocare un'IA che risolve la balistica in modo esatto (gravità + vento come accelerazione costante) e sceglie
la parabola facendo *ray cast* lungo la traiettoria per evitare gli ostacoli. La stessa IA gioca la demo nel menu principale.

## Come viene usato Box3D

| Funzionalità di Box3D | Dove nel gioco |
|---|---|
| Convex hull (`b3MakeTransformedBoxHull`, `b3CreateCylinder`, `b3CreateCone`, `b3CreateHull`) | blocchi, isole, re, corone, macigni irregolari |
| Sfere, corpi multi-shape | proiettili, re (tunica + testa), cesti, pale del mulino |
| Continuous collision (`isBullet`) | proiettili veloci che non attraversano le torri |
| `b3World_Explode` | bombe e TNT (con reazioni a catena) |
| Contact hit events | suoni per materiale, polvere, rottura del ghiaccio, innesco del TNT, re colpiti |
| Contact begin events | la bomba esplode anche con un tocco leggero |
| Body move events | sincronizzazione efficiente delle trasformate da renderizzare |
| Joint events + `forceThreshold` | le corde del ponte si spezzano quando vengono sovraccaricate |
| Giunti sferici con molla | il ponte di corda |
| Giunto rotoidale con motore | il mulino |
| Giunto prismatico con molla e limiti | gli scudi mobili |
| Giunto di distanza rigido e "a corda" (molla a 0 Hz + limite) | pendolo, palla incatenata, funi delle mongolfiere, ormeggi |
| `gravityScale` negativa, damping, forze | mongolfiere e vento sui proiettili |
| Ray cast (`b3World_CastRayClosest`) | mira assistita e pianificazione della traiettoria dell'IA |
| Sleep delle isole, multithreading (`workerCount`) | scene con centinaia di corpi a costo basso |
| **Recording & replay** (`b3World_StartRecording`, `b3CreatePlayer`, `b3RecPlayer_StepFrame`) | il replay del colpo decisivo |

### Il replay

A ogni colpo il gioco riavvia una registrazione Box3D: la registrazione parte da uno snapshot del mondo e annota ogni
mutazione (corpi creati e distrutti, forze, esplosioni, giunti spezzati) e ogni step. Dopo la vittoria un `b3RecPlayer`
ricostruisce quel mondo e lo ri-simula in modo deterministico (il player verifica anche l'hash dello stato a ogni step:
nelle prove fatte non è mai divergente). Ogni corpo ha come *nome* un numero seriale, che sopravvive alla registrazione, e il gioco lo
usa per ritrovare l'aspetto dell'entità originale, anche se nel frattempo è stata distrutta. Gli effetti che la fisica
non conosce (particelle delle esplosioni, stelline dei re) sono eventi con il numero di step, riprodotti allo step giusto;
i colpi del mondo ri-simulato generano i loro contact event, quindi suoni e polvere tornano da soli.

## Architettura

```
src/
  main.cpp       finestra, loop principale, modalità di test e screenshot
  game.cpp/.h    regole, cannone, proiettili, esplosioni, re, telecamere, schermate, HUD, IA, replay, sfida
  levels.cpp/.h  Builder (isole, torri, muri, ponti, mulini, pendoli, mongolfiere...), 10 livelli e generatore procedurale
  physics.cpp/.h Scene: mondo Box3D, entità (corpo + parti renderizzabili), eventi, corde, meccanismi
  render.cpp/.h  shader GLSL (luce, ombre, materiali procedurali, cielo), mesh dai convex hull di Box3D
  particles.*    polvere, fumo, scintille, schegge, esplosioni
  audio.*        sintesi degli effetti e musica generativa nel thread audio
  ui.*           font, pulsanti, pannelli, icone disegnate a mano
```

La simulazione usa un passo fisso di 1/60 s con 4 sub-step, interpolazione delle trasformate per il rendering e un
fattore di scala del tempo per lo slow motion.

## Crediti e licenze

- Codice del gioco: scritto per questo progetto.
- [Box3D](https://github.com/erincatto/box3d) di Erin Catto — licenza MIT.
- [raylib](https://www.raylib.com) di Ramon Santamaria — licenza zlib.
- Font [Lilita One](https://fonts.google.com/specimen/Lilita+One) — SIL Open Font License (`assets/fonts/OFL-LilitaOne.txt`).
