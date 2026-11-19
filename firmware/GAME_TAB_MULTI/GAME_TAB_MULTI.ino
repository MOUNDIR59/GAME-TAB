// =====================================================================
// GAME TAB - Version finale : étape 10 (pouvoirs, mode étoile) + mode MULTIJOUEUR (serveur web admin)
//
// Déroulement (comme un jeu mobile) :
//     ACCUEIL ──START──> CHOIX DU NIVEAU ──niveau──> PARTIE ──drapeau──> BRAVO
//        │                     ▲                      │ │
//        └─PERSONNAGE─> CHOIX │           bouton pause│ │monstre touché
//                       DU PERSO                      ▼ ▼
//                              └──────────────── PAUSE   PERDU
//
// Commandes (avec le doigt) :
//     toucher l'écran, ou glisser vers le HAUT -> saut
//     la même chose une 2e fois, en l'air       -> double saut
//     glisser vers le BAS                       -> roulade (pour passer dessous)
// Un seul geste par contact : il faut lever le doigt entre deux gestes.
// Le bonhomme avance tout seul, il ne recule jamais.
// Toucher un petit monstre = partie perdue (game over).
// Pouvoirs à ramasser : des bulles qui apparaissent AU HASARD pendant la
// partie (au sol ou en l'air, il faut alors sauter pour les prendre) :
//     épée étoile -> MODE ÉTOILE pendant 5 s : séquence de transformation
//                    façon manga, il devient un héros (cape, tenue dorée,
//                    aura), le décor devient une forêt de sapins sous la neige
//                    (les caisses deviennent des troncs d'arbre, les monstres
//                    des petits démons) et il coupe tout
//     aimant      -> les pièces viennent toutes seules pendant 8 s
//     bulle       -> elle le protège d'un monstre (une fois)
// Étoiles : 1 = niveau fini, 2 = la moitié des pièces, 3 = 80 % des pièces.
// Records, étoiles et volume restent en mémoire, même tablette éteinte.
//
// MODE MULTIJOUEUR (jusqu'à 8 Tab5 + un PC « serveur admin », tous sur le même Wi-Fi 2,4 GHz) :
//     ACCUEIL ─ MULTIJOUEUR ─> connexion Wi-Fi ─> recherche du serveur ─> SALON.
//     Dans le salon, la tablette attend. C'est l'ADMIN qui, depuis la page web du
//     serveur (serveur_admin.py, sur un PC), choisit le niveau et appuie sur DÉMARRER.
//     Compte à rebours 3-2-1 sur toutes les tablettes, puis tout le monde court en même temps.
//     Les bonus (épée, aimant, bulle) sont posés aux MÊMES endroits partout (graine
//     choisie par le serveur) : la course est équitable.
//     En haut : ton rang, le score du premier et une barre de course avec tous les joueurs.
//     Si on touche un monstre ou si on reste coincé contre un mur, on est éliminé.
//     Le serveur calcule le classement en direct et décide de la fin de la partie.
//     Classement final : arrivé au drapeau, puis le plus de points, puis le plus rapide.
//     L'admin peut aussi exclure un joueur ou arrêter la partie. Pas de pause en multijoueur.
// Astuce : dans Arduino IDE, Outils > Formatage automatique (Ctrl+T) remet
// le code bien aligné.
//
// Plan du programme :
//     1. Réglages          6. Physique, roulade et monstres
//     2. Les niveaux       7. Sons
//     3. Couleurs          8. Dessin
//     4. État du jeu       9. Les écrans
//     5. Gestes (tactile) 6 bis. Multijoueur (réseau)   10. setup() et loop()
// =====================================================================
#include <M5Unified.h>
#include <Preferences.h>   // mémoire qui reste après l'extinction
#include <math.h>
#include <stdarg.h>
#include <WiFi.h>      // Wi-Fi (mode multijoueur)
#include <WiFiUdp.h>   // petits messages entre les deux tablettes

// =====================================================================
// 1. RÉGLAGES
// =====================================================================
const int   ROTATION      = 1;     // même valeur que celle qui marchait à l'étape 1
const bool AFFICHER_FPS = true; // images par seconde en haut à droite
const int   POINTS_PIECE  = 10;

// Mode multijoueur : le réseau Wi-Fi (2,4 GHz) où se connectent les tablettes ET le PC du serveur
// (par exemple le partage de connexion d'un téléphone)
const char*    WIFI_SSID = "NOM_DU_WIFI";        // à remplacer par le nom de votre Wi-Fi 2,4 GHz
const char*    WIFI_MDP  = "MOT_DE_PASSE_WIFI";  // à remplacer par son mot de passe
const uint16_t PORT_JEU  = 4210;
// Broches de la puce Wi-Fi (ESP32-C6) de la Tab5 : ne pas changer
#define SDIO2_CLK GPIO_NUM_12
#define SDIO2_CMD GPIO_NUM_13
#define SDIO2_D0  GPIO_NUM_11
#define SDIO2_D1  GPIO_NUM_10
#define SDIO2_D2  GPIO_NUM_9
#define SDIO2_D3  GPIO_NUM_8
#define SDIO2_RST GPIO_NUM_15

// Mode multijoueur : une ligne du classement (définie ici, tout en haut, car Arduino IDE
// déclare les fonctions avant le reste du code et a besoin de connaître ce type)
const int MAX_JOUEURS = 8;
struct LigneClassement { char nom[16]; int score; int pct; int statut; int temps; };

// Gestes (distances en pixels, 60 px ≈ 5 mm sur l'écran de la Tab5)
const int      SEUIL_GLISSE       = 32;    // glissement reconnu pendant que le doigt bouge
const int      SEUIL_GLISSE_LEVER = 20;    // glissement très rapide : reconnu au lever du doigt
const uint32_t DUREE_TAP          = 250;   // ms : toucher court sans glisser = saut
// Latence : le saut part PENDANT que le doigt est posé, sans attendre qu'il se lève.
// Si le doigt n'a pas bougé de plus de SEUIL_BOUGE px après DELAI_TAP ms, c'est un
// toucher : saut tout de suite. Un glissement vers le bas bouge plus vite que ça.
// (Si la roulade part sans le vouloir : augmente DELAI_TAP ; si le saut paraît
//  encore mou : baisse-le, sans descendre sous 30.)
const uint32_t DELAI_TAP          = 45;    // ms
const int      SEUIL_BOUGE        = 12;    // px
const float    MEMOIRE_SAUT       = 0.20f; // un saut demandé juste avant d'atterrir est gardé 0,2 s
const float    TOLERANCE_BORD     = 0.12f; // on peut encore sauter 0,12 s après avoir quitté un bord

// Physique (pixels et secondes ; y augmente vers le bas)
const float GRAVITE             = 2600;
const float VITESSE_SAUT        = 1080; // saut : environ 220 px (3 caisses et demie)
const float VITESSE_DOUBLE_SAUT = 950;   // 2e saut en l'air : environ 170 px de plus
const float CHUTE_MAX           = 1300;
const float PAS_CALCUL          = 1.0f / 120; // la physique avance par pas de 1/120 s
const float JOUEUR_L          = 44;      // boîte de collision du bonhomme...
const float JOUEUR_H          = 92;      // ...debout
const float JOUEUR_H_ROULADE = 46;       // ...en boule (roulade)
const float DUREE_ROULADE     = 0.7f;    // secondes

// Pouvoirs
const float DUREE_EPEE     = 5.0f;                   // l'épée étoile coupe tout pendant 5 s
const float DUREE_AIMANT   = 8.0f;                   // l'aimant attire les pièces pendant 8 s
const float RAYON_AIMANT   = 300;                    // distance à laquelle l'aimant attire (px)
const int   POINTS_CAISSE = 5;                       // caisse coupée
const int   POINTS_MONSTRE = 20;                     // monstre coupé (ou éclaté par la bulle)
const float DUREE_TRANSFO = 1.0f;                    // séquence de transformation (le jeu est figé)

// Bonus au hasard : le premier arrive après 4 s, puis un toutes les 7 à 12 s.
// Sur 10 bonus : 4 épées, 3 aimants, 3 bulles (environ).
const float BONUS_PREMIER   = 2.0f;   // le 1er bonus arrive vite (toujours l'épée étoile)
const float BONUS_ECART_MIN = 7.0f;
const float BONUS_ECART_MAX = 12.0f;

// =====================================================================
// 2. LES NIVEAUX : chaque caractère est une case de 60 x 60 pixels
//     . vide       # sol         B caisse      = plateforme
//     o pièce      P départ      F drapeau d'arrivée
//     m petit monstre au sol (il fait des allers-retours)
//     v abeille qui vole (mettre sur la ligne 8 : on passe dessous en roulade)
// Les bonus arrivent au hasard. On peut aussi en poser un à un endroit
// fixe avec une lettre sur la ligne 9 (là où il court) :
//     E épée étoile       A aimant à pièces       U bulle qui protège
// Toutes les lignes d'un niveau doivent avoir la même longueur.
// Les obstacles font 2 cases de haut  : un seul saut suffit.
//     Coincé contre une caisse ou un mur, il recule à l'écran et sort par la gauche = PERDU.
// Un saut monte de 3 cases ; un double saut de 6 cases.
// Debout, il faut 2 cases libres pour passer ; en roulade, 1 seule suffit.
// =====================================================================
const int CASE   = 60;
const int LIGNES = 12;           // 12 x 60 = 720 px, toute la hauteur de l'écran

// ----- Niveau 1 : facile, pour apprendre (saut, monstre, double saut, roulade, abeille)
const char* const NIVEAU_1[LIGNES] = {
 "..........................................................................................................",
 "..........................................................................................................",
 "..........................................................................................................",
 "..........................................................................................................",
 "..........................................................................................................",
 "............................................oo............................................................",
 "...................................................................oooo...................................",
 "........................o.............o.............BBB............====............o......................",
 "............o.....o....o.o............B.....BB......BBB......v.....................B.....v................",
 "..P..ooo....B.....B............m.....BB.....BB......ooo.....o.o.............m.....BB.........ooo....F.....",
 "##########################################################################################################",
 "##########################################################################################################",
};

// ----- Niveau 2 : moyen
const char* const NIVEAU_2[LIGNES] = {
 "............................................................................................................................................",
 "............................................................................................................................................",
 "............................................................................................................................................",
 "................................................................................................ooo.........................................",
 "..........................oooo.......................o..........................................===......o..................................",
 "..........................====......................................oooo....................................................................",
 "......................o...............................................m.....................................................................",
 "...........o...........................BBBB.........................====.....oo....BBBB.....................................................",
 "...........B..........B...........v....BBBB.........BB......v...v............BB....BBBB.................BB......v...........v...............",
 "..P..ooo...B.....m....B................oooo....m....BB..............oooo.....BB....oooo.....m..........BBB............m.........ooo...F.....",
 "############################################################################################################################################",
 "############################################################################################################################################",
};

// ----- Niveau 3 : difficile
const char* const NIVEAU_3[LIGNES] = {
 "................................................................................................................................................................",
 "................................................................................................................................................................",
 ".......................................................................ooo......................................................................................",
 ".......................................................................===.......................o..............................................................",
 ".................o..............................oo..............................................................................................................",
 "..................................................................ooo...........................................................................................",
 "..................................................................===..................................................o........................................",
 ".............................BBBBB............................................BBB.......BBB.......................................BBBBB.........................",
 "................BB......v....BBBBB..............BB......v..v..v...............BBB...v...BBB....BBB.............v.......B.....v....BBBBB.........v...............",
 "..P..oo....m....BB...........ooooo....m....m....BB............................ooo.......ooo....BBB.......m........m....B..........ooooo....m........ooo...F.....",
 "################################################################################################################################################################",
 "################################################################################################################################################################",
};

// Astuces affichées en bas de l'écran quand le bonhomme approche d'une colonne
struct Astuce { int colonne; const char* texte; };
const Astuce ASTUCES_1[] = {
   { 12, "Touche l'écran (ou glisse ↑) pour sauter !" },
   { 31, "Saute par-dessus les petits monstres !" },
   { 44, "Saute encore une fois en l'air : double saut !" },
   { 52, "Glisse ↓ pour faire une roulade !" },
   { 61, "Roulade sous les abeilles !" },
};
const Astuce ASTUCES_2[] = { { 8, "Touche : saut     2 fois : double saut   ↓ : roulade" } };
const Astuce ASTUCES_3[] = { { 8, "Attention, ça va vite !" } };

// Liste des niveaux : carte, nom, couleur, vitesse de course (px/s),
// distance des allers-retours des monstres (px), astuces
struct InfoNiveau {
   const char* const* carte; const char* nom; uint32_t couleur;
   float vitesse; float bougeMonstres; const Astuce* astuces; int nbAstuces;
};
#define NB_ASTUCES(a) (sizeof(a) / sizeof(a[0]))
const InfoNiveau NIVEAUX[] = {
   { NIVEAU_1, "Facile",    0x8BD448, 290, 15, ASTUCES_1, NB_ASTUCES(ASTUCES_1) },
   { NIVEAU_2, "Moyen",     0xFF9F1C, 330, 45, ASTUCES_2, NB_ASTUCES(ASTUCES_2) },
   { NIVEAU_3, "Difficile", 0xE8304F, 360, 55, ASTUCES_3, NB_ASTUCES(ASTUCES_3) },
};
const int NB_NIVEAUX = sizeof(NIVEAUX) / sizeof(NIVEAUX[0]);

// =====================================================================
// 3. COULEURS (reprises du logo Game Tab)
// =====================================================================
const uint32_t MARINE     = 0x1B1036; // contours
const uint32_t CIEL_HAUT = 0x7FD8FF;
const uint32_t CIEL_BAS   = 0xC8F1FF;
const uint32_t COLLINE    = 0xA7E8A0;
const uint32_t HERBE      = 0x8BD448;
const uint32_t HERBE_FONCE= 0x6BB52C;
const uint32_t TERRE      = 0xC97B3D;
const uint32_t TERRE_FONCE= 0xA9612B;
const uint32_t CAISSE     = 0xF2A541;
const uint32_t CAISSE_FONCE = 0xC47A2C;
const uint32_t OR         = 0xFFD23F;
const uint32_t OR_CLAIR   = 0xFFF07A;
const uint32_t ROUGE      = 0xE8304F;
const uint32_t ROSE       = 0xFF5E9C;
const uint32_t ROSE_JOUE = 0xFF9EC4;    // joues des monstres
const uint32_t MAUVE      = 0xB98CF5;   // petit monstre
const uint32_t TURQUOISE = 0x2EC4F1;
const uint32_t GRIS       = 0xC9CCD6;
const uint32_t GRIS_FONCE = 0x4A5160;   // lame de course
const uint32_t BLANC      = 0xFFFFFF;
const uint32_t BOIS       = 0x8B5A2B;   // manche de l'épée
const uint32_t BLEU_BULLE = 0xAEEBFF;   // bulle qui protège
const uint32_t ROSE_PALE = 0xFFD6E5;    // fond du bonus aimant
const uint32_t BLEU_PALE = 0xC8F1FF;    // fond du bonus bulle
// Attention : toujours passer les couleurs par un nom comme ci-dessus.
// Un nombre écrit directement (ex. 0xAEEBFF) n'est pas lu de la même
// façon par la bibliothèque et donne une autre couleur.

// ----- Le mode étoile (pendant l'épée) : tenue du héros et forêt sous la neige
const uint32_t HEROS_HAUT         = 0xFFD23F; // haut doré
const uint32_t HEROS_BAS          = 0x5B3FD1; // pantalon violet
const uint32_t HEROS_CAPE         = 0x6C4DE6; // cape violette
const uint32_t FORET_CIEL_HAUT    = 0x0B1526; // nuit bleue (en haut)
const uint32_t FORET_CIEL_BAS     = 0x41597A; // brume (vers le sol)
const uint32_t BRUME              = 0x6E86A5; // brume au pied des arbres
const uint32_t SAPIN_LOIN         = 0x51698A; // sapins au loin, pâles dans la brume
const uint32_t NEIGE_LOIN         = 0x9DB2CC;
const uint32_t SAPIN_PRES         = 0x1D3F45; // sapins plus proches, vert sombre
const uint32_t NEIGE              = 0xF4F8FF; // l'herbe devient de la neige
const uint32_t NEIGE_OMBRE        = 0xC3D3E8;
const uint32_t TERRE_GELEE        = 0x3A3A55;
const uint32_t TERRE_GELEE_FONCE = 0x2B2B42;
const uint32_t ECORCE             = 0x6B4A33; // les caisses deviennent des troncs
const uint32_t ECORCE_FONCE       = 0x4A3122;
const uint32_t ECORCE_CLAIRE      = 0x8C6547;
const uint32_t DEMON              = 0xE0475B; // petit démon au sol (rouge)
const uint32_t DEMON_CLAIR        = 0xFF9A9A;
const uint32_t DEMON_VOLANT       = 0x9B5DE5; // petit démon qui vole (violet)
const uint32_t DEMON_AILE          = 0x5B2A86;
const uint32_t CORNE               = 0xFFF1D6;

// Un « thème » : toutes les couleurs du décor d'un coup
struct Theme {
   uint32_t cielHaut, cielBas, colline, herbe, herbeFonce, terre, terreFonce, caisse, caisseFonce;
   bool neige;                 // true : forêt sous la neige à la place des collines
};
const Theme THEME_JOUR = { CIEL_HAUT, CIEL_BAS, COLLINE, HERBE, HERBE_FONCE,
                             TERRE, TERRE_FONCE, CAISSE, CAISSE_FONCE, false };
const Theme THEME_FORET = { FORET_CIEL_HAUT, FORET_CIEL_BAS, SAPIN_LOIN, NEIGE, NEIGE_OMBRE,
                             TERRE_GELEE, TERRE_GELEE_FONCE, ECORCE, ECORCE_FONCE, true };
const Theme* theme = &THEME_JOUR;      // le décor utilisé en ce moment

// =====================================================================
// 3 bis. LES PERSONNAGES
// Ils ont tous la même taille et sautent pareil : seul le dessin change.
// Pour en ajouter un : une ligne de plus dans PERSOS (8 au maximum
// tiennent sur l'écran de choix).
// =====================================================================
enum Coiffure { CASQUETTE, CHEVEUX_COURTS, BOUCLES, MACARONS, QUEUE_DE_CHEVAL, FOULARD, ANTENNES };

struct Perso {
   const char* nom;
   uint32_t peau, cheveux, haut, bas, chaussures;   // cheveux : couleur des cheveux,
   Coiffure coiffure;                               // de la casquette ou du foulard
   bool lunettes;
   bool short_;          // jambes visibles sous le genou
   bool lameDeCourse;    // prothèse de course à la jambe avant
};

const Perso PERSOS[] = {
   // nom       peau      cheveux   haut      bas       chaussures coiffure          lunettes short lame
   { "Pixo",   0xFFCB9A, 0x2EC4F1, 0xFF9F1C, 0x3A5BD9, 0xFF3D5A, CASQUETTE,        false, false, false },
   { "Pépite", 0x8D5524, 0x2B1B12, 0xFF5E9C, 0x7B4FE0, 0xFFD23F, MACARONS,         false, false, false },
   { "Plume", 0xF6D7B0, 0x22202E, 0x2EC4A0, 0x3A5BD9, 0xFFFFFF, QUEUE_DE_CHEVAL,   false, false, false },
   { "Kiwi",   0x5C3A21, 0x1A1210, 0xFFD23F, 0x2E8B57, 0xE8304F, CHEVEUX_COURTS,   false, true, false },
   { "Comète", 0xC68642, 0xB98CF5, 0x3A5BD9, 0x4A4F8C, 0xFF9F1C, FOULARD,          false, false, false },
   { "Flip",   0xE0AC69, 0x6B3E1F, 0xE8304F, 0x34495E, 0x2EC4F1, BOUCLES,          true, false, false },
   { "Fusée", 0xFCDCC8, 0xD9632B, 0x6BB52C, 0x3A5BD9, 0xFF3D5A, CHEVEUX_COURTS,    false, true, true },
   { "Zip",    0x7CF29A, 0xFF5E9C, 0xB98CF5, 0xB98CF5, 0x2EC4F1, ANTENNES,         false, false, false },
};
const int NB_PERSOS = sizeof(PERSOS) / sizeof(PERSOS[0]);

