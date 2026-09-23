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
- La versione web pubblicata come Artifact (https://claude.ai/artifact/HNRpbEuELhrBG6CQuAioQ5) si aggiorna solo
  quando l'utente lo chiede: pagina `web/crollo.html` + `build-web/crollo.js` + `build-web/crollo.wasm`.

## Stato (fine sessione 4, settembre 2026)
- Fatte le sessioni 1-4: scudi di cristallo a tempo; gomma, sacchi, Vortice, bomba adesiva; campagne, biomi, mappa
  dei regni, storie, salvataggi per id; Picchi Gelati a 8 livelli.
- 19 livelli. Campagne: Prati Alti 8, Valle dei Mulini 3, Picchi Gelati 8; Dune Sospese, Arcipelago delle Tempeste e
  Fucina del Vulcano sono "in arrivo" (bioma, re e testi già pronti in `src/biomes.cpp` e `BuildCampaigns()`).
- L'utente sta facendo provare il gioco ad altri: **la sessione 4b parte dal loro riscontro** (modifiche e
  aggiustamenti), poi la roadmap riprende dalla sessione 5 (Duello 1vs1 sullo stesso PC, "prima solo scontro").
  Già fatta nella 4b: modalità trucchi (F9 o `--cheat`, colpi infiniti, vittorie non salvate).
- Sessioni 6-8: una campagna ciascuna con la sua meccanica (Dune = bersagli mobili, Arcipelago = sponde di gomma
  mobili, Fucina = meccanismi a catena), ultimo livello "boss" molto difficile. Dettagli in `docs/sviluppo.md`.
- Decisioni già prese dall'utente: 6 campagne da 8 livelli; Duello prima solo scontro; il suo server può far girare
  Node.js (servirà per l'online a turni, più avanti).

## Verifica: da rifare dopo ogni modifica al gameplay
```bash
./build/crollo --autotest 12            # tutti i livelli vincibili (oggi 19/19); --autotest 12 N per il solo livello N
./build/crollo --scan-shots N           # "!!" = un colpo solo abbatte tutti i re (voluto solo nei livelli 7, 8, 18)
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
- Stabilità: il livello deve stare in piedi da solo (l'autotest controlla i primi 5 s); lascialo anche 15 s con `--shot`.
- Web: `EXPORTED_RUNTIME_METHODS=HEAPF32`; nei test di input via browser tieni premuti tasti e click ~120 ms.

## Nuovo PC
- Build desktop: vedi README (`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, poi `cmake --build build -j`).
  Il primo configure scarica Box3D e raylib da internet.
- Build web: serve Emscripten (`emsdk`), poi `emcmake cmake -S . -B build-web ...` come nel README.
