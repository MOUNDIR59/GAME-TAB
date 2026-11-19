# Guide de dépannage (troubleshooting) : GAME TAB

Ce guide liste les problèmes les plus courants, leur cause probable et la solution.
Il suit l'ordre de mise en route : **compilation → tablette → Wi-Fi → serveur → partie multijoueur**.

## Diagnostic rapide

| Ce que vous voyez | Section |
|---|---|
| Erreur dans Arduino IDE à la compilation ou au téléversement | [1. Compilation et téléversement](#1-compilation-et-téléversement) |
| Écran noir, image tournée, tactile inversé, couleurs bizarres | [2. Affichage et tactile](#2-affichage-et-tactile) |
| Le saut ou la roulade part mal, le jeu rame, pas de son | [3. Jeu en solo](#3-jeu-en-solo) |
| La tablette reste sur « connexion Wi-Fi » ou affiche RÉESSAYER | [4. Wi-Fi de la tablette](#4-wi-fi-de-la-tablette) |
| La tablette est connectée mais ne trouve jamais le serveur | [5. La tablette ne trouve pas le serveur](#5-la-tablette-ne-trouve-pas-le-serveur) |
| Le serveur ne démarre pas, la page admin ne s'ouvre pas | [6. Serveur et page admin](#6-serveur-et-page-admin) |
| Problème pendant une partie multijoueur | [7. Partie multijoueur](#7-partie-multijoueur) |

---

## 1. Compilation et téléversement

| Symptôme | Cause probable | Solution |
|---|---|---|
| `M5Unified.h: No such file or directory` | Bibliothèque non installée | Arduino IDE → *Outils > Gérer les bibliothèques* → installer **M5Unified** (installe aussi M5GFX). |
| Carte « M5Tab5 » absente de la liste | Paquet de cartes manquant ou trop ancien | Ajouter le paquet de cartes **M5Stack** (ou **esp32** d'Espressif, version 3.x récente qui gère l'ESP32-P4) dans le gestionnaire de cartes, puis choisir la carte Tab5. |
| `'LigneClassement' does not name a type` | La structure a été déplacée plus bas dans le fichier | Arduino IDE déclare les fonctions en haut du fichier : la `struct LigneClassement` doit rester **en haut**, dans la partie 1 (Réglages). |
| `WiFi.setPins` inconnu | Version du paquet esp32 trop ancienne | Mettre à jour le paquet de cartes (le Wi-Fi de la Tab5 passe par une puce ESP32-C6 reliée en SDIO). |
| Le téléversement échoue, port COM introuvable | Câble USB qui ne fait que charger, mauvais port | Utiliser un câble USB-C **de données**, choisir le bon port dans *Outils > Port*, débrancher et rebrancher la tablette. |
| Le téléversement échoue encore | La tablette n'est pas en mode téléchargement | Rebrancher la tablette, appuyer sur le bouton reset, puis relancer le téléversement. |

## 2. Affichage et tactile

| Symptôme | Cause probable | Solution |
|---|---|---|
| Écran noir après le téléversement | Le canvas (720 x 1280, environ 1,8 Mo) n'a pas pu être créé | Vérifier que la **PSRAM** est activée dans le menu *Outils* de la carte. Le code utilise `ecran.setPsram(true)`. |
| Image à l'envers | Rotation différente selon la tablette | Changer `ROTATION` (partie 1 du `.ino`) : `1` ou `3`. |
| Le doigt touche à côté des boutons | Les coordonnées tactiles ne suivent pas la rotation | `versPaysage()` dépend de `ROTATION` : garder la même valeur qui donne une image à l'endroit. |
| Une couleur ne correspond pas à ce qui est écrit | Couleur écrite directement en nombre | Toujours passer par une constante nommée (`ROSE`, `OR`...), comme expliqué dans la partie 3 du code. |
| Accents mal affichés | Police changée | Garder `fonts::lgfxJapanGothicP_40`, qui gère les accents. |

## 3. Jeu en solo

| Symptôme | Cause probable | Solution |
|---|---|---|
| La roulade part alors qu'on voulait sauter | Le geste est reconnu trop tôt | Augmenter `DELAI_TAP` (45 ms par défaut). |
| Le saut paraît mou, en retard | Délai trop long avant de reconnaître un toucher | Baisser `DELAI_TAP`, **sans descendre sous 30**. |
| Les touchers accidentels font sauter | Seuils trop bas | Augmenter `SEUIL_GLISSE` (32 px) et `SEUIL_BOUGE` (12 px). |
| Un long glissement ne fait pas saut + double saut | Comportement voulu | Un seul geste par contact : il faut lever le doigt entre deux gestes. |
| Moins de 30 images/s (compteur en haut à droite) | Trop de choses dessinées | Vérifier que le décor du mode étoile n'a pas été alourdi ; mettre `AFFICHER_FPS = true` pour mesurer. |
| Plus aucun son | Le volume a été mis à 0 et il est **mémorisé** | Menu pause → bouton son ou `+`. |
| Remettre les records et étoiles à zéro | Les valeurs sont gardées en mémoire flash (`Preferences`, espace `gametab`) | Dans Arduino IDE, *Outils > Erase All Flash Before Sketch Upload : Enabled*, puis téléverser une fois. |
| Le personnage « perd » sans toucher de monstre | Il est resté coincé contre une caisse ou un mur | L'écran continue d'avancer : s'il sort par la gauche, c'est perdu (règle du jeu, REQ-09). Sauter plus tôt. |

## 4. Wi-Fi de la tablette

Au bout de **20 s** sans connexion, la tablette affiche le bouton **RÉESSAYER**.

| Cause probable | Solution |
|---|---|
| Nom ou mot de passe du Wi-Fi faux | Modifier `WIFI_SSID` et `WIFI_MDP` en haut du `.ino` (attention aux majuscules et aux espaces), puis téléverser de nouveau. |
| Réseau en **5 GHz** seulement | La tablette ne voit que le **2,4 GHz**. Activer la bande 2,4 GHz sur la box. |
| Partage de connexion d'un iPhone | Activer « **Maximiser la compatibilité** » (passe le partage en 2,4 GHz). |
| Réseau avec page de connexion (hôtel, école, eduroam) | Ces réseaux ne sont pas compatibles : utiliser le **partage de connexion d'un téléphone** ou un petit routeur. |
| Broches SDIO modifiées | Les `#define SDIO2_...` ne doivent pas être changés. |
| Tablette trop loin du point d'accès | Rapprocher la tablette et le téléphone / la box. |

## 5. La tablette ne trouve pas le serveur

La tablette est connectée au Wi-Fi mais reste en « recherche du serveur ».
Elle envoie `HELLO` en **broadcast** sur le port UDP **4210** et attend la réponse `LB` du PC.

| Cause probable | Solution |
|---|---|
| Le serveur n'est pas lancé | Lancer `py serveur_admin.py` sur le PC et laisser la fenêtre ouverte. |
| Le PC n'est pas sur le **même réseau** que les tablettes | Le PC doit être sur le même Wi-Fi (pas sur un câble Ethernet d'un autre réseau, pas sur le réseau « invité »). |
| **Pare-feu Windows** qui bloque l'UDP entrant | À la première ouverture, accepter la fenêtre du pare-feu pour Python en cochant **Réseaux privés**. Sinon, dans un PowerShell **administrateur** : voir la commande ci-dessous. |
| Le réseau Windows est en profil « Public » | *Paramètres > Réseau et Internet > Wi-Fi >* le réseau → **Réseau privé**. |
| Isolation des clients sur le point d'accès (« AP isolation ») | Les appareils ne se voient pas entre eux : désactiver cette option ou utiliser le partage de connexion d'un téléphone. |
| Port changé d'un seul côté | `--udp` du serveur et `PORT_JEU` du `.ino` doivent être **identiques** (4210 par défaut). |
| Un VPN est actif sur le PC | Couper le VPN pendant la partie. |

Ouvrir le port UDP 4210 (PowerShell en administrateur) :

```powershell
New-NetFirewallRule -DisplayName "GAME TAB UDP 4210" -Direction Inbound -Protocol UDP -LocalPort 4210 -Action Allow -Profile Private
```

Vérifier le profil du réseau :

```powershell
Get-NetConnectionProfile
```

## 6. Serveur et page admin

| Symptôme | Cause probable | Solution |
|---|---|---|
| `python3` n'est pas reconnu (Windows) | Sous Windows, la commande est `py` | Lancer `py serveur_admin.py`. Si `py` est inconnu aussi, installer Python 3 depuis python.org. |
| `OSError: [Errno 98]` / `[WinError 10048]` (adresse déjà utilisée) | Le serveur tourne déjà, ou un autre programme utilise le port | Fermer l'autre fenêtre du serveur, ou changer de port : `py serveur_admin.py --http 8081`. Si vous changez `--udp`, changez aussi `PORT_JEU` dans le `.ino`. |
| L'adresse affichée est `127.0.0.1` | Le PC n'est connecté à aucun réseau | Connecter le PC au Wi-Fi des tablettes et relancer le serveur. |
| La page admin s'ouvre sur le PC mais pas sur un autre appareil | Pare-feu sur le port TCP 8080 | Autoriser Python sur les réseaux privés, ou ouvrir le port TCP 8080 comme pour l'UDP ci-dessus (`-Protocol TCP -LocalPort 8080`). |
| La page affiche « serveur injoignable » | Le serveur a été arrêté (Ctrl+C) ou le PC a changé de réseau | Relancer le serveur et recharger la page. |
| Les boutons répondent « mot de passe incorrect » | Le serveur a été lancé avec `--mdp` | Ouvrir la page avec `http://ADRESSE:8080/?mdp=MOTDEPASSE`. |
| Le Top 3 n'est pas gardé après un redémarrage | Le dossier du script n'est pas modifiable, ou `meilleurs_scores.json` est abîmé | Mettre `serveur_admin.py` dans un dossier où l'on peut écrire (pas dans *Program Files*) ; supprimer `meilleurs_scores.json` s'il est abîmé (il sera recréé). |

## 7. Partie multijoueur

| Symptôme | Cause probable | Solution |
|---|---|---|
| DÉMARRER répond « aucun joueur dans le salon » | Aucune tablette n'a dit `HELLO` dans les 2,5 dernières secondes | Vérifier que les tablettes sont sur l'écran du salon et apparaissent « prêt » dans la page admin. |
| Une 9e tablette n'apparaît pas | Limite de **8 joueurs** | Comportement voulu (`MAX_JOUEURS = 8`). |
| Deux joueurs ont un nom comme `Pixo#A1` | Deux tablettes ont choisi le même personnage | Normal : le serveur ajoute la fin de l'identifiant pour les distinguer. |
| Une tablette exclue ne peut pas revenir | Elle est bannie pendant **15 s** | Attendre 15 s puis revenir dans MULTIJOUEUR. |
| Un joueur passe à « parti » pendant la course | Aucune nouvelle de la tablette depuis plus de **8 s** (Wi-Fi perdu, batterie vide) | Rapprocher la tablette du point d'accès, vérifier la batterie. La partie continue sans lui. |
| La course ne s'arrête pas | Un joueur n'a pas encore fini | La partie finit quand tous les joueurs ont fini ou sont partis, au bout de **5 min**, ou avec **ARRÊTER LA PARTIE**. |
| Une tablette reste sur l'écran de fin avec seulement « QUITTER » | Elle n'a pas reçu le classement final (envoyé pendant 4 s après la fin) | Appuyer sur **QUITTER**, puis revenir dans MULTIJOUEUR pour la manche suivante. |
| Les bonus ne sont pas aux mêmes endroits sur toutes les tablettes | Toutes les tablettes n'ont pas la même version du programme | Téléverser **le même** `GAME_TAB_MULTI.ino` sur toutes les tablettes. |
| Classement saccadé, tablettes en retard | Wi-Fi chargé ou trop loin | Rapprocher les appareils, éviter les autres usages du partage de connexion pendant la partie. |
| Pas de pause en multijoueur | Comportement voulu | La pause n'existe qu'en solo (la course est commune). |

---

### Toujours bloqué ?

1. Tester d'abord le **mode solo** : s'il marche, la tablette et le programme sont bons, le problème vient du réseau.
2. Lancer le serveur **sur le PC** et ouvrir `http://localhost:8080` : si la page s'ouvre, le serveur est bon.
3. Vérifier que le PC et les tablettes sont sur **le même réseau 2,4 GHz**, puis le **pare-feu**.
4. Regarder la fenêtre du serveur : un message `message ignoré : ...` indique un message reçu mais mal formé.