// =====================================================================
// 4. ÉTAT DU JEU
// =====================================================================
// L'image est d'abord dessinée dans une zone mémoire (le « canvas »),
// puis envoyée d'un coup à l'écran : pas de scintillement.
// Le canvas a la forme naturelle de l'écran (720 x 1280, en portrait) :
// c'est le cas où la copie vers l'écran est la plus rapide. On le fait
// pivoter avec setRotation() pour dessiner en paysage (1280 x 720).
M5Canvas ecran(&M5.Display);
Preferences memoire;

// Les 2 derniers états sont ceux du mode multijoueur
enum Etat { ACCUEIL, CHOIX_PERSO, CHOIX_NIVEAU, JEU, PAUSE, PERDU, FIN,
            LOBBY_MULTI, COMPTE };
Etat etat = ACCUEIL;

struct Piece { float x, y; bool prise; };
const int PIECES_MAX = 120;
Piece pieces[PIECES_MAX];
int   nbPieces = 0;

// Un monstre : position de départ, type ('m' au sol, 'v' volant), décalage
struct Monstre { float x0, y0; char type; float phase; bool vivant; };
const int MONSTRES_MAX = 30;
Monstre monstres[MONSTRES_MAX];
int   nbMonstres = 0;

// Un bonus à ramasser : 'E' (épée étoile), 'A' (aimant) ou 'U' (bulle)
struct Bonus { float x, y; char type; bool pris; };
const int BONUS_MAX = 10;
Bonus bonus[BONUS_MAX];
int   nbBonus = 0;

// Petits morceaux qui volent quand l'épée coupe quelque chose
struct Eclat { float x, y, vx, vy, vie; uint32_t couleur; };
const int ECLATS_MAX = 80;
Eclat eclats[ECLATS_MAX];
int   nbEclats = 0;

// Onomatopées façon manga (« TCHAK ! », « SHING ! ») qui apparaissent
// quand l'épée coupe quelque chose
struct Onomatopee { float x, y, vie; const char* texte; uint32_t couleur; };
const int ONOMATOPEES_MAX = 6;
Onomatopee onomatopees[ONOMATOPEES_MAX];
int   nbOnomatopees = 0;

// Copie du niveau en cours dans la mémoire vive :
// l'épée peut y effacer des caisses (les niveaux d'origine ne changent pas).
const int COLONNES_MAX = 200;
char grille[LIGNES][COLONNES_MAX + 1];

int   numeroNiveau = 0;                // 0 = niveau 1
int   colonnes;                        // largeur du niveau en cases
int   colonneArrivee;
float departX, departY;
float vitesseCourse;
float bougeMonstres;

// le bonhomme
float joueurX, joueurY;    // coin haut-gauche de sa boîte de collision
float hauteurJoueur;       // JOUEUR_H debout, JOUEUR_H_ROULADE en boule
float vitesseY;
bool auSol;
bool doubleSautDispo;      // redevient vrai à chaque atterrissage
float memoireSaut;         // saut demandé, pas encore fait (voir MEMOIRE_SAUT)
float memoireRoulade;      // une roulade demandée en l'air se fait à l'atterrissage
float tolerance;           // temps restant pour sauter après avoir quitté un bord
float roulade;             // temps de roulade restant (0 = debout)
float animation;           // fait bouger les jambes quand il court
float nuageX, nuageY, nuageT;   // petit nuage laissé par le double saut

// les pouvoirs en cours
float epee;                    // temps d'épée restant (0 = pas d'épée)
float aimant;                  // temps d'aimant restant
bool bulleProtege;             // la bulle le protège (jusqu'au prochain monstre)
float invincible;              // juste après que la bulle a éclaté : 1 s sans danger
float coup;                    // animation du coup d'épée
char annonce;                  // bonus qui vient d'être pris (pour le grand texte)
float annonceT;                // temps d'affichage restant du grand texte
float flash;                   // éclair blanc quand il se transforme (et à la fin)
float transfoT;                // explosion d'étoiles au moment de la transformation
float sequenceTransfo;         // temps restant de la séquence manga (jeu figé)
float prochainBonus;           // temps avant le prochain bonus au hasard
bool  premierBonusPose;        // le tout premier bonus d'une partie est toujours l'épée étoile
uint32_t graine = 12345;       // pour tirer au hasard (changée à chaque partie)

float camera;              // position du bord gauche de l'écran dans le niveau
float tempsJeu;            // temps de la partie (s'arrête pendant la pause)
int   score, piecesPrises;

// Ce qui est gardé en mémoire (voir setup et finirNiveau)
int   records[NB_NIVEAUX];         // meilleur score de chaque niveau
int   etoiles[NB_NIVEAUX];         // meilleures étoiles de chaque niveau (0 à 3)
int   niveauSon = 3;               // volume : 0 (muet) à 5
int   sonAvantMuet = 3;
int   persoChoisi = 0;             // numéro dans PERSOS

// Fin de partie
int   etoilesPartie, etoilesAffichees;
bool nouveauRecord, fanfareJouee;
float tempsFin;
float yChute, vitesseChute;        // le bonhomme qui tombe quand il a perdu

float reste;               // temps pas encore calculé par la physique
float horloge;             // temps qui passe toujours (animations)
uint32_t dernierMs;
int   fps, imagesCompteur;
uint32_t debutSeconde;

// Ce que le doigt a fait (voir la partie 5)
enum Glissement { RIEN, VERS_LE_HAUT, VERS_LE_BAS };

// Une note de musique : fréquence en Hz, durée en millisecondes
struct Note { uint16_t frequence; uint16_t duree; };

// Un bouton rectangulaire à l'écran
struct Bouton { int x, y, l, h; const char* texte; uint32_t couleur; };
const Bouton BOUTON_START       = { 520, 335, 400, 120, "START",        ROSE };
const Bouton BOUTON_PERSO       = { 300, 485, 250,  85, "PERSO",       TURQUOISE };
const Bouton BOUTON_MULTI       = { 580, 485, 400,  85, "MULTIJOUEUR", OR };
const Bouton BOUTON_RETOUR      = { 30, 24, 100, 90, "",                TURQUOISE };
const Bouton BOUTON_OK          = { 490, 618, 300, 84, "C'EST PARTI", ROSE };
const Bouton BOUTON_PAUSE       = {1176, 14, 88, 76, "",                TURQUOISE };
const Bouton BOUTON_REPRENDRE   = { 440, 180, 400, 90, "REPRENDRE",     ROSE };
const Bouton BOUTON_MUET        = { 440, 300, 90, 80, "",               TURQUOISE };
const Bouton BOUTON_MOINS       = { 545, 300, 90, 80, "-",              TURQUOISE };
const Bouton BOUTON_PLUS        = { 750, 300, 90, 80, "+",              TURQUOISE };
const Bouton BOUTON_RECOMMENCER = { 440, 410, 400, 80, "RECOMMENCER", TURQUOISE };
const Bouton BOUTON_ACCUEIL     = { 440, 520, 400, 80, "ACCUEIL",       OR };
const Bouton BOUTON_NIVEAUX     = { 140, 430, 210, 80, "NIVEAUX",       TURQUOISE };
const Bouton BOUTON_REJOUER     = { 375, 430, 210, 80, "REJOUER",       ROSE };
const Bouton BOUTON_SUIVANT     = { 610, 430, 210, 80, "SUIVANT",       HERBE };
const Bouton BOUTON_NIVEAUX_P   = { 380, 440, 240, 85, "NIVEAUX",       TURQUOISE };
const Bouton BOUTON_REJOUER_P   = { 660, 440, 240,  85, "REJOUER",     ROSE };

// Mode multijoueur
const Bouton BOUTON_REESSAYER   = { 440, 360, 400, 100, "RÉESSAYER",   ROSE };
const Bouton BOUTON_ACCUEIL_M   = { 330, 490, 280,  80, "ACCUEIL",     OR };
const Bouton BOUTON_REJOUER_M   = { 670, 490, 280,  80, "REJOUER",     ROSE };
const Bouton BOUTON_QUITTER_M   = { 440, 490, 400,  80, "QUITTER",     OR };

bool dansBouton(const Bouton& b, int x, int y) {
  return x >= b.x && x < b.x + b.l && y >= b.y && y < b.y + b.h;
}

// La carte d'un niveau sur l'écran de choix (une par niveau)
Bouton carteNiveau(int i) {
  Bouton b = { 100 + i * 370, 180, 340, 380, "", BLANC };
  return b;
}

// La carte d'un personnage sur l'écran de choix (4 par ligne)
Bouton cartePerso(int i) {
  Bouton b = { 70 + (i % 4) * 290, 120 + (i / 4) * 250, 270, 230, "", BLANC };
  return b;
}

// =====================================================================
// 5. GESTES
// Un seul geste par contact du doigt (il faut lever le doigt entre deux
// gestes : un long glissement ne déclenche donc pas saut + double saut).
//   - pendant que le doigt bouge : dès qu'il a glissé de SEUIL_GLISSE px
//     vers le haut ou le bas, le geste part tout de suite ;
//   - quand le doigt se lève : un glissement très rapide (même court)
//     compte aussi, et un simple toucher court fait sauter.
// Un glissement en diagonale est accepté s'il monte (ou descend) au moins
// un peu : il faut juste qu'il ne soit pas presque horizontal.
// =====================================================================
bool doigtPose = false;     // le doigt est sur l'écran
bool gesteFait = false;     // un geste a déjà été reconnu pendant ce contact
int doigtX0, doigtY0;       // où le doigt s'est posé
int doigtX, doigtY;         // dernière position connue du doigt
uint32_t doigtT0;           // quand il s'est posé (ms)

// Direction d'un déplacement (dx, dy), s'il est assez grand
Glissement sensDuGeste(int dx, int dy, int seuil) {
  if (abs(dy) < seuil) return RIEN;
  if (abs(dy) * 10 < abs(dx) * 6) return RIEN;    // presque horizontal : ignoré
  return dy < 0 ? VERS_LE_HAUT : VERS_LE_BAS;      // y diminue vers le haut
}

Glissement lireGlissement(bool appuye, int x, int y, uint32_t ms) {
  if (appuye && !doigtPose) {                       // le doigt vient de se poser
    doigtPose = true; gesteFait = false;
    doigtX0 = doigtX = x; doigtY0 = doigtY = y; doigtT0 = ms;
    return RIEN;
  }
  if (appuye) {                                     // le doigt reste posé
    doigtX = x; doigtY = y;
    if (gesteFait) return RIEN;
    Glissement g = sensDuGeste(x - doigtX0, y - doigtY0, SEUIL_GLISSE);
    if (g == RIEN && ms - doigtT0 >= DELAI_TAP &&
        abs(x - doigtX0) < SEUIL_BOUGE && abs(y - doigtY0) < SEUIL_BOUGE)
      g = VERS_LE_HAUT;                             // toucher : saut sans attendre le lever
    if (g != RIEN) gesteFait = true;
    return g;
  }
  if (doigtPose) {                                  // le doigt vient de se lever
    doigtPose = false;
    if (gesteFait) return RIEN;
    int dx = doigtX - doigtX0, dy = doigtY - doigtY0;
    Glissement g = sensDuGeste(dx, dy, SEUIL_GLISSE_LEVER);
    if (g != RIEN) return g;                        // glissement très rapide
    if (ms - doigtT0 < DUREE_TAP && abs(dx) < SEUIL_GLISSE && abs(dy) < SEUIL_GLISSE)
      return VERS_LE_HAUT;                          // toucher très court : saut
  }
  return RIEN;
}

// Ce toucher-là (sur un bouton) ne doit pas compter comme un geste
void ignorerCeToucher(int x, int y) {
  doigtPose = true; gesteFait = true;
  doigtX0 = doigtX = x; doigtY0 = doigtY = y;
}

// Le tactile donne des coordonnées en portrait (720 x 1280).
// On les convertit en paysage (1280 x 720), dans le même sens que l'image.
void versPaysage(int px, int py, int& x, int& y) {
  if (ROTATION == 1) { x = py;        y = 719 - px; }
  else               { x = 1279 - py; y = px; }
}

// =====================================================================
// 6. PHYSIQUE, ROULADE ET MONSTRES
// =====================================================================
char caseEn(int col, int lig) {
  if (col < 0 || col >= colonnes || lig < 0 || lig >= LIGNES) return '.';
  return grille[lig][col];
}

// Une case bloque-t-elle le passage ? Les bords du niveau sont des murs.
bool solide(int col, int lig) {
  if (col < 0 || col >= colonnes) return true;
  char c = caseEn(col, lig);
  return c == '#' || c == 'B' || c == '=';
}

int versCase(float v) { return (int)floorf(v / CASE); }

// Y a-t-il une case solide dans ce rectangle ?
bool zoneOccupee(float x, float y, float l, float h) {
  for (int c = versCase(x); c <= versCase(x + l - 0.01f); c++)
    for (int lg = versCase(y); lg <= versCase(y + h - 0.01f); lg++)
      if (solide(c, lg)) return true;
  return false;
}

// Avance vers la droite. S'il entre dans une caisse, on le recolle
// contre elle (voir contreMur).
// Si le bonhomme est collé contre un mur ou une caisse, contreMur passe à true.
// L'écran continue d'avancer : s'il sort par la gauche sans avoir sauté, c'est PERDU.
// (Si seul le bout de ses pieds touche le coin d'une caisse, ce n'est pas un choc.)
bool contreMur = false;
float decalageMur = 0;             // de combien l'écran a avancé sans lui (px) : il est coincé contre un mur
const float RETOUR_CAMERA = 500;   // vitesse (px/s) à laquelle l'écran le rattrape quand il est libéré
const float TOLERANCE_COIN = 24;   // px : en dessous, on considère qu'il atterrit sur la caisse

void avancerX(float dx) {
  joueurX += dx;
  int l0 = versCase(joueurY), l1 = versCase(joueurY + hauteurJoueur - 0.01f);
  int c = versCase(joueurX + JOUEUR_L - 0.01f);
  for (int l = l0; l <= l1; l++)
    if (solide(c, l)) {
      joueurX = c * CASE - JOUEUR_L;
      if (joueurY + hauteurJoueur - l * CASE > TOLERANCE_COIN) contreMur = true;
      return;
    }
}

// Avance en y. Renvoie true s'il a touché le sol ou un plafond.
bool deplacerY(float dy) {
  joueurY += dy;
  int c0 = versCase(joueurX), c1 = versCase(joueurX + JOUEUR_L - 0.01f);
  if (dy > 0) {
    int l = versCase(joueurY + hauteurJoueur - 0.01f);
    for (int c = c0; c <= c1; c++)
      if (solide(c, l)) { joueurY = l * CASE - hauteurJoueur; return true; }
  } else if (dy < 0) {
    int l = versCase(joueurY);
    for (int c = c0; c <= c1; c++)
      if (solide(c, l)) { joueurY = (l + 1) * CASE; return true; }
  }
  return false;
}

// ----- Roulade : le bonhomme se met en boule (sa boîte devient 2 fois
//       moins haute), en gardant les pieds au même endroit.
const float ECART_ROULADE = JOUEUR_H - JOUEUR_H_ROULADE;

void commencerRoulade() {
  if (roulade <= 0) joueurY += ECART_ROULADE;
  roulade = DUREE_ROULADE;
  hauteurJoueur = JOUEUR_H_ROULADE;
}

// Il ne peut se relever que s'il y a la place au-dessus de lui
// (pas de caisse, et pas d'abeille juste au-dessus de sa tête)
bool peutSeRelever() {
  float y = joueurY - ECART_ROULADE;
  return !zoneOccupee(joueurX, y, JOUEUR_L, JOUEUR_H) &&
         !monstreDansZone(joueurX, y, JOUEUR_L, JOUEUR_H);
}

void seRelever() {
  joueurY -= ECART_ROULADE;
  roulade = 0;
  hauteurJoueur = JOUEUR_H;
}

// Position (centre) d'un monstre à cet instant
void positionMonstre(const Monstre& m, float& x, float& y) {
  if (m.type == 'm') {      // au sol : allers-retours et petits rebonds
    x = m.x0 + bougeMonstres * sinf(tempsJeu * 1.6f + m.phase);
    y = m.y0 - 6 * fabsf(sinf(tempsJeu * 7 + m.phase));
  } else {                  // abeille : flotte de haut en bas
    x = m.x0;
    y = m.y0 + 8 * sinf(tempsJeu * 3 + m.phase);
  }
}

// Numéro du monstre (encore là) qui touche ce rectangle, ou -1 s'il n'y en a pas.
// (les boîtes des monstres sont un peu plus petites que leurs dessins :
// on ne perd pas pour un pixel)
int monstreTouche(float x, float y, float l, float h) {
  for (int i = 0; i < nbMonstres; i++) {
    if (!monstres[i].vivant) continue;
    float mx, my;
    positionMonstre(monstres[i], mx, my);
    float ml = 18, mh = (monstres[i].type == 'm') ? 15 : 16;   // demi-largeur, demi-hauteur
    if (x < mx + ml && x + l > mx - ml && y < my + mh && y + h > my - mh) return i;
  }
  return -1;
}

bool monstreDansZone(float x, float y, float l, float h) {
  return monstreTouche(x, y, l, h) >= 0;
}

// Événements renvoyés par un pas de calcul (pour les sons)
const uint16_t EV_SAUT = 1, EV_DOUBLE_SAUT = 2, EV_ROULADE = 4,
               EV_PIECE = 8, EV_ARRIVEE = 16, EV_TOUCHE = 32,
               EV_COUPE = 64, EV_BONUS = 128, EV_BULLE = 256, EV_FIN_POUVOIR = 512;

// ----- Tirages au hasard
// Un nombre entier entre 0 et n - 1
int hasardEntier(int n) {
  graine = graine * 1103515245 + 12345;
  return (int)((graine >> 16) % n);
}

// Un nombre à virgule entre -n et n (pour les éclats)
float hasard(float n) {
  return (hasardEntier(2001) - 1000) / 1000.0f * n;
}

// Un nombre à virgule entre a et b
float hasardEntre(float a, float b) {
  return a + (b - a) * hasardEntier(1001) / 1000.0f;
}

// Une onomatopée manga à cet endroit du niveau (pas deux en même temps :
// si une vient d'apparaître il y a moins de 0,2 s, on n'en remet pas)
void ajouterOnomatopee(float x, float y, const char* texte, uint32_t couleur) {
  for (int i = 0; i < nbOnomatopees; i++)
    if (onomatopees[i].vie > 0.4f) return;
  if (nbOnomatopees >= ONOMATOPEES_MAX) nbOnomatopees--;   // plein : on remplace la dernière
  Onomatopee o = { x + 30, y - 30, 0.6f, texte, couleur };
  onomatopees[nbOnomatopees++] = o;
}

