# Architecture logicielle de GAME TAB

Ce document complète les diagrammes SysML du dossier (exigences, contexte, blocs, cas d'utilisation)
avec la vue **logicielle** du projet : les deux programmes, leurs modules, et la façon dont ils communiquent.

- **`firmware/GAME_TAB_MULTI/GAME_TAB_MULTI.ino`** : le jeu, en C++ (Arduino), sur chaque M5Stack Tab5.
- **`serveur/serveur_admin.py`** : le serveur du mode multijoueur, en Python 3 (bibliothèque standard uniquement), sur un PC.

Les diagrammes ci-dessous sont écrits en [Mermaid](https://mermaid.js.org/) : GitHub les affiche directement.

---

## 1. Diagramme d'architecture logicielle (composants)

```mermaid
flowchart LR
    subgraph TAB["Tablette M5Stack Tab5 : GAME_TAB_MULTI.ino (C++ / Arduino), jusqu'à 8 tablettes"]
        direction TB
        LOOP["<b>loop()</b> : machine à états<br/>ACCUEIL, CHOIX_PERSO, CHOIX_NIVEAU,<br/>JEU, PAUSE, PERDU, FIN,<br/>LOBBY_MULTI, COMPTE"]
        GESTES["<b>5. Gestes</b><br/>lireGlissement()<br/>toucher = saut, glisser bas = roulade"]
        NIV["<b>2. Niveaux</b><br/>3 cartes en texte, cases 60x60 px<br/>chargerNiveau()"]
        PHYS["<b>6. Physique et monstres</b><br/>avancerJeu() puis unPas() tous les 1/120 s<br/>gravité, collisions, pièces, bonus,<br/>épée étoile, aimant, bulle"]
        RES["<b>6 bis. Réseau multijoueur</b><br/>wifiDemarrer(), majWifi()<br/>lireReseau(), traiterMessage()<br/>envoisPeriodiques(), preparerBonusMulti()"]
        SON["<b>7. Sons</b><br/>jouerSons(), jouerMelodie()"]
        DESS["<b>8 et 9. Dessin et écrans</b><br/>canvas 720x1280 en PSRAM<br/>dessinerPartie(), dessinerLobby()...<br/>pushSprite() une fois par image"]
        MEM[("<b>Preferences</b> (mémoire flash)<br/>records, étoiles,<br/>personnage, volume")]
    end

    subgraph LIB["Bibliothèques de la tablette"]
        direction TB
        M5["<b>M5Unified</b><br/>écran, tactile, haut-parleur"]
        WIFI["<b>WiFi + WiFiUdp</b><br/>puce Wi-Fi ESP32-C6 (liaison SDIO)"]
    end

    subgraph PC["PC serveur : serveur_admin.py (Python 3, sans dépendance)"]
        direction TB
        ECOUTE["<b>Thread ecoute()</b><br/>reçoit les messages UDP<br/>traiter() : HELLO, PRG, FIN, BYE"]
        BOUCLE["<b>Thread boucle()</b> toutes les 30 ms<br/>phases : salon, compte, course, termine<br/>classement en direct et final"]
        ETAT[("<b>État partagé S</b><br/>joueurs, participants, phase,<br/>niveau, graine, résultat<br/>protégé par un verrou RLock")]
        WEB["<b>Serveur HTTP</b> (ThreadingHTTPServer, port 8080)<br/>GET / et GET /api/state<br/>POST /api/start, stop, kick,<br/>niveau, effacer_top"]
        TOP[("<b>meilleurs_scores.json</b><br/>Top 3, gardé après redémarrage")]
    end

    NAV["<b>Navigateur de l'admin</b><br/>page HTML + JavaScript<br/>rafraîchie toutes les 0,5 s"]

    M5 -->|"tactile"| GESTES
    GESTES --> LOOP
    LOOP --> PHYS
    NIV --> PHYS
    PHYS -->|"événements : saut, pièce, perdu..."| LOOP
    PHYS --> SON
    LOOP --> DESS
    LOOP <--> MEM
    LOOP <--> RES
    DESS -->|"image"| M5
    SON -->|"notes"| M5
    RES <--> WIFI

    WIFI -->|"UDP port 4210<br/>HELLO, PRG, FIN, BYE"| ECOUTE
    ECOUTE -->|"LB (réponse au HELLO)"| WIFI
    BOUCLE -->|"GO, RK, RES"| WIFI
    WEB -->|"KICK"| WIFI

    ECOUTE --> ETAT
    BOUCLE <--> ETAT
    WEB <--> ETAT
    BOUCLE -->|"fin de partie"| TOP
    NAV <-->|"HTTP + JSON"| WEB
```

**Points clés de l'architecture :**

| Choix | Pourquoi |
|---|---|
| Le jeu tourne **entièrement sur chaque tablette** | Le nombre d'images par seconde ne dépend pas du réseau (REQ-01, REQ-16). |
| Les tablettes ne se parlent **pas entre elles** : elles parlent toutes au serveur | Un seul arbitre décide du départ, de la fin et du classement (REQ-28, REQ-30). |
| Messages **UDP courts** en texte `TYPE\|id\|...` | Simples à lire et à déboguer ; les messages importants sont répétés car le Wi-Fi peut en perdre (REQ-34). |
| **Graine commune** choisie par le serveur | Les bonus sont posés aux mêmes endroits sur toutes les tablettes : course équitable (REQ-31). |
| Découverte par **broadcast** | La tablette n'a pas besoin de connaître l'adresse du PC : elle l'apprend à la première réponse (REQ-27). |
| Physique à **pas fixe** (1/120 s) | Le personnage va à la même vitesse quel que soit le nombre d'images par seconde. |
| Dessin dans un **canvas** puis envoi d'un coup | Pas de scintillement à l'écran. |
| Serveur en **Python standard** | Rien à installer sur le PC à part Python 3. |

---

## 2. Machine à états de la tablette (stm)

```mermaid
stateDiagram-v2
    [*] --> ACCUEIL
    ACCUEIL --> CHOIX_NIVEAU : START
    ACCUEIL --> CHOIX_PERSO : PERSO
    ACCUEIL --> LOBBY_MULTI : MULTIJOUEUR
    CHOIX_PERSO --> CHOIX_NIVEAU : C EST PARTI
    CHOIX_PERSO --> ACCUEIL : retour
    CHOIX_NIVEAU --> JEU : carte de niveau
    CHOIX_NIVEAU --> ACCUEIL : retour
    JEU --> PAUSE : bouton pause (solo)
    PAUSE --> JEU : REPRENDRE / RECOMMENCER
    PAUSE --> ACCUEIL : ACCUEIL
    JEU --> PERDU : monstre touché ou coincé
    JEU --> FIN : drapeau atteint
    PERDU --> JEU : REJOUER (solo)
    PERDU --> CHOIX_NIVEAU : NIVEAUX (solo)
    FIN --> JEU : REJOUER / SUIVANT (solo)
    FIN --> CHOIX_NIVEAU : NIVEAUX (solo)
    LOBBY_MULTI --> COMPTE : message GO du serveur
    LOBBY_MULTI --> ACCUEIL : retour ou KICK
    COMPTE --> JEU : 3 s écoulées
    JEU --> FIN : message RES (l admin arrête)
    FIN --> LOBBY_MULTI : REJOUER (multi)
    PERDU --> LOBBY_MULTI : REJOUER (multi)
    FIN --> ACCUEIL : ACCUEIL / QUITTER (multi)
    PERDU --> ACCUEIL : ACCUEIL / QUITTER (multi)
```

---

## 3. Déroulement d'une partie multijoueur (séquence)

```mermaid
sequenceDiagram
    participant A as Admin (navigateur)
    participant S as serveur_admin.py
    participant T as Tablette (x 1 à 8)

    T->>S: HELLO id perso (broadcast, toutes les 0,5 s)
    S-->>T: LB id nbJoueurs niveau
    A->>S: POST /api/niveau puis POST /api/start
    Note over S: phase salon vers compte, graine tirée au hasard
    loop pendant 3 s, toutes les 0,15 s
        S->>T: GO id niveau graine msRestantes
    end
    Note over T: compte à rebours 3-2-1, bonus posés avec la graine
    loop course
        T->>S: PRG id score avancement (toutes les 0,1 s)
        S-->>T: RK id rang n classement (toutes les 0,2 s)
    end
    T->>S: FIN id score statut temps avancement (3 fois)
    Note over S: tous finis, ou ARRÊTER, ou 5 min : classement final et Top 3
    loop pendant 4 s, toutes les 0,3 s
        S->>T: RES id rang n classement final
    end
    A->>S: GET /api/state (toutes les 0,5 s)
    S-->>A: JSON : phase, joueurs, classement, Top 3
```

## 4. Protocole UDP (port 4210)

| Sens | Message | Rôle |
|---|---|---|
| tablette → serveur | `HELLO\|id\|perso` | « Je suis dans le salon » (toutes les 0,5 s) |
| tablette → serveur | `PRG\|id\|score\|pct` | Avancement pendant la course (10 fois par seconde) |
| tablette → serveur | `FIN\|id\|score\|statut\|temps\|pct` | Fin de course (statut 1 = drapeau, 0 = éliminé) |
| tablette → serveur | `BYE\|id` | Je quitte le multijoueur |
| serveur → tablette | `LB\|id\|joueurs\|niveau` | État du salon |
| serveur → tablette | `GO\|id\|niveau\|graine\|ms` | Départ : niveau, graine des bonus, ms avant le départ |
| serveur → tablette | `RK\|id\|rang\|n\|(nom\|score\|pct)*` | Classement en direct |
| serveur → tablette | `RES\|id\|rang\|n\|(nom\|score\|statut\|temps)*` | Classement final |
| serveur → tablette | `KICK\|id` | L'admin a exclu la tablette |

`id` = les 4 derniers caractères de l'adresse MAC de la tablette.