// Fait apparaître un bonus au hasard, juste après le bord droit de l'écran,
// sur le chemin du bonhomme (au sol, ou en l'air : il faut alors sauter)
void ajouterBonusAuHasard() {
  int n = 0;                                    // on enlève les bonus déjà pris
  for (int i = 0; i < nbBonus; i++)             // ou déjà dépassés
    if (!bonus[i].pris && bonus[i].x > camera - 100) bonus[n++] = bonus[i];
  nbBonus = n;
  if (nbBonus >= BONUS_MAX) return;
  int c0 = versCase(camera + 1280) + 1;
  for (int c = c0; c < c0 + 8 && c < colonneArrivee - 2; c++) {
    if (solide(c, 8) || solide(c, 9) || !solide(c, 10)) continue;   // pas une bonne place
    int t = hasardEntier(10);                   // quel pouvoir ?
    char type = (t < 4) ? 'E' : (t < 7) ? 'A' : 'U';
    if (!premierBonusPose) type = 'E';          // le 1er bonus : toujours l'épée étoile
    else if (type == 'E' && epee > 0) type = 'A';   // déjà en mode étoile
    if (type == 'U' && bulleProtege) type = 'A';// déjà une bulle
    bool enLair = hasardEntier(10) < 4 && !solide(c, 6) && !solide(c, 7);
    if (!premierBonusPose) enLair = false;      // le 1er est au sol, facile à prendre
    premierBonusPose = true;
    Bonus b = { c * CASE + CASE / 2.0f, (enLair ? 6 : 9) * CASE + CASE / 2.0f, type, false };
    bonus[nbBonus++] = b;
    return;
  }
}

// Fait voler quelques éclats depuis (x, y), de deux couleurs
void ajouterEclats(float x, float y, uint32_t c1, uint32_t c2, int nombre) {
  for (int i = 0; i < nombre && nbEclats < ECLATS_MAX; i++) {
    Eclat e = { x + hasard(15), y + hasard(15), 120 + hasard(260), -480 + hasard(280),
                0.6f + hasard(0.15f), (i % 2) ? c1 : c2 };
    eclats[nbEclats++] = e;
  }
}

void bougerEclats(float dt) {
  for (int i = 0; i < nbEclats; ) {
    Eclat& e = eclats[i];
    e.vie -= dt;
    e.vy += 1800 * dt;
    e.x += e.vx * dt;
    e.y += e.vy * dt;
    if (e.vie <= 0) eclats[i] = eclats[--nbEclats];   // fini : on met le dernier à sa place
    else i++;
  }
}

// ÉPÉE ÉTOILE : coupe les caisses et les monstres autour de lui, surtout
// devant (on coupe avant qu'il ne se cogne). Le sol et les plateformes
// ne se coupent pas, ni ce qui est sous ses pieds.
uint16_t couperAvecEpee() {
  uint16_t ev = 0;
  float x0 = joueurX - 6, x1 = joueurX + JOUEUR_L + 40;
  float y0 = joueurY - 24, y1 = joueurY + hauteurJoueur - 2;
  for (int c = versCase(x0); c <= versCase(x1); c++)
    for (int l = versCase(y0); l <= versCase(y1); l++)
      if (caseEn(c, l) == 'B') {
        // la caisse est coupée, et aussi toutes celles empilées au-dessus
        // (sinon elles resteraient à flotter dans le vide)
        for (int k = l; k >= 0 && caseEn(c, k) == 'B'; k--) {
          grille[k][c] = '.';
          ajouterEclats(c * CASE + CASE / 2, k * CASE + CASE / 2, ECORCE_CLAIRE, NEIGE, 4);
          score += POINTS_CAISSE;
        }
        ajouterOnomatopee(c * CASE + CASE / 2, l * CASE, "TCHAK !", BLANC);
        ev |= EV_COUPE;
      }
  int i;
  while ((i = monstreTouche(x0, y0, x1 - x0, y1 - y0 + 2)) >= 0) {
    float mx, my;
    positionMonstre(monstres[i], mx, my);
    monstres[i].vivant = false;
    ajouterEclats(mx, my, monstres[i].type == 'm' ? DEMON : DEMON_VOLANT, BLANC, 8);
    ajouterOnomatopee(mx, my - 40, "SHING !", OR);
    score += POINTS_MONSTRE;
    ev |= EV_COUPE;
  }
  if (ev) coup = 0.25f;                              // petit coup d'épée à l'écran
  return ev;
}

// Un pas de calcul de 1/120 s
uint16_t unPas() {
  const float dt = PAS_CALCUL;
  uint16_t ev = 0;
  tempsJeu += dt;
  if (memoireSaut > 0)    memoireSaut -= dt;
  if (memoireRoulade > 0) memoireRoulade -= dt;
  if (tolerance > 0)      tolerance -= dt;
  if (nuageT > 0)         nuageT -= dt;
  if (invincible > 0)     invincible -= dt;
  if (coup > 0)           coup -= dt;
  if (annonceT > 0)       annonceT -= dt;
  if (flash > 0)          flash -= dt;
  if (transfoT > 0)       transfoT -= dt;
  for (int i = 0; i < nbOnomatopees; ) {             // les onomatopées s'effacent
    onomatopees[i].vie -= dt;
    if (onomatopees[i].vie <= 0) onomatopees[i] = onomatopees[--nbOnomatopees];
    else i++;
  }
  prochainBonus -= dt;                               // un bonus au hasard ?
  if (prochainBonus <= 0) {
    prochainBonus = hasardEntre(BONUS_ECART_MIN, BONUS_ECART_MAX);
    ajouterBonusAuHasard();
  }
  if (epee > 0) {
    epee -= dt;
    if (epee <= 0) {                                 // fin du mode étoile
      ev |= EV_FIN_POUVOIR;
      flash = 0.2f;
    }
  }
  if (aimant > 0) {
    aimant -= dt;
    if (aimant <= 0) ev |= EV_FIN_POUVOIR;
  }
  bougerEclats(dt);

  // ----- Saut et double saut
  if (memoireSaut > 0) {
    if (auSol || tolerance > 0) {                    // saut normal
      if (roulade > 0 && peutSeRelever()) seRelever();
      if (roulade <= 0) {
        vitesseY = -VITESSE_SAUT;
        auSol = false; tolerance = 0; memoireSaut = 0;
        doubleSautDispo = true;
        ev |= EV_SAUT;
      }
    } else if (doubleSautDispo && roulade <= 0) {    // double saut, en l'air
      vitesseY = -VITESSE_DOUBLE_SAUT;
      doubleSautDispo = false; memoireSaut = 0;
      nuageX = joueurX + JOUEUR_L / 2; nuageY = joueurY + hauteurJoueur; nuageT = 0.35f;
      ev |= EV_DOUBLE_SAUT;
    }
  }

  // ----- Roulade (au sol) ; en l'air, il plonge et roule en arrivant
  if (memoireRoulade > 0 && auSol) {
    commencerRoulade();
    memoireRoulade = 0;
    ev |= EV_ROULADE;
  }
  if (roulade > 0) {
    roulade -= dt;
    if (roulade <= 0) {
      if (peutSeRelever()) seRelever();
      else roulade = 0.02f;                          // encore sous un plafond : il continue
    }
  }

  // ----- Gravité
  vitesseY += GRAVITE * dt;
  if (vitesseY > CHUTE_MAX) vitesseY = CHUTE_MAX;

  // ----- Épée : elle coupe ce qui est devant lui avant qu'il ne s'y cogne
  if (epee > 0) ev |= couperAvecEpee();

  // ----- Déplacement : il court toujours vers la droite, puis la verticale
  contreMur = false;
  avancerX(vitesseCourse * dt);
  if (contreMur) decalageMur += vitesseCourse * dt; // coincé : l'écran continue d'avancer sans lui
  else if (decalageMur > 0) {                       // libéré : l'écran le rattrape doucement
    decalageMur -= RETOUR_CAMERA * dt;
    if (decalageMur < 0) decalageMur = 0;
  }
  bool etaitAuSol = auSol;
  auSol = false;
  if (deplacerY(vitesseY * dt)) {
    if (vitesseY > 0) { auSol = true; doubleSautDispo = true; }
    vitesseY = 0;
  }
  if (etaitAuSol && !auSol && vitesseY >= 0) tolerance = TOLERANCE_BORD;
  if (auSol) animation += dt * 12;

  // ----- Aimant : les pièces proches volent vers lui
  if (aimant > 0) {
    float cx = joueurX + JOUEUR_L / 2, cy = joueurY + hauteurJoueur / 2;
    for (int i = 0; i < nbPieces; i++) {
      Piece& p = pieces[i];
      float dx = cx - p.x, dy = cy - p.y;
      float d = sqrtf(dx * dx + dy * dy);
      if (p.prise || d > RAYON_AIMANT || d < 1) continue;
      float pas = 900 * dt;                        // les pièces volent à 900 px/s
      if (pas > d) pas = d;
      p.x += dx / d * pas;
      p.y += dy / d * pas;
    }
  }

  // ----- Bonus ramassé ?
  for (int i = 0; i < nbBonus; i++) {
    Bonus& b = bonus[i];
    if (!b.pris && joueurX < b.x + 30 && joueurX + JOUEUR_L > b.x - 30 &&
                   joueurY < b.y + 30 && joueurY + hauteurJoueur > b.y - 30) {
      b.pris = true;
      annonce = b.type;
      annonceT = 1.8f;                             // grand texte (aimant et bulle)
      if (b.type == 'E') {                         // transformation !
        epee = DUREE_EPEE;
        sequenceTransfo = DUREE_TRANSFO;           // la séquence manga commence
        flash = 0.2f;                              // (éclair et étoiles juste après)
        transfoT = 0.6f;
        annonceT = 0;                              // le titre est déjà dans la séquence
      }
      if (b.type == 'A') aimant = DUREE_AIMANT;
      if (b.type == 'U') bulleProtege = true;
      ev |= EV_BONUS;
    }
  }

  // ----- Pièces touchées ?
  for (int i = 0; i < nbPieces; i++) {
    Piece& p = pieces[i];
    if (!p.prise && joueurX < p.x + 18 && joueurX + JOUEUR_L > p.x - 18 &&
                    joueurY < p.y + 18 && joueurY + hauteurJoueur > p.y - 18) {
      p.prise = true;
      piecesPrises++;
      score += POINTS_PIECE;
      ev |= EV_PIECE;
    }
  }

  // ----- Monstre touché ? (on retire 6 px autour du bonhomme : c'est plus juste)
  int m = monstreTouche(joueurX + 6, joueurY + 6, JOUEUR_L - 12, hauteurJoueur - 6);
  if (m >= 0 && invincible <= 0) {
    float mx, my;
    positionMonstre(monstres[m], mx, my);
    if (epee > 0) {                                 // mode étoile : le monstre est coupé
      monstres[m].vivant = false;                   // (même s'il tombe dessus)
      ajouterEclats(mx, my, monstres[m].type == 'm' ? DEMON : DEMON_VOLANT, BLANC, 8);
      score += POINTS_MONSTRE;
      coup = 0.25f;
      ev |= EV_COUPE;
    } else if (bulleProtege) {                      // la bulle éclate : il est sauvé
      monstres[m].vivant = false;
      ajouterEclats(mx, my, TURQUOISE, BLANC, 10);
      bulleProtege = false;
      invincible = 1.0f;                            // 1 s pour s'éloigner sans danger
      score += POINTS_MONSTRE;
      ev |= EV_BULLE;
    } else {
      ev |= EV_TOUCHE;                              // perdu !
    }
  }

    // ----- Drapeau atteint ?
    if (joueurX + JOUEUR_L > colonneArrivee * CASE + 24) ev |= EV_ARRIVEE;
    return ev;
}

// Fait avancer le jeu de dt secondes, par petits pas réguliers :
// le bonhomme va à la même vitesse quel que soit le nombre d'images/s.
uint16_t avancerJeu(float dt) {
  if (dt > 0.1f) dt = 0.1f;
  if (sequenceTransfo > 0) {                // séquence de transformation : jeu figé
    sequenceTransfo -= dt;
    return 0;
  }
  uint16_t ev = 0;
  reste += dt;
  while (reste >= PAS_CALCUL && !(ev & (EV_ARRIVEE | EV_TOUCHE)) && sequenceTransfo <= 0) {
    reste -= PAS_CALCUL;
    ev |= unPas();
  }
  // La caméra suit le bonhomme (il reste à 30 % de la largeur de l'écran,
  // pour bien voir les obstacles qui arrivent)
  camera = joueurX + JOUEUR_L / 2 - 1280 * 0.3f;
  float maxi = colonnes * CASE - 1280;
  if (camera > maxi) camera = maxi;
  if (camera < 0) camera = 0;
  camera += decalageMur;                    // coincé contre un mur : l'écran avance, lui reste en arrière
  if (joueurX + JOUEUR_L < camera) ev |= EV_TOUCHE;   // il a disparu à gauche de l'écran : PERDU
  return ev;
}

void chargerNiveau(int n) {
  numeroNiveau = n;
  const char* const* carte = NIVEAUX[n].carte;    // le niveau d'origine
  vitesseCourse = NIVEAUX[n].vitesse;
  bougeMonstres = NIVEAUX[n].bougeMonstres;
  colonnes = strlen(carte[0]);
  if (colonnes > COLONNES_MAX) colonnes = COLONNES_MAX;
  nbPieces = nbMonstres = nbBonus = nbEclats = nbOnomatopees = 0;
  for (int l = 0; l < LIGNES; l++) {
    for (int c = 0; c < colonnes; c++) {
      char ch = carte[l][c];
      grille[l][c] = ch;                                        // copie modifiable
      float cx = c * CASE + CASE / 2.0f, cy = l * CASE + CASE / 2.0f;
      if (ch == 'o' && nbPieces < PIECES_MAX) {
        pieces[nbPieces++] = { cx, cy, false };
      } else if ((ch == 'm' || ch == 'v') && nbMonstres < MONSTRES_MAX) {
        float y0 = (ch == 'm') ? (l + 1) * CASE - 17 : cy;    // 'm' posé sur le sol
        monstres[nbMonstres++] = { cx, y0, ch, c * 1.7f, true };
      } else if ((ch == 'E' || ch == 'A' || ch == 'U') && nbBonus < BONUS_MAX) {
        bonus[nbBonus++] = { cx, cy, ch, false };
      } else if (ch == 'P') {
        departX = c * CASE + (CASE - JOUEUR_L) / 2;
        departY = (l + 1) * CASE - JOUEUR_H;
      } else if (ch == 'F') {
        colonneArrivee = c;
      }
    }
    grille[l][colonnes] = 0;
  }
  joueurX = departX; joueurY = departY;
  hauteurJoueur = JOUEUR_H;
  vitesseY = 0;
  auSol = true; doubleSautDispo = true;
  memoireSaut = memoireRoulade = tolerance = roulade = animation = nuageT = 0;
  epee = aimant = invincible = coup = annonceT = flash = transfoT = sequenceTransfo = 0;
  prochainBonus = BONUS_PREMIER;
  premierBonusPose = false;
  decalageMur = 0;
  contreMur = false;
  bulleProtege = false;
  score = piecesPrises = 0;
  reste = tempsJeu = 0;
  camera = 0;
}

// =====================================================================
// 6 bis. MODE MULTIJOUEUR (Wi-Fi + serveur admin)
// Les tablettes ne se parlent PAS entre elles : elles parlent toutes au SERVEUR
// (un programme Python sur un PC, serveur_admin.py). Messages UDP « TYPE|nom|... » :
//   tablette -> serveur
//     HELLO|nom|perso               je suis dans le salon (toutes les 0,5 s)
//     PRG|nom|score|%               où j'en suis (10 fois par seconde en course)
//     FIN|nom|score|statut|temps|%  j'ai fini (statut 1 = drapeau, 0 = éliminé)
//     BYE|nom                       je pars
//   serveur -> tablette
//     LB|nom|joueurs|niveau         le salon : combien de joueurs
//     GO|nom|niveau|graine|ms       départ : compte à rebours (ms restantes) et graine des bonus
//     RK|nom|rang|n|(nom|score|%)*  classement en direct
//     RES|nom|rang|n|(nom|score|statut|temps)*   classement final
//     KICK|nom                      l'admin t'a exclu
// Le serveur décide du niveau, du départ, de la fin et du classement.
// Le jeu, lui, tourne sur chaque tablette : le nombre d'images par seconde ne baisse pas.
// Les messages importants sont répétés (le Wi-Fi peut en perdre).
// =====================================================================
WiFiUDP udp;
bool modeMulti = false;            // une partie multijoueur est en cours
bool wifiInitialise = false;
int  reseauEtat = 0;               // 0 pas lancé, 1 connexion..., 2 connecté, 3 échec
uint32_t debutWifi = 0;
char monId[12] = "";               // nom court de cette tablette (ex. "A1B2")
IPAddress serveurIP;               // adresse du PC serveur (apprise à sa 1re réponse)
bool serveurConnu = false;
uint32_t serveurDernierMs = 0;     // dernier message reçu du serveur
int  nbJoueurs = 0;                // joueurs dans le salon
int  niveauPartie = 0;             // niveau choisi par l'admin
uint32_t seedPartie = 1;           // graine commune des bonus
uint32_t graineMulti = 1;          // tirages au hasard pour poser les bonus
uint32_t compteDebut = 0;          // début du compte à rebours
uint32_t dernierEnvoi = 0;
// classement en direct (reçu du serveur) et classement final
LigneClassement classement[MAX_JOUEURS];
int  nbClassement = 0, monRang = 0;
LigneClassement finale[MAX_JOUEURS];
int  nbFinale = 0, rangFinal = 0;
bool finalPret = false;            // le serveur a envoyé le classement final
// mon résultat
int  monStatut = -1, monTemps = 0, monPct = 0;
bool resultatJoue = false;

void envoyer(const char* msg) {
  if (serveurConnu) udp.beginPacket(serveurIP, PORT_JEU);
  else              udp.beginPacket(WiFi.broadcastIP(), PORT_JEU);   // le serveur n'est pas encore connu
  udp.write((const uint8_t*)msg, strlen(msg));
  udp.endPacket();
}

void envoyerF(const char* fmt, ...) {
  char buf[96];
  va_list a;
  va_start(a, fmt);
  vsnprintf(buf, sizeof(buf), fmt, a);
  va_end(a);
  envoyer(buf);
}

// Lance la connexion Wi-Fi (sans attendre : l'écran continue de tourner)
void wifiDemarrer() {
  if (reseauEtat == 1 || reseauEtat == 2) return;
  if (!wifiInitialise) {
    WiFi.setPins(SDIO2_CLK, SDIO2_CMD, SDIO2_D0, SDIO2_D1, SDIO2_D2, SDIO2_D3, SDIO2_RST);
    wifiInitialise = true;
  }
  if (reseauEtat == 3) WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                            // pas d'économie d'énergie : messages plus rapides
  WiFi.begin(WIFI_SSID, WIFI_MDP);
  reseauEtat = 1;
  debutWifi = millis();
}

// À appeler à chaque image dans le salon
void majWifi() {
  if (reseauEtat == 1) {
    if (WiFi.status() == WL_CONNECTED) {
      String mac = WiFi.macAddress();
      mac.replace(":", "");
      snprintf(monId, sizeof(monId), "%s", mac.substring(mac.length() - 4).c_str());
      udp.begin(PORT_JEU);
      reseauEtat = 2;
    } else if (millis() - debutWifi > 20000) {
      reseauEtat = 3;                              // échec : bouton RÉESSAYER
    }
  } else if (reseauEtat == 2 && WiFi.status() != WL_CONNECTED) {
    udp.stop();                                    // Wi-Fi perdu : bouton RÉESSAYER
    reseauEtat = 3;
    serveurConnu = false;
  }
  // le serveur ne répond plus depuis 6 s : on le cherche de nouveau
  if (reseauEtat == 2 && serveurConnu && millis() - serveurDernierMs > 6000) serveurConnu = false;
}

int pctMulti() {                                      // avancement dans le niveau, 0 à 100
  int p = (int)(100.0f * joueurX / (colonnes * CASE));
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// Le serveur ne donne plus de nouvelles
bool serveurPerdu() {
  return serveurConnu && millis() - serveurDernierMs > 8000;
}

// Tirage au hasard réservé aux bonus du multijoueur (même suite sur toutes les tablettes)
int hasardMulti(int n) {
  graineMulti = graineMulti * 1103515245u + 12345u;
  return (int)((graineMulti >> 16) % n);
}

// Pose les bonus (épée, aimant, bulle) à des endroits tirés avec la graine commune :
// tous les joueurs trouvent les mêmes bonus aux mêmes endroits. Le premier bonus est
// toujours l'épée étoile, tôt dans le niveau. Pas de bonus au hasard pendant la course.
void preparerBonusMulti() {
  graineMulti = seedPartie;
  prochainBonus = 1.0e9f;
  int c = 8;
  bool premier = true;
  while (nbBonus < BONUS_MAX) {
    c += premier ? 8 + hasardMulti(5) : 22 + hasardMulti(14);   // ensuite un bonus tous les 22 à 35 cases
    if (c >= colonneArrivee - 3) break;
    int cc = c;
    while (cc < c + 6 && (solide(cc, 8) || solide(cc, 9) || !solide(cc, 10))) cc++;   // bonne place ?
    if (cc >= c + 6) continue;
    int t = hasardMulti(10);                       // quel pouvoir : 4 épées, 3 aimants, 3 bulles
    char type = premier ? 'E' : (t < 4) ? 'E' : (t < 7) ? 'A' : 'U';
    bool enLair = hasardMulti(10) < 4 && !solide(cc, 6) && !solide(cc, 7);
    if (premier) { enLair = false; premier = false; }
    Bonus b = { cc * CASE + CASE / 2.0f, (enLair ? 6 : 9) * CASE + CASE / 2.0f, type, false };
    bonus[nbBonus++] = b;
  }
}

// Prépare la partie et lance le compte à rebours (msRestantes : ce que dit le serveur)
void lancerCompte(int msRestantes) {
  chargerNiveau(niveauPartie);
  preparerBonusMulti();
  modeMulti = true;
  nbClassement = 0;
  monRang = 0;
  nbFinale = 0;
  finalPret = false;
  monStatut = -1;
  resultatJoue = false;
  compteDebut = millis() - (3000 - constrain(msRestantes, 0, 3000));   // tout le monde part ensemble
  dernierEnvoi = 0;
  etat = COMPTE;
  ignorerCeToucher(0, 0);
}

// Copie un nom reçu (max 15 caractères) dans une ligne de classement
void copierNom(char* dest, const char* src) {
  snprintf(dest, 16, "%s", src);
}

// Ce qui arrive du réseau (seuls les messages adressés à CETTE tablette comptent)
void traiterMessage(char* msg) {
  char* champs[40];
  int n = 0;
  char* p = msg;
  while (n < 40) {                                 // découpe aux « | »
    champs[n++] = p;
    char* sep = strchr(p, '|');
    if (!sep) break;
    *sep = 0;
    p = sep + 1;
  }
  if (n < 2) return;
  const char* type = champs[0];
  if (strcmp(champs[1], monId) != 0) return;       // pas pour moi (message d'une autre tablette)
  auto num = [&](int i) { return i < n ? atoi(champs[i]) : 0; };
  serveurIP = udp.remoteIP();                      // c'est le serveur qui parle
  serveurConnu = true;
  serveurDernierMs = millis();

  if (!strcmp(type, "LB")) {                       // salon : nombre de joueurs
    nbJoueurs = num(2);
  } else if (!strcmp(type, "GO") && etat == LOBBY_MULTI) {         // départ donné par l'admin
    niveauPartie = constrain(num(2), 0, NB_NIVEAUX - 1);
    seedPartie   = (uint32_t)num(3);
    lancerCompte(num(4));
  } else if (!strcmp(type, "RK") && modeMulti) {   // classement en direct
    monRang = num(2);
    nbClassement = 0;
    for (int i = 4; i + 2 < n + 0 && nbClassement < MAX_JOUEURS; i += 3) {
      LigneClassement& l = classement[nbClassement++];
      copierNom(l.nom, champs[i]);
      l.score = atoi(champs[i + 1]);
      l.pct = atoi(champs[i + 2]);
    }
  } else if (!strcmp(type, "RES") && modeMulti) {  // classement final
    rangFinal = num(2);
    nbFinale = 0;
    for (int i = 4; i + 3 < n + 0 && nbFinale < MAX_JOUEURS; i += 4) {
      LigneClassement& l = finale[nbFinale++];
      copierNom(l.nom, champs[i]);
      l.score = atoi(champs[i + 1]);
      l.statut = atoi(champs[i + 2]);
      l.temps = atoi(champs[i + 3]);
    }
    finalPret = true;
    if (etat == JEU || etat == COMPTE) {           // l'admin a arrêté la partie
      etat = FIN;
      tempsFin = 1.1f;
    }
  } else if (!strcmp(type, "KICK")) {              // l'admin m'a exclu
    modeMulti = false;
    etat = ACCUEIL;
  }
}

void lireReseau() {
  for (int k = 0; k < 8; k++) {                    // au plus 8 messages par image
    if (udp.parsePacket() <= 0) return;
    char buf[400];
    int len = udp.read((uint8_t*)buf, sizeof(buf) - 1);
    if (len > 0) { buf[len] = 0; traiterMessage(buf); }
  }
}

// Messages envoyés régulièrement selon l'état (les pertes sont rattrapées)
void envoisPeriodiques(uint32_t ms) {
  uint32_t periode = 0;
  if (etat == LOBBY_MULTI) periode = 500;
  else if (etat == JEU && modeMulti) periode = 100;
  else if ((etat == FIN || etat == PERDU) && modeMulti && monStatut != -1 && !finalPret) periode = 300;
  if (periode == 0 || ms - dernierEnvoi < periode) return;
  dernierEnvoi = ms;
  switch (etat) {
    case LOBBY_MULTI: envoyerF("HELLO|%s|%d", monId, persoChoisi); break;
    case JEU:         envoyerF("PRG|%s|%d|%d", monId, score, pctMulti()); break;
    default:          envoyerF("FIN|%s|%d|%d|%d|%d", monId, score, monStatut, monTemps, monPct); break;
  }
}

// J'ai fini ma partie (drapeau ou monstre) : je le dis au serveur
void terminerMulti(bool arrive) {
  monStatut = arrive ? 1 : 0;
  monTemps = (int)(tempsJeu * 1000);
  monPct = arrive ? 100 : pctMulti();
  resultatJoue = false;
  for (int i = 0; i < 3; i++) envoyerF("FIN|%s|%d|%d|%d|%d", monId, score, monStatut, monTemps, monPct);
}

// Je quitte le multijoueur
void quitterMulti() {
  for (int i = 0; i < 2; i++) envoyerF("BYE|%s", monId);
  modeMulti = false;
}

// Mon résultat est connu quand le serveur a envoyé le classement final
bool resultatPret() { return finalPret; }

// =====================================================================
// 7. SONS
// Les mélodies sont jouées note par note pendant que le jeu continue
// (le programme ne s'arrête pas pour attendre la fin d'une note).
// =====================================================================
const Note MELODIE_START[]   = { {784, 90}, {1175, 160} };
const Note MELODIE_ARRIVEE[] = { {1047, 140}, {1319, 140}, {1568, 320} };
const Note MELODIE_RECORD[] = { {1047, 110}, {1319, 110}, {1568, 110}, {2093, 220},
                                 {1568, 110}, {2093, 500} };
const Note MELODIE_PERDU[]   = { {784, 160}, {659, 160}, {523, 160}, {392, 450} };
const Note MELODIE_BONUS[]   = { {523, 60}, {659, 60}, {784, 60}, {1047, 60}, {1319, 60}, {1568, 200} };
const Note MELODIE_TRANSFO[] = { {392, 70}, {523, 70}, {659, 70}, {784, 70}, {1047, 90},
                                 {784, 70}, {1047, 90}, {1568, 140}, {2093, 320} };
#define NB_NOTES(m) (sizeof(m) / sizeof(m[0]))

const Note* melodie = nullptr;
int melodieLongueur = 0, melodieIndex = 0;
uint32_t prochaineNote = 0;

void jouerMelodie(const Note* m, int longueur) {
  melodie = m; melodieLongueur = longueur; melodieIndex = 0;
  prochaineNote = millis();
}

// À appeler à chaque image : lance la note suivante quand c'est l'heure.
void majMelodie() {
  if (melodie == nullptr || melodieIndex >= melodieLongueur) return;
  if (millis() < prochaineNote) return;
  const Note& n = melodie[melodieIndex++];
  M5.Speaker.tone(n.frequence, n.duree, 1, true);      // canal 1 : musique
  prochaineNote = millis() + n.duree + 15;
}

void jouerSons(uint16_t ev) {                          // canal 0 : bruitages
  if (ev & EV_BONUS) {
    if (annonce == 'E') jouerMelodie(MELODIE_TRANSFO, NB_NOTES(MELODIE_TRANSFO));
    else                jouerMelodie(MELODIE_BONUS, NB_NOTES(MELODIE_BONUS));
  }
  if (ev & EV_BULLE)            { M5.Speaker.tone(900, 50, 0, true); M5.Speaker.tone(300, 120, 0, false); }
  else if (ev & EV_COUPE)       { M5.Speaker.tone(2600, 30, 0, true); M5.Speaker.tone(1700, 70, 0, false); }
  else if (ev & EV_PIECE)       { M5.Speaker.tone(1568, 50, 0, true); M5.Speaker.tone(2093, 90, 0, false); }
  else if (ev & EV_DOUBLE_SAUT) { M5.Speaker.tone(880, 50, 0, true); M5.Speaker.tone(1320, 80, 0, false); }
  else if (ev & EV_SAUT)        { M5.Speaker.tone(660, 70, 0, true); }
  else if (ev & EV_ROULADE)     { M5.Speaker.tone(330, 60, 0, true); M5.Speaker.tone(250, 90, 0, false); }
  else if (ev & EV_FIN_POUVOIR) { M5.Speaker.tone(600, 70, 0, true); M5.Speaker.tone(400, 110, 0, false); }
}

void clic() { M5.Speaker.tone(1200, 40, 0, true); }     // petit bruit des boutons

// Volume de 0 (muet) à 5, enregistré dans la mémoire
void reglerSon(int niveau) {
  if (niveau < 0) niveau = 0;
  if (niveau > 5) niveau = 5;
  niveauSon = niveau;
  M5.Speaker.setVolume(niveauSon * 50);                 // 0 à 250
  memoire.putInt("son", niveauSon);
}

// =====================================================================
// 8. DESSIN
// =====================================================================
// Mélange deux couleurs (k = 0 -> a, k = 1 -> b)
uint32_t melange(uint32_t a, uint32_t b, float k) {
  int r = ((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * k);
  int v = ((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * k);
  int u = (a & 255)         + (((int)(b & 255)         - (int)(a & 255)) * k);
  return ((uint32_t)r << 16) | ((uint32_t)v << 8) | (uint32_t)u;
}

void ellipseContour(float x, float y, float rx, float ry, uint32_t couleur, float bord) {
  ecran.fillEllipse(x, y, rx + bord, ry + bord, MARINE);
  ecran.fillEllipse(x, y, rx, ry, couleur);
}

// Texte avec un contour épais, comme sur le logo
void texteContour(const char* t, int x, int y, uint32_t couleur, float taille) {
  ecran.setTextSize(taille);
  int b = (int)(3 * taille);
  ecran.setTextColor(MARINE);
  for (int dx = -b; dx <= b; dx += b)
    for (int dy = -b; dy <= b + b; dy += b)       // un peu plus bas : effet d'ombre
      ecran.drawString(t, x + dx, y + dy);
  ecran.setTextColor(couleur);
  ecran.drawString(t, x, y);
  ecran.setTextSize(1);
}

// Étoile à 5 branches, avec un contour épais
void etoile(float cx, float cy, float r, uint32_t couleur) {
  etoileAvecBord(cx, cy, r, couleur, 5);
}

// Étoile à 5 branches ; bord : épaisseur du contour
void etoileAvecBord(float cx, float cy, float r, uint32_t couleur, float bord) {
  for (int passe = 0; passe < 2; passe++) {
    float R = (passe == 0) ? r + bord : r;           // d'abord le contour, plus grand
    uint32_t c = (passe == 0) ? MARINE : couleur;
    for (int i = 0; i < 10; i++) {
      float a1 = -M_PI / 2 + i * M_PI / 5, a2 = a1 + M_PI / 5;
      float r1 = (i % 2 == 0) ? R : R * 0.45f, r2 = (i % 2 == 0) ? R * 0.45f : R;
      ecran.fillTriangle(cx, cy, cx + r1 * cosf(a1), cy + r1 * sinf(a1),
                         cx + r2 * cosf(a2), cy + r2 * sinf(a2), c);
    }
  }
}

// Un sapin couvert de neige : un tronc et 3 étages de branches (des triangles),
// avec de la neige en haut et au bord de chaque étage.
// (x, base) : pied du tronc ; h : hauteur ; l : largeur
// etages : 3 pour les sapins proches, 2 (sans neige en haut) pour ceux du fond : moins à dessiner.
void sapin(float x, float base, float h, float l, uint32_t couleur, uint32_t neige, int etages = 3) {
  ecran.fillRect(x - l * 0.07f, base - h * 0.14f, l * 0.14f, h * 0.14f, couleur);   // tronc
  for (int e = 0; e < etages; e++) {
    float bas = base - h * (0.1f + e * 0.25f);       // bas de l'étage
    float demi = l * 0.5f * (1 - e * 0.22f);         // demi-largeur (plus étroit en haut)
    float haut = bas - h * 0.42f;                    // pointe de l'étage
    ecran.fillTriangle(x - demi, bas, x + demi, bas, x, haut, couleur);
    if (etages < 3) continue;                        // sapins du fond : pas de détails de neige
    ecran.fillRect(x - demi * 0.9f, bas - 4, demi * 1.8f, 4, neige);           // neige au bord
    ecran.fillTriangle(x - demi * 0.45f, haut + h * 0.19f, x + demi * 0.45f,
                       haut + h * 0.19f, x, haut, neige);                         // neige en haut
  }
}

// MODE ÉTOILE : une forêt de sapins sous la neige, sur deux rangées.
// Les sapins du fond bougent moins vite que ceux de devant : ça donne
// l'impression de profondeur.
void dessinerForet() {
  // sapins au loin, pâles dans la brume (bougent 4x moins vite que le sol)
  for (int i = 0; i < 7; i++) {                    // (moins de sapins = plus d'images par seconde)
    float x = fmodf(i * 220.0f - camera * 0.25f, 1540);
    if (x < 0) x += 1540;
    float h = 230 + (i * 37) % 90;
    sapin(x - 130, 600, h, h * 0.45f, SAPIN_LOIN, NEIGE_LOIN, 2);
  }
  ecran.fillRect(0, 550, 1280, 50, BRUME);         // brume au ras du sol
  // grands sapins plus proches, vert sombre (bougent 2x moins vite que le sol)
  for (int i = 0; i < 5; i++) {
    float x = fmodf(i * 360.0f - camera * 0.5f, 1800);
    if (x < 0) x += 1800;
    float h = 380 + (i * 53) % 110;
    sapin(x - 260, 600, h, h * 0.5f, SAPIN_PRES, NEIGE);
  }
}

// MODE ÉTOILE : la neige qui tombe, dessinée devant le décor.
// Chaque flocon a sa vitesse : les gros (plus proches) tombent plus vite.
void dessinerNeige() {
  for (int i = 0; i < 36; i++) {
    int couche = i % 4;                            // 0 = loin ... 3 = près
    float y = fmodf(i * 53.0f + horloge * (50 + couche * 28), 740) - 20;
    float x = fmodf(i * 97.0f - camera * (0.3f + couche * 0.2f) - horloge * 25
                    + 12 * sinf(horloge * 1.5f + i), 1300);      // le vent le fait onduler
    if (x < 0) x += 1300;
    ecran.fillCircle(x - 10, y, 1.5f + couche * 0.8f, NEIGE);
  }
}

// Le ciel s'arrête à y = 600 : en dessous, le sol le recouvre de toute façon
// (moins de pixels à dessiner = plus d'images par seconde).
// Les couleurs viennent du thème (jour ou forêt du mode étoile).
void dessinerCiel() {
  const Theme& t = *theme;
  for (int i = 0; i < 8; i++)                       // dégradé en 8 bandes
    ecran.fillRect(0, i * 75, 1280, 75, melange(t.cielHaut, t.cielBas, i / 7.0f));
  if (t.neige) {                                    // forêt : pas de collines ni de nuages
     dessinerForet();
     return;
    }
    ecran.setClipRect(0, 0, 1280, 600);               // collines : rien sous le sol
    for (int i = 0; i < 4; i++) {                     // collines au loin (bougent 2x moins vite)
      float x = fmodf(i * 520 - camera * 0.5f, 2080);
      if (x < -300) x += 2080;
      ecran.fillCircle(x, 760, 260 + (i % 2) * 60, t.colline);
    }
    ecran.clearClipRect();
    for (int i = 0; i < 4; i++) {                     // nuages (bougent 4x moins vite)
      float x = fmodf(i * 430 + 150 - camera * 0.25f, 1720);
      if (x < -200) x += 1720;
      int y = 120 + (i % 3) * 70;
      ecran.fillCircle(x, y, 34, BLANC);
      ecran.fillCircle(x + 40, y - 18, 44, BLANC);
      ecran.fillCircle(x + 86, y, 32, BLANC);
      ecran.fillRoundRect(x - 20, y, 130, 32, 16, BLANC);
    }
}

void dessinerCase(int c, int l, int x, int y) {
  const Theme& t = *theme;
  char k = caseEn(c, l);
  if (k == '#' || k == '=') {
    bool hautLibre = caseEn(c, l - 1) != k;
    int m = (k == '=') ? 4 : 0;                       // bloc flottant : contour tout autour
    int g = (caseEn(c - 1, l) == k) ? 0 : m;          // marge à gauche
    int d = (caseEn(c + 1, l) == k) ? 0 : m;          // marge à droite
    if (k == '=') ecran.fillRect(x, y, CASE, CASE, MARINE);
    ecran.fillRect(x + g, y + m, CASE - g - d, CASE - 2 * m, t.terre);
    ecran.fillCircle(x + 14 + (c * 7 + l * 3) % 30, y + 34 + (c * 5) % 14, 5, t.terreFonce);
    if (hautLibre) {                                  // herbe (ou neige) sur le dessus
      ecran.fillRect(x, y, CASE, 5, MARINE);
      ecran.fillRect(x + g, y + 5, CASE - g - d, 16, t.herbe);
      ecran.fillRect(x + g, y + 21, CASE - g - d, 5, t.herbeFonce);
    }
  } else if (k == 'B' && t.neige) {                   // mode étoile : morceau de tronc
    dessinerTronc(c, l, x, y);
  } else if (k == 'B') {                              // caisse en bois
    ecran.fillRect(x, y, CASE, CASE, MARINE);
    ecran.fillRect(x + 4, y + 4, CASE - 8, CASE - 8, t.caisse);
    ecran.drawRect(x + 10, y + 10, CASE - 20, CASE - 20, t.caisseFonce);
    ecran.drawRect(x + 11, y + 11, CASE - 22, CASE - 22, t.caisseFonce);
    ecran.drawWideLine(x + 13, y + CASE - 13, x + CASE - 13, y + 13, 2, t.caisseFonce);
  }
}

// MODE ÉTOILE : une caisse devient un morceau de tronc d'arbre.
// Les caisses collées ensemble forment un seul gros tronc : le contour
// n'est dessiné que sur les bords du tronc.
void dessinerTronc(int c, int l, int x, int y) {
  bool bordG = caseEn(c - 1, l) != 'B', bordD = caseEn(c + 1, l) != 'B';
  bool bordH = caseEn(c, l - 1) != 'B', bordB = caseEn(c, l + 1) != 'B';
  ecran.fillRect(x, y, CASE, CASE, ECORCE);
  for (int k = 0; k < 3; k++)                       // rainures de l'écorce (de haut en bas)
    ecran.fillRect(x + 9 + k * 18 + (c * 5 + k * 7) % 6, y, 3, CASE, ECORCE_FONCE);
  if ((c * 7 + l * 13) % 5 == 0) {                   // un nœud dans le bois
    ecran.fillEllipse(x + 30, y + 30, 7, 9, ECORCE_FONCE);
    ecran.fillEllipse(x + 30, y + 30, 3, 5, ECORCE);
  }
  if (bordG) {                                      // côté gauche éclairé
    ecran.fillRect(x, y, 4, CASE, MARINE);
    ecran.fillRect(x + 4, y, 6, CASE, ECORCE_CLAIRE);
  }
  if (bordD) {                                      // côté droit dans l'ombre
    ecran.fillRect(x + CASE - 4, y, 4, CASE, MARINE);
    ecran.fillRect(x + CASE - 10, y, 6, CASE, ECORCE_FONCE);
  }
  if (bordB) ecran.fillRect(x, y + CASE - 4, CASE, 4, MARINE);
  if (bordH) {                                      // de la neige sur le dessus, d'un seul
    ecran.fillRect(x, y - 6, CASE, 16, MARINE);     // morceau sur toute la largeur du tronc
    ecran.fillRect(x, y - 3, CASE, 10, NEIGE);
    if (bordG) { ecran.fillCircle(x, y + 2, 8, MARINE);         ecran.fillCircle(x + 2, y + 2, 5, NEIGE); }
    if (bordD) { ecran.fillCircle(x + CASE, y + 2, 8, MARINE); ecran.fillCircle(x + CASE - 2, y + 2, 5, NEIGE); }
  }
  if (bordD && (c * 3 + l * 5) % 4 == 1) {           // une petite branche avec de la neige
    ecran.drawWideLine(x + CASE - 2, y + 36, x + CASE + 15, y + 22, 4, MARINE);
        ecran.drawWideLine(x + CASE - 2, y + 36, x + CASE + 14, y + 23, 2, ECORCE);
        ecran.fillRoundRect(x + CASE + 3, y + 15, 15, 6, 3, NEIGE);
    }
}

void dessinerNiveau() {
  int c0 = versCase(camera);
  for (int c = c0; c <= c0 + 22; c++) {
    int x = c * CASE - (int)camera;
    for (int l = 0; l < LIGNES; l++) dessinerCase(c, l, x, l * CASE);
  }
}

// Seulement le sol (pour les menus)
void dessinerSol() {
  for (int x = 0; x < 1280; x += CASE) {
    ecran.fillRect(x, 600, CASE, 120, TERRE);
    ecran.fillCircle(x + 14 + (x / 60 * 7) % 30, 640 + (x / 60 * 5) % 14, 5, TERRE_FONCE);
    ecran.fillCircle(x + 30, 690, 5, TERRE_FONCE);
  }
  ecran.fillRect(0, 600, 1280, 5, MARINE);
  ecran.fillRect(0, 605, 1280, 16, HERBE);
  ecran.fillRect(0, 621, 1280, 5, HERBE_FONCE);
}

void dessinerPieces() {
  for (int i = 0; i < nbPieces; i++) {
    Piece& p = pieces[i];
    float x = p.x - camera;
    if (p.prise || x < -30 || x > 1310) continue;
    float largeur = fabsf(cosf(horloge * 3 + p.x * 0.01f)); // la pièce tourne
    if (largeur < 0.2f) largeur = 0.2f;
    ellipseContour(x, p.y, 17 * largeur, 17, OR, 3);
    ecran.fillEllipse(x - 3 * largeur, p.y - 3, 7 * largeur, 9, OR_CLAIR);
  }
}

void dessinerDrapeau() {
  float x = colonneArrivee * CASE + 28 - camera;
  if (x < -150 || x > 1400) return;
  float sol = 600;                                          // haut de l'herbe (ligne 10)
  ecran.fillRect(x - 5, sol - 260, 10, 260, MARINE);        // mât
  ecran.fillRect(x - 2, sol - 258, 4, 256, BLANC);
  ellipseContour(x, sol - 266, 10, 10, OR, 3);
  float vague = sinf(horloge * 6) * 8;                      // le drapeau flotte
  ecran.fillTriangle(x + 4, sol - 252, x + 4, sol - 160, x + 120, sol - 206 + vague, MARINE);
  ecran.fillTriangle(x + 8, sol - 244, x + 8, sol - 168, x + 108, sol - 206 + vague, ROSE);
}

// Yeux mignons : gros, avec un reflet blanc. (x, y) = entre les deux yeux.
void yeuxMignons(float x, float y, float ecart, float taille) {
  for (int i = -1; i <= 1; i += 2) {
    ecran.fillEllipse(x + i * ecart, y, taille, taille * 1.25f, MARINE);
    ecran.fillCircle(x + i * ecart - taille * 0.3f, y - taille * 0.45f, taille * 0.4f, BLANC);
  }
}

// Petit monstre au sol : une boule de gelée violette qui rebondit
void dessinerGelee(float x, float y) {
  float rebond = fabsf(sinf(horloge * 7));                // il s'écrase et s'étire
  float rx = 27 - 3 * rebond, ry = 21 + 4 * rebond;
  float base = y + 17;                                    // bas du monstre
  float cy = base - ry;
  ecran.fillTriangle(x + 2, cy - ry - 2, x + 14, cy - ry - 16, x + 12, cy - ry + 4, MARINE);     // petite feuille
  ecran.fillTriangle(x + 5, cy - ry - 2, x + 13, cy - ry - 11, x + 11, cy - ry + 2, HERBE);
  ellipseContour(x, cy, rx, ry, MAUVE, 3);
  ecran.fillEllipse(x - 8, cy - ry * 0.45f, 6, 4, BLANC); // reflet
  yeuxMignons(x - 4, cy - 2, 9, 5);
  ecran.fillEllipse(x - 19, cy + 6, 5, 3, ROSE_JOUE);     // joues
  ecran.fillEllipse(x + 11, cy + 6, 5, 3, ROSE_JOUE);
  ecran.fillArc(x - 4, cy + 7, 0, 4, 0, 180, MARINE);     // sourire
}

// Abeille : on passe dessous en roulade
void dessinerAbeille(float x, float y) {
  float battement = fabsf(sinf(horloge * 30));            // les ailes battent vite
  ellipseContour(x + 4, y - 18, 10, 5 + 6 * battement, BLANC, 2.5f);
  ellipseContour(x + 16, y - 16, 8, 4 + 5 * battement, BLANC, 2.5f);
    ecran.fillTriangle(x + 22, y + 2, x + 34, y + 6, x + 22, y + 10, MARINE);   // dard
    ellipseContour(x, y, 24, 18, OR, 3);
    for (int i = 0; i < 2; i++)                             // rayures
      ecran.fillRect(x + 4 + i * 10, y - 15 + i * 2, 5, 30 - i * 4, MARINE);
    yeuxMignons(x - 10, y - 2, 6, 4);
    ecran.fillEllipse(x - 20, y + 7, 4, 3, ROSE_JOUE);
    ecran.fillArc(x - 10, y + 6, 0, 3, 0, 180, MARINE);
}

// Triangle avec un contour (on le grossit un peu depuis son centre pour le contour)
void triangleContour(float x0, float y0, float x1, float y1, float x2, float y2,
                     uint32_t couleur) {
  float cx = (x0 + x1 + x2) / 3, cy = (y0 + y1 + y2) / 3;
  const float k = 1.3f;
  ecran.fillTriangle(cx + (x0 - cx) * k, cy + (y0 - cy) * k, cx + (x1 - cx) * k, cy + (y1 - cy) * k,
                     cx + (x2 - cx) * k, cy + (y2 - cy) * k, MARINE);
  ecran.fillTriangle(x0, y0, x1, y1, x2, y2, couleur);
}

// Yeux de petit démon : jaunes, avec des sourcils froncés (mais toujours mignon)
void yeuxDemon(float x, float y, float ecart, float taille) {
  for (int i = -1; i <= 1; i += 2) {
    float ox = x + i * ecart;
    ecran.fillEllipse(ox, y, taille + 1.5f, taille * 1.2f + 1.5f, MARINE);
    ecran.fillEllipse(ox, y, taille, taille * 1.2f, OR);
    ecran.fillCircle(ox - 1, y + 1, taille * 0.5f, MARINE);             // pupille
    ecran.drawWideLine(ox - taille * 1.2f, y - taille * 1.6f - i * 2,    // sourcil
                       ox + taille * 1.2f, y - taille * 1.6f + i * 2, 1.5f, MARINE);
  }
}

// MODE ÉTOILE : petit démon au sol (à la place de la gelée), qui rebondit
void dessinerDemon(float x, float y) {
  float rebond = fabsf(sinf(horloge * 7));
  float rx = 25 - 2 * rebond, ry = 20 + 3 * rebond;
  float base = y + 17;                                    // bas du monstre
  float cy = base - ry;
  // queue pointue qui remue (derrière lui, à droite)
  float q = 4 * sinf(horloge * 6);
  ecran.drawWideLine(x + rx - 4, cy + 8, x + rx + 14, cy - 6 + q, 3, MARINE);
  ecran.drawWideLine(x + rx - 4, cy + 8, x + rx + 13, cy - 5 + q, 1.5f, DEMON);
  triangleContour(x + rx + 9, cy - 10 + q, x + rx + 20, cy - 8 + q, x + rx + 13, cy + q, DEMON);
  // deux petites cornes
  triangleContour(x - 16, cy - ry + 7, x - 7, cy - ry + 3, x - 17, cy - ry - 10, CORNE);
  triangleContour(x + 5, cy - ry + 3, x + 14, cy - ry + 7, x + 15, cy - ry - 10, CORNE);
  // corps rond et ventre plus clair
  ellipseContour(x, cy, rx, ry, DEMON, 3);
  ecran.fillEllipse(x - 3, cy + ry * 0.45f, rx * 0.5f, ry * 0.35f, DEMON_CLAIR);
  yeuxDemon(x - 4, cy - 3, 9, 4);
  // sourire avec deux petits crocs
  ecran.fillArc(x - 4, cy + 7, 0, 6, 0, 180, MARINE);
  ecran.fillTriangle(x - 8, cy + 7, x - 5, cy + 7, x - 6.5f, cy + 11, BLANC);
  ecran.fillTriangle(x - 3, cy + 7, x, cy + 7, x - 1.5f, cy + 11, BLANC);
}

// MODE ÉTOILE : petit démon qui vole (à la place de l'abeille), avec des
// ailes de chauve-souris qui battent
void dessinerDemonVolant(float x, float y) {
  float b = sinf(horloge * 18);
  for (int i = -1; i <= 1; i += 2) {                      // les deux ailes
    float sx = x + i * 13, sy = y - 6;                    // attache de l'aile
    float tx = x + i * 40, ty = y - 18 - 10 * b;          // pointe
    triangleContour(sx, sy, tx, ty, x + i * 30, y + 6, DEMON_AILE);
    triangleContour(sx, sy, x + i * 30, y + 6, x + i * 17, y + 9, DEMON_AILE);
  }
  triangleContour(x - 12, y - 10, x - 4, y - 14, x - 13, y - 26, CORNE);    // cornes
  triangleContour(x + 4, y - 14, x + 12, y - 10, x + 13, y - 26, CORNE);
  ellipseContour(x, y, 19, 17, DEMON_VOLANT, 3);
  yeuxDemon(x - 4, y - 3, 7, 3.5f);
  ecran.fillArc(x - 4, y + 6, 0, 5, 0, 180, MARINE);      // sourire et crocs
  ecran.fillTriangle(x - 7, y + 6, x - 5, y + 6, x - 6, y + 9, BLANC);
  ecran.fillTriangle(x - 3, y + 6, x - 1, y + 6, x - 2, y + 9, BLANC);
}

void dessinerMonstres() {
  bool demons = theme->neige;                         // en mode étoile : des petits démons
  for (int i = 0; i < nbMonstres; i++) {
    float x, y;
        positionMonstre(monstres[i], x, y);
        x -= camera;
        if (x < -60 || x > 1340) continue;
        if (!monstres[i].vivant) continue;           // coupé ou éclaté
        if (monstres[i].type == 'm') {
          if (demons) dessinerDemon(x, y);
          else dessinerGelee(x, y);
        } else {
          if (demons) dessinerDemonVolant(x, y);
          else dessinerAbeille(x, y);
        }
    }
}

// Le dessin d'un pouvoir, centré sur (x, y). t : taille (1 = normale)
void iconeBonus(char type, float x, float y, float t) {
  if (type == 'E') {                                  // épée étoile, en diagonale
    float ax = x - 13 * t, ay = y + 13 * t;           // bout du manche
    float px = x + 15 * t, py = y - 15 * t;           // pointe de la lame
    ecran.drawWideLine(ax, ay, px, py, 4.5f * t, MARINE);
    ecran.drawWideLine(x - 5 * t, y + 5 * t, px, py, 2.5f * t, OR_CLAIR);      // lame
    ecran.drawWideLine(ax, ay, x - 6 * t, y + 6 * t, 2.2f * t, BOIS);          // manche
    ecran.drawWideLine(x - 13 * t, y - t, x + t, y + 13 * t, 3.5f * t, MARINE); // garde
    ecran.drawWideLine(x - 12 * t, y, x, y + 12 * t, 1.8f * t, OR);
    ellipseContour(ax, ay, 3 * t, 3 * t, OR, 1.5f * t);                         // pommeau
  } else if (type == 'A') {                           // aimant en U, bouts en métal
    float r0 = 6 * t, r1 = 16 * t, h = 13 * t, b = 3 * t;   // b : épaisseur du contour
    y -= 3 * t;
    ecran.fillArc(x, y, r0 - b, r1 + b, 0, 180, MARINE);
    ecran.fillRect(x - r1 - b, y - h - b, r1 - r0 + 2 * b, h + b + 1, MARINE);
    ecran.fillRect(x + r0 - b, y - h - b, r1 - r0 + 2 * b, h + b + 1, MARINE);
    ecran.fillArc(x, y, r0, r1, 0, 180, ROUGE);
    ecran.fillRect(x - r1, y - h, r1 - r0, h + 1, ROUGE);
    ecran.fillRect(x + r0, y - h, r1 - r0, h + 1, ROUGE);
    ecran.fillRect(x - r1, y - h, r1 - r0, 6 * t, GRIS);
    ecran.fillRect(x + r0, y - h, r1 - r0, 6 * t, GRIS);
  } else {                                            // bulle : petit bouclier bleu
    auto forme = [&](float g, uint32_t c) {           // g : en plus (pour le contour)
      ecran.fillRect(x - (13 + g) * t, y - (15 + g) * t, (26 + 2 * g) * t, (13 + g) * t, c);
      ecran.fillTriangle(x - (13 + g) * t, y - 3 * t, x + (13 + g) * t, y - 3 * t,
                         x, y + (16 + 1.5f * g) * t, c);
    };
    forme(3, MARINE);
    forme(0, TURQUOISE);
    forme(-5, BLEU_BULLE);
    ecran.fillCircle(x - 5 * t, y - 9 * t, 2.5f * t, BLANC); // reflet
  }
}

// Les bonus du niveau : une bulle qui flotte, avec le pouvoir dedans
void dessinerBonus() {
  for (int i = 0; i < nbBonus; i++) {
    Bonus& b = bonus[i];
    float x = b.x - camera;
    if (b.pris || x < -60 || x > 1340) continue;
    float y = b.y - 8 + 6 * sinf(horloge * 4 + i);              // elle flotte
    uint32_t fond = (b.type == 'E') ? OR_CLAIR : (b.type == 'A') ? ROSE_PALE : BLEU_PALE;
    ellipseContour(x, y, 28, 28, fond, 3);
    ecran.fillEllipse(x - 12, y - 14, 6, 4, BLANC);              // reflet de la bulle
    iconeBonus(b.type, x, y + 1, 1.0f);
    for (int k = 0; k < 2; k++) {                                // deux étincelles qui tournent
      float a = horloge * 3 + k * M_PI;
      ellipseContour(x + 40 * cosf(a), y + 40 * sinf(a), 3, 3, OR_CLAIR, 1.5f);
    }
  }
}

// Les éclats de caisse ou de monstre qui volent
void dessinerEclats() {
  for (int i = 0; i < nbEclats; i++) {
    float x = eclats[i].x - camera, y = eclats[i].y;
    ecran.fillRect(x - 6, y - 6, 12, 12, MARINE);
    ecran.fillRect(x - 4, y - 4, 8, 8, eclats[i].couleur);
  }
}

// Un personnage (voir PERSOS). (bx, by) : milieu de ses pieds à l'écran.
// e : taille (1 = normale).   enLair : pose de saut.
// phase : position des jambes quand il court.
void dessinerPerso(const Perso& q, float bx, float by, float e, bool enLair, float phase) {
  // X(d) : d pixels vers l'avant ; Y(d) : d pixels au-dessus des pieds
  auto X = [&](float d) { return bx + d * e; };
  auto Y = [&](float d) { return by - d * e; };
  // bras ou jambe : deux traits épais (couleurs c1 puis c2) avec un contour
  auto membre = [&](float x0, float y0, float x1, float y1, float x2, float y2,
                    uint32_t c1, uint32_t c2) {
    ecran.drawWideLine(X(x0), Y(y0), X(x1), Y(y1), 6.5f * e, MARINE);
    ecran.drawWideLine(X(x1), Y(y1), X(x2), Y(y2), 6.5f * e, MARINE);
    ecran.drawWideLine(X(x0), Y(y0), X(x1), Y(y1), 3.5f * e, c1);
    ecran.drawWideLine(X(x1), Y(y1), X(x2), Y(y2), 3.5f * e, c2);
  };
  auto rond = [&](float x, float y, float rx, float ry, uint32_t couleur) {
    ellipseContour(X(x), Y(y), rx * e, ry * e, couleur, 3 * e);
  };
  // moitié haute d'un disque (cheveux, casquette), avec contour
  auto calotte = [&](float x, float y, float r, uint32_t couleur) {
    ecran.fillArc(X(x), Y(y), 0, (r + 3) * e, 180, 360, MARINE);
    ecran.fillArc(X(x), Y(y), 0, r * e, 180, 360, couleur);
  };
  float p = sinf(phase);
  uint32_t mollet = q.short_ ? q.peau : q.bas;     // en short, on voit la peau

  // ----- Jambes (hanche -> genou -> pied) ; 1 = jambe avant, 2 = arrière
  float g1x, g1y, p1x, p1y, g2x, g2y, p2x, p2y;
  if (enLair) {                                     // en saut : jambes repliées
    g1x = 16; g1y = 30; p1x = 8;   p1y = 14;
    g2x = -6; g2y = 20; p2x = -22; p2y = 10;
  } else {                                          // en train de courir
    p1x = 18 * p; g1x = 11 * p + 5; p1y = 6 + fmaxf(0, -p) * 10; g1y = 20;
    p2x = -18 * p; g2x = -11 * p + 5; p2y = 6 + fmaxf(0, p) * 10;   g2y = 20;
  }
  membre(-5, 34, g2x, g2y, p2x, p2y, q.bas, mollet);       // jambe arrière
  rond(p2x + 4, p2y - 1, 11, 6, q.chaussures);
  if (q.lameDeCourse) {                                     // prothèse de course en J
    membre(3, 34, (3 + g1x) / 2, (34 + g1y) / 2, g1x, g1y, q.bas, q.bas);
    membre(g1x, g1y, p1x + 10, p1y + 6, p1x - 2, p1y - 5, GRIS_FONCE, GRIS_FONCE);
  } else {                                                  // jambe avant
    membre(3, 34, g1x, g1y, p1x, p1y, q.bas, mollet);
    rond(p1x + 4, p1y - 1, 11, 6, q.chaussures);
    ecran.drawWideLine(X(p1x - 2), Y(p1y - 3), X(p1x + 11), Y(p1y - 3), 1.2f * e, BLANC);
  }

  // ----- Bras arrière, corps, bras avant (manches de la couleur du haut)
  float b = enLair ? -1 : p;
  float mainY = enLair ? 74 : 52;
  membre(-7, 60, -18 - 7 * b, 52, -25 - 9 * b, mainY, q.haut, q.haut);
  rond(-25 - 9 * b, mainY, 5, 5, q.peau);
  rond(0, 50, 16, 18, q.haut);
  if (q.coiffure == FOULARD) rond(-3, 63, 15, 8, q.cheveux);   // le foulard sur les épaules
  membre(7, 60, 18 + 7 * b, 52, 25 + 9 * b, mainY + 2, q.haut, q.haut);
  rond(25 + 9 * b, mainY + 2, 5, 5, q.peau);

  // ----- Ce qui est derrière la tête
  float R = (q.coiffure == ANTENNES) ? 21 : 18;    // l'alien a une plus grosse tête
  if (q.coiffure == QUEUE_DE_CHEVAL) {             // elle se balance quand il court
    float balance = enLair ? 8 : 4 * p;
    membre(-10, 90, -26, 82 + balance, -31, 66 + balance, q.cheveux, q.cheveux);
  } else if (q.coiffure == MACARONS) {             // deux macarons sur le dessus
    rond(-9, 99, 10, 10, q.cheveux);
    rond(13, 103, 10, 10, q.cheveux);
  } else if (q.coiffure == FOULARD) {              // le foulard entoure la tête
    rond(3, 80, R + 5, R + 6, q.cheveux);
  } else if (q.coiffure == CHEVEUX_COURTS || q.coiffure == BOUCLES) {
    rond(-8, 79, 10, 12, q.cheveux);               // arrière de la tête
  }

  // ----- Tête (dans le foulard, on ne voit que le visage)
  if (q.coiffure == FOULARD) rond(7, 79, R - 4, R - 2, q.peau);
  else rond(4, 80, R, R, q.peau);

  // ----- Ce qui est sur la tête
  if (q.coiffure == CASQUETTE) {                   // casquette à l'envers, comme le logo
    calotte(4, 82, 18, q.cheveux);
    rond(-15, 83, 10, 4, q.cheveux);
  } else if (q.coiffure == CHEVEUX_COURTS || q.coiffure == MACARONS ||
             q.coiffure == QUEUE_DE_CHEVAL) {
      calotte(4, 82, 18, q.cheveux);
      rond(15, 91, 7, 4, q.cheveux);                 // petite mèche devant
    } else if (q.coiffure == BOUCLES) {
      for (int i = 0; i < 7; i++) {                  // petites boucles en arc de cercle
        float a = M_PI + i * M_PI / 6;
        rond(4 + 16 * cosf(a), 84 - 16 * sinf(a), 7, 7, q.cheveux);
      }
    } else if (q.coiffure == ANTENNES) {             // deux antennes qui bougent
      float bal = 3 * sinf(horloge * 8);
      ecran.drawWideLine(X(-2), Y(97), X(-8 + bal), Y(116), 2 * e, MARINE);
      ecran.drawWideLine(X(11), Y(99), X(17 + bal), Y(118), 2 * e, MARINE);
      rond(-8 + bal, 118, 5, 5, q.cheveux);
      rond(17 + bal, 120, 5, 5, q.cheveux);
    }

    // ----- Visage
    if (q.coiffure == ANTENNES) {                    // grands yeux noirs qui brillent
      ecran.fillEllipse(X(9), Y(80), 6 * e, 8 * e, MARINE);
      ecran.fillEllipse(X(20), Y(80), 5 * e, 7 * e, MARINE);
      ecran.fillCircle(X(7), Y(84), 2.2f * e, BLANC);
      ecran.fillCircle(X(18), Y(83), 2 * e, BLANC);
      ecran.fillEllipse(X(3), Y(72), 3 * e, 2 * e, ROSE_JOUE);
    } else {
      rond(9, 78, 4, 6, BLANC);
      rond(18, 78, 3, 5, BLANC);
      ecran.fillCircle(X(10.5f), Y(77), 2 * e, MARINE);
      ecran.fillCircle(X(19), Y(77), 2 * e, MARINE);
    }
    if (q.lunettes) {                                // lunettes rondes
      ecran.fillArc(X(8), Y(78), 9 * e, 11 * e, 0, 360, MARINE);
      ecran.fillArc(X(21), Y(78), 8 * e, 10 * e, 0, 360, MARINE);
      ecran.drawWideLine(X(-2), Y(80), X(-8), Y(82), 1.2f * e, MARINE);   // branche
    }
    ecran.fillArc(X(13), Y(70), 0, 6 * e, 0, 180, MARINE);     // sourire
    ecran.fillArc(X(13), Y(70), 0, 4 * e, 0, 180, BLANC);
}

// Le personnage en boule pendant la roulade : il tourne sur lui-même
void dessinerBoule(const Perso& q, float bx, float by) {
  float cx = bx, cy = by - 24;
  float a = horloge * 900;                          // angle en degrés (il tourne vite)
  float ar = a * M_PI / 180;
  for (int i = 0; i < 3; i++)                       // traits de vitesse
    ecran.drawWideLine(cx - 40 - i * 6, cy - 12 + i * 12, cx - 60 - i * 6, cy - 12 + i * 12, 2.5f, BLANC);
  ellipseContour(cx, cy, 22, 22, q.haut, 3);
  ecran.fillArc(cx, cy, 13, 22, a + 110, a + 250, q.bas);             // jambes repliées
  float tx = cx + 11 * cosf(ar), ty = cy + 11 * sinf(ar);             // la tête tourne
  ellipseContour(tx, ty, 12, 12, q.peau, 3);
  if (q.coiffure == ANTENNES) {
    ecran.fillCircle(tx + 12 * cosf(ar), ty + 12 * sinf(ar), 5, q.cheveux);
  } else {                                          // cheveux, casquette ou foulard
    ecran.fillArc(tx, ty, 0, 15, a - 80, a + 80, MARINE);
    ecran.fillArc(tx, ty, 0, 12, a - 75, a + 75, q.cheveux);
  }
}

// L'épée étoile dans la main avant (mêmes repères que dans dessinerPerso)
void dessinerEpee(const Perso& q, float bx, float by, bool enLair, float phase) {
  float b = enLair ? -1 : sinf(phase);
  float mainY = enLair ? 74 : 52;
  float hx = bx + 25 + 9 * b, hy = by - (mainY + 2);        // position de la main
  // angle de la lame en degrés (0 = droit devant, -90 = vers le haut)
  float angle = -50;
  float avance = 0;                                          // 0 -> 1 pendant un coup
  if (coup > 0) {
    avance = 1 - coup / 0.25f;
    angle = -95 + avance * 135;                              // l'épée descend d'un coup
  }
  if (avance > 0.15f) {                                      // traînée blanche du coup
    float cx = bx + 6, cy = by - 52;
    ecran.fillArc(cx, cy, 58, 98, -95, angle, MARINE);
    ecran.fillArc(cx, cy, 62, 94, -93, angle - 2, BLANC);
  }
  float a = angle * M_PI / 180, ca = cosf(a), sa = sinf(a);
  // point situé à d pixels de la main, le long de l'épée
  auto P = [&](float d, float& x, float& y) { x = hx + d * ca; y = hy + d * sa; };
  float x0, y0, x1, y1;
  P(-12, x0, y0); P(4, x1, y1);                              // manche
    ecran.drawWideLine(x0, y0, x1, y1, 4, MARINE);
    ecran.drawWideLine(x0, y0, x1, y1, 2, BOIS);
    P(6, x0, y0); P(74, x1, y1);                              // lame dorée
    ecran.drawWideLine(x0, y0, x1, y1, 6, MARINE);
    ecran.drawWideLine(x0, y0, x1, y1, 3.5f, OR_CLAIR);
    P(10, x0, y0); P(68, x1, y1);
    ecran.drawWideLine(x0, y0, x1, y1, 1.2f, BLANC);
    float gx, gy;
    P(6, gx, gy);                                             // garde, en travers
    ecran.drawWideLine(gx - 12 * sa, gy + 12 * ca, gx + 12 * sa, gy - 12 * ca, 4, MARINE);
    ecran.drawWideLine(gx - 10 * sa, gy + 10 * ca, gx + 10 * sa, gy - 10 * ca, 2, OR);
    P(-13, x0, y0);
    ellipseContour(x0, y0, 4, 4, OR, 2);                      // pommeau
    ellipseContour(hx, hy, 5, 5, q.peau, 3);                  // la main tient le manche
    // une petite étoile brillante glisse le long de la lame
    P(10 + fmodf(horloge * 160, 60), x0, y0);
    ecran.drawWideLine(x0 - 8, y0, x0 + 8, y0, 1.5f, BLANC);
    ecran.drawWideLine(x0, y0 - 8, x0, y0 + 8, 1.5f, BLANC);
}

// La bulle qui protège : un grand rond bleu clair autour de lui
void dessinerBulleProtection(float cx, float cy) {
  float r = 66 + 3 * sinf(horloge * 5);
  ecran.fillArc(cx, cy, r, r + 3, 0, 360, MARINE);
  ecran.fillArc(cx, cy, r - 5, r, 0, 360, BLEU_BULLE);
  ecran.fillArc(cx, cy, r - 15, r - 10, 200, 245, BLANC); // reflet
}

// MODE ÉTOILE : la cape du héros, dessinée derrière lui. (bx, by) : ses pieds.
void dessinerCape(float bx, float by, bool enLair) {
  float w = 5 * sinf(horloge * 14);                  // elle flotte au vent
  float monte = enLair ? 14 : 0;                     // en saut, elle remonte un peu
  float ax = bx - 8, ay = by - 68;                   // épaule arrière
  float cx = bx + 6, cy = by - 70;                   // épaule avant
  float dx = bx - 52, dy = by - 50 + w - monte;      // bout de la cape, en haut
  float ex = bx - 42, ey = by - 14 - w * 0.6f - monte;    // bout de la cape, en bas
  // d'abord le contour (un peu plus grand), puis la cape
  ecran.fillTriangle(ax - 3, ay - 3, cx + 3, cy - 3, ex - 3, ey + 4, MARINE);
  ecran.fillTriangle(ax - 3, ay - 3, dx - 4, dy - 3, ex - 3, ey + 4, MARINE);
  ecran.fillTriangle(ax, ay, cx, cy, ex, ey, HEROS_CAPE);
  ecran.fillTriangle(ax, ay, dx, dy, ex, ey, HEROS_CAPE);
  etoileAvecBord((ax + dx + ex) / 3, (ay + dy + ey) / 3, 5, OR_CLAIR, 1.5f);    // étoiles dorées
  etoileAvecBord((ax + cx + ex) / 3 - 4, (ay + cy + ey) / 3 + 8, 4, OR_CLAIR, 1.5f);
}

// MODE ÉTOILE : aura en flammes façon manga, dessinée DERRIÈRE le héros.
// Des pointes tout autour qui tremblent, plus grandes vers le haut
// (les flammes montent). Deux couches : dorée, puis blanche au centre.
void dessinerAura(float bx, float by) {
  float cx = bx, cy = by - 52;
  for (int couche = 0; couche < 2; couche++) {
    uint32_t c = (couche == 0) ? OR_CLAIR : BLANC;
    float rx = (couche == 0) ? 36 : 27, ry = (couche == 0) ? 60 : 46;
    for (int i = 0; i < 9; i++) {
      float a = i * 2 * M_PI / 9;
      float flamme = 1.3f + 0.3f * fabsf(sinf(horloge * 22 + i * 1.7f));   // ça tremble
      if (sinf(a) < 0) flamme -= 0.4f * sinf(a);                            // plus haut en haut
      ecran.fillTriangle(cx + rx * cosf(a - 0.23f), cy + ry * sinf(a - 0.23f),
                         cx + rx * cosf(a + 0.23f), cy + ry * sinf(a + 0.23f),
                         cx + rx * flamme * cosf(a), cy + ry * flamme * sinf(a), c);
    }
    ecran.fillEllipse(cx, cy, rx, ry, c);
  }
}

// MODE ÉTOILE : lignes de vitesse façon manga, qui filent vers la gauche
void dessinerLignesVitesse() {
  for (int i = 0; i < 7; i++) {
    float y = 60 + (i * 47) % 480;
    float l = 120 + (i * 53) % 160;
    float x = fmodf(i * 331.0f - horloge * 2400, 1700);
    if (x < 0) x += 1700;
    x -= 200;
    ecran.drawWideLine(x, y, x + l, y, 1 + (i % 3) * 0.6f, BLANC);
  }
}

// MODE ÉTOILE : les onomatopées montent un peu et grossissent
void dessinerOnomatopees() {
  ecran.setTextDatum(middle_center);
  for (int i = 0; i < nbOnomatopees; i++) {
    Onomatopee& o = onomatopees[i];
    float age = 0.6f - o.vie;                       // 0 au début
    float taille = (age < 0.1f) ? 0.6f + age * 4 : 1.0f;
    texteContour(o.texte, o.x - camera, o.y - age * 60, o.couleur, taille);
  }
}

// Explosion d'étoiles juste après la transformation
void dessinerExplosionEtoiles(float bx, float by) {
  if (transfoT > 0) {
    float rayon = 30 + (0.6f - transfoT) * 450;      // le cercle d'étoiles grandit
    for (int k = 0; k < 8; k++) {
      float a = k * 0.785f + horloge * 2;
      etoileAvecBord(bx + rayon * cosf(a), by - 50 + rayon * sinf(a), 10, OR, 2);
    }
  }
}

// Petit nuage blanc sous ses pieds au moment du double saut
void dessinerNuageSaut() {
  if (nuageT <= 0) return;
  float k = nuageT / 0.35f;                         // 1 au début, 0 à la fin
  float x = nuageX - camera, y = nuageY;
  for (int i = -1; i <= 1; i++)
    ellipseContour(x + i * 22 * (2 - k), y - (i == 0 ? 6 : 0), 12 * k + 2, 9 * k + 2, BLANC, 2);
}

// Cadre avec contour et ombre (fenêtres, bulles, boutons)
void bulle(int x, int y, int l, int h, int rayon, uint32_t fond) {
  ecran.fillRoundRect(x, y + 6, l, h, rayon, MARINE);       // ombre
  ecran.fillRoundRect(x, y, l, h, rayon, MARINE);
  ecran.fillRoundRect(x + 5, y + 5, l - 10, h - 10, rayon - 5, fond);
}

// respire = true : le bouton grossit et rétrécit doucement
void dessinerBouton(const Bouton& b, float taille, bool respire) {
  int g = respire ? (int)(4 + 4 * sinf(horloge * 4)) : 0;
  bulle(b.x - g, b.y - g, b.l + 2 * g, b.h + 2 * g, b.h < 100 ? 22 : 30, b.couleur);
  if (b.texte[0] != 0) {
    ecran.setTextDatum(middle_center);
    texteContour(b.texte, b.x + b.l / 2, b.y + b.h / 2, BLANC, taille);
  }
}

// ----- Petites icônes dessinées avec des formes simples -----
void iconePause(int cx, int cy) {
  for (int i = -1; i <= 1; i += 2) {
    ecran.fillRoundRect(cx + i * 12 - 9, cy - 22, 18, 44, 5, MARINE);
    ecran.fillRoundRect(cx + i * 12 - 5, cy - 18, 10, 36, 3, BLANC);
  }
}

void iconeMaison(int cx, int cy) {
  ecran.fillTriangle(cx - 32, cy - 2, cx + 32, cy - 2, cx, cy - 32, MARINE);
  ecran.fillRect(cx - 22, cy - 4, 44, 32, MARINE);
  ecran.fillTriangle(cx - 24, cy - 6, cx + 24, cy - 6, cx, cy - 26, BLANC);
  ecran.fillRect(cx - 17, cy - 6, 34, 29, BLANC);
  ecran.fillRect(cx - 6, cy + 6, 12, 17, MARINE);           // porte
}

void iconeSon(int cx, int cy, bool muet) {
  ecran.fillRect(cx - 24, cy - 10, 16, 20, MARINE);         // haut-parleur
  ecran.fillTriangle(cx - 12, cy, cx + 6, cy - 22, cx + 6, cy + 22, MARINE);
  ecran.fillRect(cx - 21, cy - 7, 10, 14, BLANC);
  ecran.fillTriangle(cx - 9, cy, cx + 3, cy - 15, cx + 3, cy + 15, BLANC);
  if (muet) {                                               // croix rouge
    ecran.drawWideLine(cx + 12, cy - 12, cx + 30, cy + 12, 4, ROUGE);
    ecran.drawWideLine(cx + 12, cy + 12, cx + 30, cy - 12, 4, ROUGE);
  } else {                                                  // ondes sonores
    ecran.fillArc(cx + 6, cy, 12, 16, 320, 40, BLANC);
    ecran.fillArc(cx + 6, cy, 22, 26, 320, 40, BLANC);
  }
}

// =====================================================================
// 9. LES ÉCRANS
// =====================================================================
// Astuce du niveau à montrer, selon la colonne où est le bonhomme
const char* astuceActuelle() {
  int col = versCase(joueurX);
  const InfoNiveau& n = NIVEAUX[numeroNiveau];
  for (int i = 0; i < n.nbAstuces; i++)
    if (col >= n.astuces[i].colonne - 10 && col <= n.astuces[i].colonne)
      return n.astuces[i].texte;
  return nullptr;
}

// Un pouvoir en cours, sous les pièces : son icône et le temps qui reste
// (part : 1 = plein, 0 = fini ; la barre devient rouge et clignote à la fin)
void jaugePouvoir(int x, char type, float part) {
  bulle(x, 92, 184, 58, 29, BLANC);
  iconeBonus(type, x + 32, 121, 0.75f);
  ecran.fillRoundRect(x + 60, 110, 108, 22, 11, MARINE);
  bool bientotFini = part < 0.3f && fmodf(horloge, 0.3f) < 0.15f;
  ecran.fillRoundRect(x + 63, 113, 16 + (int)(86 * part), 16, 8, bientotFini ? ROUGE : HERBE);
}

// Grand texte quand on vient de prendre un bonus
void dessinerAnnonce() {
  const char* titre = "BULLE !";
  const char* explication = "Elle te protège d'un monstre";
  if (annonce == 'E') { titre = "MODE ÉTOILE !"; explication = "Tu coupes tout pendant 5 secondes !"; }
  if (annonce == 'A') { titre = "AIMANT !";       explication = "Les pièces viennent toutes seules !"; }
  float pop = annonceT > 1.6f ? 1 + (annonceT - 1.6f) * 3 : 1;    // petit effet « pop »
  ecran.setTextDatum(middle_center);
  texteContour(titre, 640, 205, OR, 1.3f * pop);
  int l = ecran.textWidth(explication) + 60;
  bulle(640 - l / 2, 262, l, 62, 31, BLANC);
  ecran.setTextColor(MARINE);
  ecran.drawString(explication, 640, 294);
}

void dessinerInfos() {
  char texte[64];
  ecran.setTextColor(MARINE);
  ecran.setTextDatum(middle_left);

  // Pièces ramassées
  bulle(20, 16, 250, 64, 32, BLANC);
  ellipseContour(58, 48, 15, 15, OR, 3);
  snprintf(texte, sizeof(texte), "%d / %d", piecesPrises, nbPieces);
  ecran.drawString(texte, 86, 50);

  // Score
  bulle(290, 16, 270, 64, 32, BLANC);
  snprintf(texte, sizeof(texte), "Score %d", score);
  ecran.drawString(texte, 316, 50);

  if (modeMulti) {
    // Mon rang et le score du premier
    bulle(580, 16, 440, 64, 32, BLANC);
    ecran.setTextColor(MARINE);
    ecran.setTextDatum(middle_left);
    if (serveurPerdu())            snprintf(texte, sizeof(texte), "Serveur perdu...");
    else if (nbClassement > 0)     snprintf(texte, sizeof(texte), "Rang %d/%d  1er : %d", monRang, nbClassement, classement[0].score);
    else                           snprintf(texte, sizeof(texte), "En course !");
    ecran.drawString(texte, 606, 50);
    // Barre de course : où en est chacun (rose = moi, bleu = les autres)
    ecran.fillRoundRect(580, 96, 660, 14, 7, MARINE);
    ecran.fillRoundRect(583, 99, 654, 8, 4, BLANC);
    for (int i = 0; i < nbClassement; i++) {
      if (i == monRang - 1) continue;
      ellipseContour(592 + (int)(636 * (float)classement[i].pct / 100), 103, 9, 9, TURQUOISE, 3);
    }
    ellipseContour(592 + (int)(636 * (float)pctMulti() / 100), 103, 11, 11, ROSE, 3);
    if (AFFICHER_FPS) {
      snprintf(texte, sizeof(texte), "%d i/s", fps);
      ecran.setTextColor(theme->neige ? BLANC : MARINE);
      ecran.setTextDatum(middle_right);
      ecran.drawString(texte, 1260, 50);
    }
    if (etat == JEU && tempsJeu < 0.5f) {          // « GO ! » juste après le départ
      ecran.setTextDatum(middle_center);
      texteContour("GO !", 640, 330, OR, 3.0f);
    }
  } else {
  // Numéro du niveau (une ombre + le texte : plus rapide qu'un contour complet)
  ecran.setTextDatum(middle_center);
  snprintf(texte, sizeof(texte), "Niveau %d", numeroNiveau + 1);
  ecran.setTextColor(MARINE);
  ecran.drawString(texte, 763, 52);
  ecran.setTextColor(BLANC);
  ecran.drawString(texte, 760, 48);

  if (AFFICHER_FPS) {
    snprintf(texte, sizeof(texte), "%d i/s", fps);
    ecran.setTextColor(theme->neige ? BLANC : MARINE);   // lisible sur le ciel de nuit
    ecran.setTextDatum(middle_right);
    ecran.drawString(texte, 1150, 50);
  }

  // Bouton pause
  dessinerBouton(BOUTON_PAUSE, 1.0f, false);
  iconePause(BOUTON_PAUSE.x + BOUTON_PAUSE.l / 2, BOUTON_PAUSE.y + BOUTON_PAUSE.h / 2);
  }

  // Pouvoirs en cours
  int px = 20;
  if (epee > 0)   { jaugePouvoir(px, 'E', epee / DUREE_EPEE);     px += 200; }
  if (aimant > 0) { jaugePouvoir(px, 'A', aimant / DUREE_AIMANT); px += 200; }
  if (bulleProtege) {
    bulle(px, 92, 64, 58, 29, BLANC);
      iconeBonus('U', px + 32, 121, 0.75f);
    }
    // Astuce en bas de l'écran
    const char* astuce = astuceActuelle();
    if (astuce != nullptr && etat == JEU) {
      int l = ecran.textWidth(astuce) + 80;
      bulle(640 - l / 2, 630, l, 70, 35, BLANC);
      ecran.setTextColor(MARINE);
      ecran.setTextDatum(middle_center);
      ecran.drawString(astuce, 640, 665);
    }
}

void dessinerAccueil() {
  char texte[64];
  theme = &THEME_JOUR;                       // les menus sont toujours de jour
  camera = 0;
  dessinerCiel();
  dessinerSol();
  dessinerPerso(PERSOS[persoChoisi], 230, 600, 2.2f, false, horloge * 10);   // il court sur place
  dessinerGelee(1150, 583);                                 // un petit monstre le regarde

    ecran.setTextDatum(middle_center);
    texteContour("GAME", 720, 95, OR, 2.3f);
    texteContour("TAB", 720, 215, ROUGE, 2.8f);
    dessinerBouton(BOUTON_START, 1.6f, true);
    dessinerBouton(BOUTON_PERSO, 1.0f, false);
  dessinerBouton(BOUTON_MULTI, 1.0f, false);

    // Total des étoiles gagnées, en haut à droite
    int total = 0;
    for (int i = 0; i < NB_NIVEAUX; i++) total += etoiles[i];
    bulle(1030, 24, 230, 64, 32, BLANC);
    etoile(1076, 56, 20, OR);
    ecran.setTextColor(MARINE);
    ecran.setTextDatum(middle_left);
    snprintf(texte, sizeof(texte), "%d / %d", total, 3 * NB_NIVEAUX);
    ecran.drawString(texte, 1112, 58);
}

// Fenêtre de choix du personnage : 8 cartes, celle choisie est dorée
void dessinerChoixPerso() {
  theme = &THEME_JOUR;
  camera = horloge * 40;                      // le décor défile doucement
  dessinerCiel();
  dessinerSol();
  ecran.setTextDatum(middle_center);
  texteContour("CHOISIS TON PERSO", 680, 62, OR, 1.3f);
  dessinerBouton(BOUTON_RETOUR, 1.0f, false);
  iconeMaison(BOUTON_RETOUR.x + BOUTON_RETOUR.l / 2, BOUTON_RETOUR.y + BOUTON_RETOUR.h / 2 + 4);

    for (int i = 0; i < NB_PERSOS; i++) {
      Bouton c = cartePerso(i);
      bool choisi = (i == persoChoisi);
      if (choisi) {                            // contour doré épais
        ecran.fillRoundRect(c.x - 8, c.y - 8, c.l + 16, c.h + 16, 34, OR);
      }
      bulle(c.x, c.y, c.l, c.h, 28, choisi ? OR_CLAIR : BLANC);
      // celui qui est choisi court sur place, les autres attendent
      dessinerPerso(PERSOS[i], c.x + c.l / 2, c.y + 172, 1.25f, false, choisi ? horloge * 10 : 1.2f);
      ecran.setTextDatum(middle_center);
      ecran.setTextColor(MARINE);
      ecran.drawString(PERSOS[i].nom, c.x + c.l / 2, c.y + 200);
      if (choisi) etoile(c.x + c.l - 30, c.y + 30, 18, OR);
    }
    dessinerBouton(BOUTON_OK, 1.0f, true);
}

void dessinerChoixNiveau() {
  char texte[64];
  theme = &THEME_JOUR;
  camera = horloge * 40;                      // le décor défile doucement
  dessinerCiel();
  dessinerSol();

    ecran.setTextDatum(middle_center);
    texteContour("CHOISIS TON NIVEAU", 680, 80, OR, 1.3f);
    dessinerBouton(BOUTON_RETOUR, 1.0f, false);
    iconeMaison(BOUTON_RETOUR.x + BOUTON_RETOUR.l / 2, BOUTON_RETOUR.y + BOUTON_RETOUR.h / 2 + 4);

    for (int i = 0; i < NB_NIVEAUX; i++) {
      Bouton c = carteNiveau(i);
      int cx = c.x + c.l / 2;
      bulle(c.x, c.y, c.l, c.h, 30, BLANC);
      // pastille avec le numéro
      ellipseContour(cx, c.y + 95, 62, 62, NIVEAUX[i].couleur, 5);
      snprintf(texte, sizeof(texte), "%d", i + 1);
      ecran.setTextDatum(middle_center);
      texteContour(texte, cx, c.y + 95, BLANC, 2.0f);
      // nom du niveau
      ecran.setTextColor(MARINE);
      ecran.drawString(NIVEAUX[i].nom, cx, c.y + 200);
      // étoiles gagnées (grises si pas encore gagnées)
      for (int k = 0; k < 3; k++)
        etoile(cx + (k - 1) * 80, c.y + 265 - (k == 1 ? 10 : 0), 28, k < etoiles[i] ? OR : GRIS);
      // record
      snprintf(texte, sizeof(texte), "Record : %d", records[i]);
      ecran.setTextColor(MARINE);
      ecran.drawString(texte, cx, c.y + 335);
    }
}

void dessinerPause() {
  // assombrit le jeu : une ligne sur deux est peinte en bleu foncé
  for (int ligne = 0; ligne < 720; ligne += 2) ecran.fillRect(0, ligne, 1280, 1, MARINE);
  bulle(360, 60, 560, 590, 34, BLANC);
  ecran.setTextDatum(middle_center);
  texteContour("PAUSE", 640, 118, ROSE, 1.5f);

    dessinerBouton(BOUTON_REPRENDRE, 1.1f, true);

    // Réglage du son : muet / - / barres / +
    dessinerBouton(BOUTON_MUET, 1.0f, false);
    iconeSon(BOUTON_MUET.x + BOUTON_MUET.l / 2 - 4, BOUTON_MUET.y + BOUTON_MUET.h / 2, niveauSon == 0);
    dessinerBouton(BOUTON_MOINS, 1.4f, false);
    dessinerBouton(BOUTON_PLUS, 1.4f, false);
    for (int i = 0; i < 5; i++) {
      int h = 14 + i * 9;
      int x = 650 + i * 18, y = 362 - h;
      ecran.fillRect(x, y, 14, h, MARINE);
      ecran.fillRect(x + 3, y + 3, 8, h - 6, i < niveauSon ? HERBE : GRIS);
    }

    dessinerBouton(BOUTON_RECOMMENCER, 1.0f, false);
    dessinerBouton(BOUTON_ACCUEIL, 1.0f, false);
}

void dessinerFin() {
  char texte[64];
  // fenêtre décalée à gauche pour laisser voir le bonhomme au drapeau
  bulle(120, 40, 720, 500, 34, BLANC);
  ecran.setTextDatum(middle_center);
  texteContour("BRAVO !", 480, 100, ROSE, 1.6f);

    // les étoiles apparaissent une par une
    for (int k = 0; k < 3; k++) {
      bool gagnee = k < etoilesAffichees;
      float r = (k == 1) ? 44 : 36;
      etoile(480 + (k - 1) * 110, 200 - (k == 1 ? 14 : 0), r, gagnee ? OR : GRIS);
    }

    ecran.setTextColor(MARINE);
    snprintf(texte, sizeof(texte), "Score : %d      Pièces : %d / %d", score, piecesPrises, nbPieces);
    ecran.drawString(texte, 480, 300);
    if (fanfareJouee && nouveauRecord) {
      if (fmodf(horloge, 0.6f) < 0.4f)                 // clignote
        texteContour("NOUVEAU RECORD !", 480, 365, OR, 1.0f);
    } else {
      ecran.setTextColor(MARINE);
      snprintf(texte, sizeof(texte), "Record : %d", records[numeroNiveau]);
      ecran.drawString(texte, 480, 365);
    }

    if (tempsFin > 1.0f) {
      dessinerBouton(BOUTON_NIVEAUX, 0.9f, false);
      dessinerBouton(BOUTON_REJOUER, 0.9f, false);
      if (numeroNiveau + 1 < NB_NIVEAUX) dessinerBouton(BOUTON_SUIVANT, 0.9f, true);
    }
}

void dessinerPerdu() {
  char texte[64];
  if (tempsFin < 1.0f) return;                      // on laisse d'abord le bonhomme tomber
  bulle(290, 120, 700, 440, 34, BLANC);
  ecran.setTextDatum(middle_center);
  texteContour("OH NON !", 640, 190, ROUGE, 1.6f);
  ecran.setTextColor(MARINE);
  ecran.drawString("Un petit monstre t'a attrapé !", 640, 280);
  snprintf(texte, sizeof(texte), "Pièces : %d / %d", piecesPrises, nbPieces);
  ecran.drawString(texte, 640, 345);
  dessinerGelee(390, 360);                          // les monstres ont l'air content
  dessinerAbeille(890, 340);
  dessinerBouton(BOUTON_NIVEAUX_P, 1.0f, false);
  dessinerBouton(BOUTON_REJOUER_P, 1.0f, true);
}

// Le personnage choisi, avec la tenue du héros (pour le mode étoile)
Perso persoHeros() {
  Perso heros = PERSOS[persoChoisi];
  heros.haut = HEROS_HAUT;
  heros.bas = HEROS_BAS;
  heros.chaussures = BLANC;
  return heros;
}

// MODE ÉTOILE : la séquence de transformation façon manga (le jeu est figé).
// - fond sombre avec des lignes de concentration qui pointent vers le centre
// - une bande qui traverse l'écran avec le visage du héros en très gros
// - le nom du pouvoir en grand, et une onomatopée
void dessinerSequenceTransfo() {
  float t = DUREE_TRANSFO - sequenceTransfo;          // temps écoulé (0 -> 1 s)
  const int haut = 200, h = 250;                      // la bande : y de 200 à 450
  // la bande arrive de la gauche, reste, puis repart à droite
  float glisse = 0;
  if (t < 0.15f) glisse = -1280 * (1 - t / 0.15f);
  else if (t > DUREE_TRANSFO - 0.15f) glisse = 1280 * (t - (DUREE_TRANSFO - 0.15f)) / 0.15f;

    // 1) la bande violette avec des lignes de vitesse
    ecran.fillRect(0, 0, 1280, 720, MARINE);
    ecran.fillRect(glisse, haut, 1280, h, HEROS_CAPE);
    for (int i = 0; i < 9; i++) {
      float y = haut + 15 + i * 27;
      float x = fmodf(i * 263.0f + horloge * 3000, 1500) - 200;
      ecran.drawWideLine(glisse + x, y, glisse + x + 220, y, 2, MAUVE);
    }
    // 2) le visage en très gros plan (taille x5) avec un regard décidé
    Perso heros = persoHeros();
    float bx = 620 + glisse, by = haut + h / 2 + 400;
    dessinerPerso(heros, bx, by, 5.0f, false, 1.2f);
    ecran.drawWideLine(bx + 18, by - 428, bx + 64, by - 414, 6, MARINE);    // sourcils froncés
    ecran.drawWideLine(bx + 70, by - 414, bx + 116, by - 428, 6, MARINE);
    ecran.fillCircle(bx + 56, by - 390, 4, BLANC);                         // reflets dans les yeux
    ecran.fillCircle(bx + 99, by - 390, 3, BLANC);
    float eclat = 12 + 6 * sinf(t * 30);                                   // petite étoile « kira »
    ecran.drawWideLine(bx + 160 - eclat, by - 440, bx + 160 + eclat, by - 440, 3, BLANC);
    ecran.drawWideLine(bx + 160, by - 440 - eclat, bx + 160, by - 440 + eclat, 3, BLANC);
    // 3) on recouvre ce qui dépasse de la bande (drawWideLine annule le setClipRect,
    //    alors on ne peut pas s'en servir pour couper le dessin)
    ecran.fillRect(0, 0, 1280, haut, MARINE);
    ecran.fillRect(0, haut + h, 1280, 720 - haut - h, MARINE);
    if (glisse < 0) ecran.fillRect(glisse + 1280, haut, -glisse, h, MARINE);
    if (glisse > 0) ecran.fillRect(0, haut, glisse, h, MARINE);
    // 4) les lignes de concentration, au-dessus et au-dessous de la bande
    //    (fillTriangle respecte bien le setClipRect)
    for (int zone = 0; zone < 2; zone++) {
      if (zone == 0) ecran.setClipRect(0, 0, 1280, haut - 12);
      else           ecran.setClipRect(0, haut + h + 12, 1280, 720 - haut - h - 12);
      for (int i = 0; i < 48; i++) {
        float a = i * 2 * M_PI / 48 + hasard(0.03f);    // elles tremblent un peu
        float rInterieur = 250 + hasard(70);
        float l = 0.022f + hasard(0.01f);
        ecran.fillTriangle(640 + 900 * cosf(a - l), 360 + 900 * sinf(a - l),
                           640 + 900 * cosf(a + l), 360 + 900 * sinf(a + l),
                           640 + rInterieur * cosf(a), 360 + rInterieur * sinf(a), BLANC);
      }
    }
    ecran.clearClipRect();
    ecran.fillRect(glisse, haut - 12, 1280, 12, BLANC);         // bords blancs de la bande
    ecran.fillRect(glisse, haut + h, 1280, 12, BLANC);
    // le texte
    if (t > 0.12f) {
      ecran.setTextDatum(middle_center);
      texteContour("MODE ÉTOILE !", 640, 565, OR, 2.0f);
      texteContour("SHIIING !", 1050, 125, BLANC, 1.1f);
    }
}

// Une ligne du classement final : « 1. Pixo  120 pts (41,5 s) » ou « (éliminé) »
void ligneResultat(char* t, size_t n, int rang, const LigneClassement& l) {
  if (l.statut == 1)      snprintf(t, n, "%d. %s : %d pts (%d,%d s)", rang, l.nom, l.score, l.temps / 1000, (l.temps % 1000) / 100);
  else if (l.statut == 0) snprintf(t, n, "%d. %s : %d pts (éliminé)", rang, l.nom, l.score);
  else                    snprintf(t, n, "%d. %s : %d pts (pas fini)", rang, l.nom, l.score);
}

// Fenêtre de fin d'une partie multijoueur
void dessinerFinMulti() {
  char texte[96];
  if (tempsFin < 1.0f) return;                      // on laisse d'abord le bonhomme finir
  bulle(240, 40, 800, 550, 34, BLANC);
  ecran.setTextDatum(middle_center);
  if (!finalPret) {
    texteContour("TERMINÉ !", 640, 105, OR, 1.5f);
    ecran.setTextColor(MARINE);
    snprintf(texte, sizeof(texte), "Ton score : %d", score);
    ecran.drawString(texte, 640, 260);
    ecran.drawString("En attente des autres joueurs...", 640, 340);
    for (int k = 0; k < 3; k++)
      ecran.fillCircle(600 + k * 40, 400, 8 + 4 * sinf(horloge * 6 + k), TURQUOISE);
    if (serveurPerdu()) ecran.drawString("Le serveur ne répond plus", 640, 450);
    dessinerBouton(BOUTON_QUITTER_M, 0.9f, false);
    return;
  }
  if (rangFinal == 1) { texteContour("1er ! BRAVO !", 640, 105, OR, 1.5f); etoile(1010, 110, 34, OR); etoile(270, 110, 34, OR); }
  else {
    snprintf(texte, sizeof(texte), "%de sur %d", rangFinal, nbFinale);
    texteContour(texte, 640, 105, rangFinal * 2 <= nbFinale ? TURQUOISE : ROUGE, 1.5f);
  }
  ecran.setTextColor(MARINE);
  ecran.setTextDatum(middle_left);
  int lignes = nbFinale < 6 ? nbFinale : 6;          // on affiche les 6 premiers
  for (int i = 0; i < lignes; i++) {
    ligneResultat(texte, sizeof(texte), i + 1, finale[i]);
    if (i + 1 == rangFinal) ecran.fillRoundRect(300, 175 + i * 46 - 20, 680, 40, 14, OR_CLAIR);
    ecran.setTextColor(MARINE);
    ecran.drawString(texte, 320, 175 + i * 46);
  }
  dessinerBouton(BOUTON_ACCUEIL_M, 0.9f, false);
  dessinerBouton(BOUTON_REJOUER_M, 0.9f, true);
}

void dessinerPartie() {
  theme = (epee > 0) ? &THEME_FORET : &THEME_JOUR;     // le décor change avec l'épée
  dessinerCiel();
  if (epee > 0) dessinerLignesVitesse();
  dessinerNiveau();
  dessinerPieces();
  dessinerMonstres();
  dessinerBonus();
  dessinerDrapeau();
  dessinerNuageSaut();
  dessinerEclats();
  // le grand texte du bonus est dessiné AVANT le bonhomme : il ne le cache jamais
  if (annonceT > 0 && etat == JEU) dessinerAnnonce();
  float bx = joueurX + JOUEUR_L / 2 - camera;
  float pieds = joueurY + hauteurJoueur;
  const Perso& q = PERSOS[persoChoisi];
  // En mode étoile, le même personnage avec la tenue du héros
  Perso heros = (epee > 0) ? persoHeros() : q;
  // il clignote juste après que la bulle a éclaté ; les pouvoirs clignotent
  // aussi pendant leur dernière seconde et demie
  bool cache = invincible > 0 && fmodf(horloge, 0.16f) < 0.07f;
  bool clignote = fmodf(horloge, 0.2f) < 0.08f;
  if (etat == PERDU) {
    dessinerPerso(q, bx, yChute + JOUEUR_H, 1.0f, true, 0);
  } else {
    if (cache) {
      // rien : il est caché une image sur deux
    } else if (roulade > 0) {
      if (epee > 0) dessinerAura(bx, pieds + 22);
      dessinerBoule(heros, bx, pieds);
    } else {
      if (epee > 0) {
        dessinerAura(bx, pieds);                              // aura derrière lui
        dessinerCape(bx, pieds, !auSol);
      }
      dessinerPerso(heros, bx, pieds, 1.0f, !auSol, animation);
      if (epee > 0) {
        etoileAvecBord(bx + 2, pieds - 52, 6, BLANC, 2);      // étoile sur la poitrine
        if (!(epee < 1.5f && clignote)) dessinerEpee(heros, bx, pieds, !auSol, animation);
      }
    }
    if (epee > 0) dessinerExplosionEtoiles(bx, pieds);
    dessinerOnomatopees();
    if (bulleProtege) dessinerBulleProtection(bx, pieds - 50);
    if (aimant > 0 && !(aimant < 1.5f && clignote))
      iconeBonus('A', bx, pieds - hauteurJoueur - 48, 0.7f); // petit aimant au-dessus de lui
  }
  if (theme->neige) dessinerNeige();     // la neige tombe devant tout le décor
  if (flash > 0) {                       // éclair blanc : une ligne sur 2, puis sur 4
    int pas = (flash > 0.1f) ? 2 : 4;
    for (int ligne = 0; ligne < 720; ligne += pas) ecran.fillRect(0, ligne, 1280, 1, BLANC);
  }
  if (etat == FIN) { if (modeMulti) dessinerFinMulti(); else dessinerFin(); }
  else if (etat == PERDU) { if (modeMulti) dessinerFinMulti(); else dessinerPerdu(); }
  else dessinerInfos();
  if (etat == PAUSE) dessinerPause();
}

// ---------- Écrans du mode multijoueur ----------
void dessinerFondMulti() {
  theme = &THEME_JOUR;                       // les menus sont toujours de jour
  camera = horloge * 40;                     // le décor défile doucement
  dessinerCiel();
  dessinerSol();
  ecran.setTextDatum(middle_center);
  texteContour("MULTIJOUEUR", 680, 62, OR, 1.3f);
}

void troisPoints(int y) {                          // trois points qui pulsent
  for (int k = 0; k < 3; k++)
    ecran.fillCircle(600 + k * 40, y, 8 + 4 * sinf(horloge * 6 + k), TURQUOISE);
}

void dessinerLobby() {
  char texte[96];
  dessinerFondMulti();
  dessinerBouton(BOUTON_RETOUR, 1.0f, false);
  iconeMaison(BOUTON_RETOUR.x + BOUTON_RETOUR.l / 2, BOUTON_RETOUR.y + BOUTON_RETOUR.h / 2 + 4);
  bulle(120, 110, 1040, 470, 34, BLANC);
  ecran.setTextDatum(middle_center);
  ecran.setTextColor(MARINE);
  if (reseauEtat == 3) {
    ecran.drawString("Pas de Wi-Fi...", 640, 190);
    ecran.drawString("Partage de connexion actif ? (2,4 GHz)", 640, 255);
    ecran.drawString(WIFI_SSID, 640, 310);
    dessinerBouton(BOUTON_REESSAYER, 1.0f, true);
  } else if (reseauEtat != 2) {
    ecran.drawString("Connexion au Wi-Fi...", 640, 190);
    ecran.drawString(WIFI_SSID, 640, 250);
    ecran.fillArc(640, 400, 24, 38, horloge * 360, horloge * 360 + 240, TURQUOISE);
  } else if (!serveurConnu) {
    ecran.drawString("Wi-Fi connecté !", 640, 170);
    ecran.drawString("Recherche du serveur du jeu...", 640, 245);
    ecran.drawString("(le serveur admin tourne-t-il sur le PC ?)", 640, 310);
    troisPoints(400);
  } else {
    snprintf(texte, sizeof(texte), "Connecté au serveur !  Tu es %s (%s)", PERSOS[persoChoisi].nom, monId);
    ecran.drawString(texte, 640, 175);
    snprintf(texte, sizeof(texte), "Joueurs dans le salon : %d", nbJoueurs);
    ecran.drawString(texte, 640, 260);
    ecran.drawString("L'admin va lancer la partie...", 640, 340);
    troisPoints(420);
  }
}

// Le niveau est affiché, avec un gros 3, 2, 1 par-dessus
void dessinerCompte() {
  dessinerPartie();
  int sec = 3 - (int)((millis() - compteDebut) / 1000);
  if (sec < 1) sec = 1;
  char t[4];
  snprintf(t, sizeof(t), "%d", sec);
  ecran.setTextDatum(middle_center);
  texteContour(t, 640, 300, OR, 4.0f);
  texteContour("Prêt ?", 640, 450, BLANC, 1.2f);
}

// =====================================================================
// 10. SETUP ET LOOP
// =====================================================================
void setup() {
  M5.begin();
  M5.Display.setRotation(0);                     // l'écran reste en portrait...
  ecran.setColorDepth(M5.Display.getColorDepth());
  ecran.setPsram(true);                          // le canvas (1,8 Mo) va en PSRAM
    ecran.createSprite(720, 1280);
    ecran.setRotation(ROTATION);                   // ...mais on dessine en paysage
    ecran.setFont(&fonts::lgfxJapanGothicP_40);    // police qui gère les accents

    // Lecture de ce qui a été enregistré : records, étoiles, volume
    memoire.begin("gametab", false);
    char cle[8];
    for (int i = 0; i < NB_NIVEAUX; i++) {
      snprintf(cle, sizeof(cle), "rec%d", i); records[i] = memoire.getInt(cle, 0);
      snprintf(cle, sizeof(cle), "etoi%d", i); etoiles[i] = memoire.getInt(cle, 0);
    }
    niveauSon = memoire.getInt("son", 3);
    M5.Speaker.setVolume(niveauSon * 50);
    persoChoisi = memoire.getInt("perso", 0);
    if (persoChoisi < 0 || persoChoisi >= NB_PERSOS) persoChoisi = 0;

    chargerNiveau(0);
    dernierMs = debutSeconde = millis();
}

// Lance (ou relance) un niveau
void commencerPartie(int n, int x, int y) {
  modeMulti = false;                         // partie seul
  graine ^= millis() * 2654435761u;          // le hasard change à chaque partie
  chargerNiveau(n);
  jouerMelodie(MELODIE_START, NB_NOTES(MELODIE_START));
  ignorerCeToucher(x, y);
  etat = JEU;
}

// Appelé quand le bonhomme touche un monstre
void perdre() {
  etat = PERDU;
  tempsFin = 0;
  yChute = joueurY - (JOUEUR_H - hauteurJoueur); // il se relève pour tomber
  vitesseChute = -900;                            // petit bond, puis il tombe de l'écran
  M5.Speaker.tone(180, 120, 0, true);             // « boing »
  jouerMelodie(MELODIE_PERDU, NB_NOTES(MELODIE_PERDU));
  if (modeMulti) terminerMulti(false);            // je le dis au serveur
}

// Appelé quand le bonhomme touche le drapeau
void finirNiveau() {
  etat = FIN;
  tempsFin = 0;
  if (modeMulti) {                                // multijoueur : pas d'étoiles ni de record
    terminerMulti(true);
    jouerMelodie(MELODIE_ARRIVEE, NB_NOTES(MELODIE_ARRIVEE));
    return;
  }
  etoilesAffichees = 0;
  fanfareJouee = false;
  float part = nbPieces > 0 ? (float)piecesPrises / nbPieces : 1;
  etoilesPartie = 1 + (part >= 0.5f) + (part >= 0.8f);

    char cle[8];
    nouveauRecord = score > records[numeroNiveau];
    if (nouveauRecord) {
      records[numeroNiveau] = score;
      snprintf(cle, sizeof(cle), "rec%d", numeroNiveau);
      memoire.putInt(cle, score);                  // enregistré pour la prochaine fois
    }
    if (etoilesPartie > etoiles[numeroNiveau]) {
      etoiles[numeroNiveau] = etoilesPartie;
      snprintf(cle, sizeof(cle), "etoi%d", numeroNiveau);
      memoire.putInt(cle, etoilesPartie);
    }
    jouerMelodie(MELODIE_ARRIVEE, NB_NOTES(MELODIE_ARRIVEE));
}

// Pendant l'écran de fin : étoiles une par une, puis fanfare si record
void animerFin(float dt) {
  tempsFin += dt;
  int aMontrer = (int)((tempsFin - 0.8f) / 0.35f) + 1;    // une étoile toutes les 0,35 s
  if (aMontrer > etoilesPartie) aMontrer = etoilesPartie;
  if (aMontrer > etoilesAffichees) {
    etoilesAffichees = aMontrer;
    M5.Speaker.tone(1319 + 400 * etoilesAffichees, 120, 0, true);    // « ding »
  }
  if (!fanfareJouee && tempsFin > 1.0f + 0.35f * etoilesPartie) {
    fanfareJouee = true;
    if (nouveauRecord) jouerMelodie(MELODIE_RECORD, NB_NOTES(MELODIE_RECORD));
  }
}

// Boutons de la fenêtre de fin d'une partie multijoueur
void boutonsFinMulti(int x, int y) {
  if (finalPret && dansBouton(BOUTON_REJOUER_M, x, y))       { clic(); modeMulti = false; etat = LOBBY_MULTI; }
  else if (finalPret && dansBouton(BOUTON_ACCUEIL_M, x, y))  { clic(); quitterMulti(); etat = ACCUEIL; }
  else if (!finalPret && dansBouton(BOUTON_QUITTER_M, x, y)) { clic(); quitterMulti(); etat = ACCUEIL; }
}

// Une mélodie quand le classement final est connu
void sonResultatMulti() {
  if (resultatJoue || tempsFin < 1.0f || !finalPret) return;
  resultatJoue = true;
  if (rangFinal == 1)                 jouerMelodie(MELODIE_RECORD, NB_NOTES(MELODIE_RECORD));
  else if (rangFinal * 2 <= nbFinale) jouerMelodie(MELODIE_ARRIVEE, NB_NOTES(MELODIE_ARRIVEE));
  else                                jouerMelodie(MELODIE_PERDU, NB_NOTES(MELODIE_PERDU));
}

void loop() {
  M5.update();
  uint32_t maintenant = millis();
  float dt = (maintenant - dernierMs) / 1000.0f;
  dernierMs = maintenant;
  if (dt > 0.1f) dt = 0.1f;
  horloge += dt;

  // Mode multijoueur : lecture des messages du serveur
  bool reseauActif = (reseauEtat == 2) &&
      (modeMulti || etat == LOBBY_MULTI || etat == COMPTE);
  if (reseauActif) { lireReseau(); envoisPeriodiques(maintenant); }

  // Lecture du doigt, en coordonnées paysage
  auto doigt = M5.Touch.getDetail();
  int x, y;
  versPaysage(doigt.x, doigt.y, x, y);
  bool appui = doigt.wasPressed();            // le doigt vient de toucher l'écran

  switch (etat) {
    case ACCUEIL:
      if (appui && dansBouton(BOUTON_START, x, y)) {
        jouerMelodie(MELODIE_START, NB_NOTES(MELODIE_START));
        etat = CHOIX_NIVEAU;
      } else if (appui && dansBouton(BOUTON_PERSO, x, y)) {
        clic();
        etat = CHOIX_PERSO;
      } else if (appui && dansBouton(BOUTON_MULTI, x, y)) {
        clic();
        wifiDemarrer();
        etat = LOBBY_MULTI;
      }
      if (etat == CHOIX_PERSO) dessinerChoixPerso();
      else if (etat == CHOIX_NIVEAU) dessinerChoixNiveau();
      else if (etat == LOBBY_MULTI) dessinerLobby();
      else dessinerAccueil();
      break;

    case CHOIX_PERSO:
      if (appui && dansBouton(BOUTON_RETOUR, x, y)) { clic(); etat = ACCUEIL; }
      else if (appui && dansBouton(BOUTON_OK, x, y)) {
        jouerMelodie(MELODIE_START, NB_NOTES(MELODIE_START));
        etat = CHOIX_NIVEAU;
      } else if (appui) {
        for (int i = 0; i < NB_PERSOS; i++)
          if (dansBouton(cartePerso(i), x, y)) {
            persoChoisi = i;
            memoire.putInt("perso", i);             // gardé pour la prochaine fois
            M5.Speaker.tone(1047 + 120 * i, 80, 0, true);   // chaque perso a sa note
          }
      }
      if (etat == ACCUEIL) dessinerAccueil();
      else if (etat == CHOIX_NIVEAU) dessinerChoixNiveau();
      else dessinerChoixPerso();
      break;

    case CHOIX_NIVEAU:
      if (appui && dansBouton(BOUTON_RETOUR, x, y)) { clic(); etat = ACCUEIL; }
      for (int i = 0; i < NB_NIVEAUX; i++)
        if (appui && dansBouton(carteNiveau(i), x, y)) commencerPartie(i, x, y);
      if (etat == CHOIX_NIVEAU) dessinerChoixNiveau();
      else if (etat == ACCUEIL) dessinerAccueil();
      else dessinerPartie();
      break;

    case JEU: {
      if (sequenceTransfo > 0) {
        // pendant la séquence de transformation, le doigt ne compte pas
        if (doigt.isPressed()) ignorerCeToucher(x, y);
      } else if (!modeMulti && appui && x > 1140 && y < 110) {
        // zone du bouton pause un peu plus grande que le dessin (plus facile à toucher)
        clic();
        ignorerCeToucher(x, y);
        etat = PAUSE;
      } else {
        Glissement g = lireGlissement(doigt.isPressed(), x, y, maintenant);
        if (g == VERS_LE_HAUT) memoireSaut = MEMOIRE_SAUT;
        if (g == VERS_LE_BAS) {
          memoireRoulade = 0.4f;
          if (!auSol && vitesseY < 700) vitesseY = 700;    // en l'air : il plonge
        }
      }
      if (etat == JEU) {
        uint16_t ev = avancerJeu(dt);
        jouerSons(ev);
        if (ev & EV_TOUCHE) perdre();
        else if (ev & EV_ARRIVEE) finirNiveau();
      }
      if (sequenceTransfo > 0) dessinerSequenceTransfo();
      else dessinerPartie();
      break;
    }

    case PAUSE:                              // le jeu est figé
      if (appui) {
        if (dansBouton(BOUTON_REPRENDRE, x, y))   { clic(); ignorerCeToucher(x, y); etat = JEU; }
        else if (dansBouton(BOUTON_MUET, x, y)) {
          if (niveauSon > 0) { sonAvantMuet = niveauSon; reglerSon(0); }
          else               { reglerSon(sonAvantMuet > 0 ? sonAvantMuet : 3); clic(); }
        }
        else if (dansBouton(BOUTON_MOINS, x, y))  { reglerSon(niveauSon - 1); clic(); }
        else if (dansBouton(BOUTON_PLUS, x, y))   { reglerSon(niveauSon + 1); clic(); }
        else if (dansBouton(BOUTON_RECOMMENCER, x, y)) commencerPartie(numeroNiveau, x, y);
        else if (dansBouton(BOUTON_ACCUEIL, x, y)) { clic(); etat = ACCUEIL; }
      }
      if (etat == ACCUEIL) dessinerAccueil();
      else dessinerPartie();
      break;

    case PERDU:                              // le bonhomme tombe, puis la fenêtre s'ouvre
      tempsFin += dt;
      vitesseChute += GRAVITE * dt;
      yChute += vitesseChute * dt;
      if (modeMulti) {
        if (appui && tempsFin > 1.0f) boutonsFinMulti(x, y);
        sonResultatMulti();
      } else if (appui && tempsFin > 1.0f) {
        if (dansBouton(BOUTON_NIVEAUX_P, x, y))      { clic(); etat = CHOIX_NIVEAU; }
        else if (dansBouton(BOUTON_REJOUER_P, x, y)) commencerPartie(numeroNiveau, x, y);
      }
      if (etat == CHOIX_NIVEAU) dessinerChoixNiveau();
      else if (etat == LOBBY_MULTI) dessinerLobby();
      else if (etat == ACCUEIL) dessinerAccueil();
      else dessinerPartie();
      break;

    case FIN:
      if (modeMulti) {
        tempsFin += dt;
        if (appui && tempsFin > 1.0f) boutonsFinMulti(x, y);
        sonResultatMulti();
      } else {
        animerFin(dt);
        if (appui && tempsFin > 1.0f) {
          if (dansBouton(BOUTON_NIVEAUX, x, y))      { clic(); etat = CHOIX_NIVEAU; }
          else if (dansBouton(BOUTON_REJOUER, x, y)) commencerPartie(numeroNiveau, x, y);
          else if (dansBouton(BOUTON_SUIVANT, x, y) && numeroNiveau + 1 < NB_NIVEAUX)
            commencerPartie(numeroNiveau + 1, x, y);
        }
      }
      if (etat == CHOIX_NIVEAU) dessinerChoixNiveau();
      else if (etat == LOBBY_MULTI) dessinerLobby();
      else if (etat == ACCUEIL) dessinerAccueil();
      else dessinerPartie();
      break;

    // ----- Mode multijoueur -----
    case LOBBY_MULTI:                        // Wi-Fi, recherche du serveur, attente du départ de l'admin
      majWifi();
      if (appui && dansBouton(BOUTON_RETOUR, x, y)) { clic(); quitterMulti(); etat = ACCUEIL; }
      else if (reseauEtat == 3 && appui && dansBouton(BOUTON_REESSAYER, x, y)) { clic(); wifiDemarrer(); }
      if (etat == ACCUEIL) dessinerAccueil();
      else if (etat == COMPTE) dessinerCompte();
      else dessinerLobby();
      break;

    case COMPTE:                             // 3, 2, 1... puis la course commence
      if (millis() - compteDebut >= 3000) {
        etat = JEU;
        reste = 0;
        ignorerCeToucher(0, 0);
        jouerMelodie(MELODIE_START, NB_NOTES(MELODIE_START));
        dessinerPartie();
      } else {
        dessinerCompte();
      }
      break;
  }

  majMelodie();
  ecran.pushSprite(0, 0);                    // envoi de l'image à l'écran

  // Compteur d'images par seconde
  imagesCompteur++;
  if (maintenant - debutSeconde >= 1000) {
    fps = imagesCompteur;
    imagesCompteur = 0;
    debutSeconde = maintenant;
  }
}
