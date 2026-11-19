#!/usr/bin/env python3
"""
GAME TAB : serveur web admin du mode MULTIJOUEUR.

Il tourne sur un PC relié au MÊME Wi-Fi que les tablettes (Python 3, rien à installer).
  - Les tablettes lui envoient en UDP (port 4210) : HELLO (je suis dans le salon),
    PRG (mon score), FIN (j'ai fini), BYE (je pars).
  - Le serveur gère la partie en temps réel : il choisit le niveau et la graine des bonus,
    donne le départ commun (compte à rebours), calcule le classement en direct, décide de
    la fin de la partie, et envoie le résultat à chaque tablette.
  - L'admin pilote tout depuis une page web : http://ADRESSE_DU_PC:8080

Lancer :   python3 serveur_admin.py            (Windows : py serveur_admin.py)
Options :  --http 8080   --udp 4210   --mdp MOTDEPASSE (protège les boutons de l'admin)
"""
import argparse, json, os, random, socket, threading, time, urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PERSOS = ["Pixo", "Pépite", "Plume", "Kiwi", "Comète", "Flip", "Fusée", "Zip"]
NIVEAUX = ["Facile", "Moyen", "Difficile"]
MAX_JOUEURS = 8
COMPTE_MS = 3000        # compte à rebours 3-2-1
SALON_MS = 2500         # une tablette est « dans le salon » si elle a dit HELLO il y a moins de ça
SILENCE_MS = 8000       # une tablette muette plus longtemps que ça est considérée partie
DUREE_MAX_S = 300       # une course s'arrête toute seule au bout de 5 minutes

FICHIER_TOP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "meilleurs_scores.json")
lock = threading.RLock()
sock = None
S = {
    "phase": "salon",      # salon -> compte -> course -> termine
    "niveau": 0, "seed": 1,
    "t_phase": 0.0,
    "joueurs": {},          # id -> dict
    "bannis": {},           # id -> fin du bannissement
    "participants": [],     # ids de la partie en cours
    "resultat": [],         # classement final
    "manche": 0,
    "meilleurs": [],        # tableau d'honneur : meilleurs résultats, gardés d'une partie à l'autre
}

def charger_top():
    try:
        with open(FICHIER_TOP, encoding="utf-8") as f: S["meilleurs"] = json.load(f)
    except (OSError, ValueError): S["meilleurs"] = []

def sauver_top():
    try:
        with open(FICHIER_TOP, "w", encoding="utf-8") as f: json.dump(S["meilleurs"], f, ensure_ascii=False)
    except OSError: pass

def cle_top(e):                       # meilleur score d'abord ; à égalité, le plus rapide
    return (-e["score"], e["temps"] if e["arrive"] else 10**9)

def noter_meilleurs():
    """Après une partie : chaque joueur qui a marqué des points entre dans le tableau d'honneur
    (un seul résultat par joueur : son meilleur). On garde les 10 meilleurs."""
    for rang, pid in enumerate(S["resultat"], 1):
        p = S["joueurs"].get(pid)
        if not p or p["score"] <= 0: continue
        e = dict(nom=nom_affiche(pid), id=pid, score=p["score"], temps=p["temps"],
                 arrive=p["statut"] == 1, niveau=NIVEAUX[S["niveau"]], manche=S["manche"])
        ancien = next((x for x in S["meilleurs"] if x["id"] == pid), None)
        if ancien and cle_top(ancien) <= cle_top(e): continue
        if ancien: S["meilleurs"].remove(ancien)
        S["meilleurs"].append(e)
    S["meilleurs"].sort(key=cle_top)
    del S["meilleurs"][10:]
    sauver_top()

def maintenant(): return time.monotonic()
def ms(t): return int(t * 1000)

def nom_affiche(pid):
    p = S["joueurs"].get(pid)
    if not p: return pid
    base = PERSOS[p["perso"] % len(PERSOS)]
    mêmes = [q for q in S["joueurs"].values() if q["perso"] == p["perso"]]
    return base if len(mêmes) < 2 else f"{base}#{pid[-2:]}"

def envoyer(pid, texte):
    p = S["joueurs"].get(pid)
    if p and p.get("addr"):
        try: sock.sendto(texte.encode("utf-8"), p["addr"])
        except OSError: pass

def dans_salon(p): return ms(maintenant() - p["hello"]) < SALON_MS

# ---------------------------------------------------------------- classements
def cle_live(p):                      # pendant la course : score puis avancement
    return (-p["score"], -p["pct"])

def cle_final(p):                     # arrivé > éliminé > pas fini ; puis points ; puis temps / avancement
    classe = {1: 2, 0: 1}.get(p["statut"], 0)
    return (-classe, -p["score"], p["temps"] if p["statut"] == 1 else -p["pct"])

def classement(cle):
    ps = [S["joueurs"][i] for i in S["participants"] if i in S["joueurs"]]
    return sorted(ps, key=cle)

# ---------------------------------------------------------------- messages reçus
def traiter(texte, addr):
    champs = texte.strip().split("|")
    if len(champs) < 2: return
    typ, pid = champs[0], champs[1][:11]
    n = lambda i: int(champs[i]) if i < len(champs) and champs[i].lstrip("-").isdigit() else 0
    with lock:
        if S["bannis"].get(pid, 0) > maintenant(): return
        p = S["joueurs"].get(pid)
        if typ == "HELLO":
            if not p:
                if len(S["joueurs"]) >= MAX_JOUEURS: return
                p = S["joueurs"][pid] = dict(id=pid, score=0, pct=0, statut=-1, temps=0, etat="salon")
            p.update(perso=max(0, min(7, n(2))), addr=addr, hello=maintenant(), vu=maintenant())
            if S["phase"] in ("salon", "termine") or pid not in S["participants"]:
                p["etat"] = "salon"
            nb = sum(1 for q in S["joueurs"].values() if dans_salon(q))
            envoyer(pid, f"LB|{pid}|{nb}|{S['niveau']}")
        elif not p:
            return
        elif typ == "PRG" and pid in S["participants"] and S["phase"] == "course":
            p.update(score=n(2), pct=n(3), vu=maintenant(), addr=addr)
        elif typ == "FIN" and pid in S["participants"]:
            p.update(score=n(2), statut=n(3), temps=n(4), pct=n(5), vu=maintenant(), addr=addr, etat="fini")
        elif typ == "BYE":
            if pid in S["participants"] and S["phase"] in ("compte", "course"):
                p["etat"] = "parti"
            else:
                del S["joueurs"][pid]

# ---------------------------------------------------------------- actions de l'admin
def demarrer():
    with lock:
        if S["phase"] in ("compte", "course"): return "déjà en cours"
        ids = [i for i, p in S["joueurs"].items() if dans_salon(p)]
        if not ids: return "aucun joueur dans le salon"
        S["participants"] = ids
        S["seed"] = random.randint(1, 0x7FFFFFFF)
        for i in ids:
            S["joueurs"][i].update(score=0, pct=0, statut=-1, temps=0, etat="jeu")
        S["resultat"] = []
        S["manche"] += 1
        S["phase"], S["t_phase"] = "compte", maintenant()
        return "ok"

def arreter():
    with lock:
        if S["phase"] in ("compte", "course"):
            finaliser()
            return "ok"
        return "rien à arrêter"

def exclure(pid):
    with lock:
        if pid not in S["joueurs"]: return "inconnu"
        for _ in range(4): envoyer(pid, f"KICK|{pid}")
        S["bannis"][pid] = maintenant() + 15
        if pid in S["participants"] and S["phase"] in ("compte", "course"):
            S["joueurs"][pid]["etat"] = "parti"
        else:
            del S["joueurs"][pid]
        return "ok"

def effacer_top():
    with lock:
        S["meilleurs"] = []
        sauver_top()
        return "ok"

def choisir_niveau(i):
    with lock:
        if S["phase"] in ("salon", "termine"): S["niveau"] = max(0, min(len(NIVEAUX) - 1, i))

def finaliser():
    S["resultat"] = [p["id"] for p in classement(cle_final)]
    S["phase"], S["t_phase"] = "termine", maintenant()
    noter_meilleurs()

# ---------------------------------------------------------------- boucle temps réel
def ligne_res(pid):
    p = S["joueurs"][pid]
    return f"{nom_affiche(pid)}|{p['score']}|{p['statut']}|{p['temps']}"

def boucle():
    dernier_go = dernier_rk = dernier_res = 0.0
    while True:
        time.sleep(0.03)
        with lock:
            t = maintenant()
            if S["phase"] == "compte":
                reste = COMPTE_MS - ms(t - S["t_phase"])
                if t - dernier_go > 0.15:
                    dernier_go = t
                    for pid in S["participants"]:
                        envoyer(pid, f"GO|{pid}|{S['niveau']}|{S['seed']}|{max(0, reste)}")
                if reste <= -200:
                    S["phase"], S["t_phase"] = "course", t
            elif S["phase"] == "course":
                for pid in S["participants"]:
                    p = S["joueurs"].get(pid)
                    if p and p["statut"] == -1 and p["etat"] != "parti" and ms(t - p["vu"]) > SILENCE_MS:
                        p["etat"] = "parti"
                actifs = [S["joueurs"][i] for i in S["participants"] if i in S["joueurs"]]
                fini = all(p["statut"] != -1 or p["etat"] == "parti" for p in actifs)
                if fini or t - S["t_phase"] > DUREE_MAX_S:
                    finaliser()
                elif t - dernier_rk > 0.2:
                    dernier_rk = t
                    cl = classement(cle_live)
                    corps = "|".join(f"{nom_affiche(p['id'])}|{p['score']}|{p['pct']}" for p in cl)
                    for rang, p in enumerate(cl, 1):
                        envoyer(p["id"], f"RK|{p['id']}|{rang}|{len(cl)}|{corps}")
            elif S["phase"] == "termine" and t - S["t_phase"] < 4 and t - dernier_res > 0.3:
                dernier_res = t
                cl = [S["joueurs"][i] for i in S["resultat"] if i in S["joueurs"]]
                corps = "|".join(ligne_res(p["id"]) for p in cl)
                for rang, p in enumerate(cl, 1):
                    envoyer(p["id"], f"RES|{p['id']}|{rang}|{len(cl)}|{corps}")
            # nettoyage : tablettes disparues du salon
            for pid in list(S["joueurs"]):
                p = S["joueurs"][pid]
                if pid not in S["participants"] or S["phase"] in ("salon", "termine"):
                    if ms(t - p["vu"]) > 20000 and ms(t - p["hello"]) > 20000:
                        del S["joueurs"][pid]

def ecoute():
    while True:
        try:
            data, addr = sock.recvfrom(512)
        except OSError:
            continue
        try: traiter(data.decode("utf-8", "ignore"), addr)
        except Exception as e: print("message ignoré :", e)

# ---------------------------------------------------------------- état pour la page web
def etat_json():
    with lock:
        t = maintenant()
        joueurs = []
        for pid, p in S["joueurs"].items():
            joueurs.append(dict(id=pid, nom=nom_affiche(pid), perso=PERSOS[p["perso"] % 8], score=p["score"],
                                pct=p["pct"], statut=p["statut"], temps=p["temps"], etat=p["etat"],
                                salon=dans_salon(p), vu=ms(t - p["vu"]),
                                participant=pid in S["participants"]))
        live = [p["id"] for p in classement(cle_live)] if S["phase"] in ("compte", "course") else []
        return dict(phase=S["phase"], niveau=S["niveau"], niveaux=NIVEAUX, manche=S["manche"],
                    reste=max(0, COMPTE_MS - ms(t - S["t_phase"])) if S["phase"] == "compte" else 0,
                    duree=int(t - S["t_phase"]) if S["phase"] == "course" else 0,
                    joueurs=joueurs, live=live, resultat=S["resultat"], top=S["meilleurs"][:3])

PAGE = r"""<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GAME TAB admin</title><style>
:root{--bg:#f5f3ff;--fg:#1c1b2e;--card:#fff;--line:#d9d4f2;--acc:#7c3aed;--ok:#15935a;--bad:#d1344c;--mut:#6b6885}
@media (prefers-color-scheme:dark){:root{--bg:#15132a;--fg:#ece9ff;--card:#201d3d;--line:#37325f;--acc:#a78bfa;--ok:#4ade80;--bad:#fb7185;--mut:#a29fc7}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
main{max-width:980px;margin:0 auto;padding:16px max(16px,env(safe-area-inset-left))}
h1{font-size:22px;margin:4px 0 2px}.sub{color:var(--mut);margin:0 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:14px}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:center}
button{font:inherit;border:0;border-radius:9px;padding:10px 16px;cursor:pointer;background:var(--acc);color:#fff;font-weight:600}
button.sec{background:transparent;color:var(--fg);border:1px solid var(--line)}button.bad{background:var(--bad)}button:disabled{opacity:.4;cursor:default}
button.niv.on{outline:3px solid var(--acc);outline-offset:2px}
.badge{display:inline-block;padding:3px 10px;border-radius:99px;font-weight:700;background:var(--line)}
table{width:100%;border-collapse:collapse}th,td{text-align:left;padding:7px 6px;border-bottom:1px solid var(--line)}th{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.05em}
.bar{height:10px;background:var(--line);border-radius:6px;min-width:90px;overflow:hidden}.bar i{display:block;height:100%;background:var(--acc)}
.mut{color:var(--mut)}.big{font-size:44px;font-weight:800}.scroll{overflow-x:auto}
</style></head><body><main>
<h1>GAME TAB : admin</h1><p class="sub">Le serveur gère la partie en temps réel. Les tablettes se connectent d'elles-mêmes.</p>
<div class="card"><div class="row"><span class="badge" id="phase">…</span><span id="info" class="mut"></span><span class="big" id="cpt"></span></div></div>
<div class="card"><div class="row"><b>Niveau :</b><span id="niv" class="row"></span></div>
<div class="row" style="margin-top:12px"><button id="go">DÉMARRER</button><button id="stop" class="bad">ARRÊTER LA PARTIE</button><span id="msg" class="mut"></span></div></div>
<div class="card"><div class="row" style="justify-content:space-between"><b>Top 3 des meilleurs joueurs</b><button class="sec" id="rz">Remettre à zéro</button></div>
<div id="podium" class="row" style="margin-top:10px;align-items:stretch"></div></div>
<div class="card scroll"><b>Joueurs</b><table><thead><tr><th>Rang</th><th>Joueur</th><th>État</th><th>Score</th><th>Avancement</th><th></th></tr></thead><tbody id="joueurs"></tbody></table><p class="mut" id="vide"></p></div>
<div class="card scroll" id="resbox" hidden><b>Résultat final</b><table><thead><tr><th>Rang</th><th>Joueur</th><th>Score</th><th>Résultat</th></tr></thead><tbody id="res"></tbody></table></div>
</main><script>
const MDP=new URLSearchParams(location.search).get('mdp')||'';
const $=id=>document.getElementById(id);
async function post(u,b){const r=await fetch(u+'?mdp='+encodeURIComponent(MDP),{method:'POST',body:JSON.stringify(b||{})});$('msg').textContent=(await r.text());}
$('rz').onclick=()=>{if(confirm('Effacer le Top 3 ?'))post('/api/effacer_top')};$('go').onclick=()=>post('/api/start');$('stop').onclick=()=>post('/api/stop');
const NOMS={salon:'Salon (en attente)',compte:'Compte à rebours',course:'Course en cours',termine:'Partie terminée'};
function etatJ(j,s){if(j.etat==='parti')return'parti';if(s.phase==='course'||s.phase==='compte'){if(!j.participant)return'spectateur';return j.statut===1?'arrivé ✔':j.statut===0?'éliminé':'en course';}return j.salon?'prêt':'absent';}
async function maj(){try{const s=await (await fetch('/api/state')).json();
$('phase').textContent=NOMS[s.phase];
$('info').textContent=s.phase==='course'?s.duree+' s':s.phase==='salon'||s.phase==='termine'?s.joueurs.filter(j=>j.salon).length+' joueur(s) prêt(s)':'';
$('cpt').textContent=s.phase==='compte'?Math.ceil(s.reste/1000):'';
$('niv').innerHTML='';s.niveaux.forEach((n,i)=>{const b=document.createElement('button');b.className='sec niv'+(i===s.niveau?' on':'');b.textContent=n;b.disabled=!(s.phase==='salon'||s.phase==='termine');b.onclick=()=>post('/api/niveau',{niveau:i});$('niv').appendChild(b);});
$('go').disabled=!(s.phase==='salon'||s.phase==='termine');$('stop').disabled=!(s.phase==='compte'||s.phase==='course');
const rang={};(s.phase==='termine'?s.resultat:s.live).forEach((id,i)=>rang[id]=i+1);
const js=[...s.joueurs].sort((a,b)=>(rang[a.id]||99)-(rang[b.id]||99));
$('joueurs').innerHTML=js.map(j=>`<tr><td>${rang[j.id]||''}</td><td><b>${j.nom}</b> <span class="mut">${j.id}</span></td><td>${etatJ(j,s)}</td><td>${j.participant?j.score:''}</td><td><div class="bar"><i style="width:${j.participant?j.pct:0}%"></i></div></td><td><button class="sec" onclick="if(confirm('Exclure ${j.nom} ?'))post('/api/kick',{id:'${j.id}'})">Exclure</button></td></tr>`).join('');
$('vide').textContent=js.length?'':'Aucune tablette connectée. Sur la tablette : MULTIJOUEUR.';
const MED=['🥇','🥈','🥉'];
$('podium').innerHTML=s.top.length?s.top.map((e,i)=>`<div style="flex:1;min-width:180px;border:1px solid var(--line);border-radius:10px;padding:10px"><div style="font-size:28px">${MED[i]}</div><b>${e.nom}</b><div class="big" style="font-size:30px">${e.score}</div><div class="mut">${e.arrive?'arrivé en '+(e.temps/1000).toFixed(1)+' s':'non arrivé'} · ${e.niveau}</div></div>`).join(''):'<span class="mut">Pas encore de résultat : le Top 3 se remplit à la fin de chaque partie.</span>';
const f=s.phase==='termine';$('resbox').hidden=!f;
if(f)$('res').innerHTML=s.resultat.map((id,i)=>{const j=s.joueurs.find(x=>x.id===id);if(!j)return'';const t=j.statut===1?`arrivé en ${(j.temps/1000).toFixed(1)} s`:j.statut===0?'éliminé':'pas fini';return`<tr><td>${i+1}</td><td><b>${j.nom}</b></td><td>${j.score}</td><td>${t}</td></tr>`}).join('');
}catch(e){$('phase').textContent='serveur injoignable';}}
setInterval(maj,500);maj();
</script></body></html>"""

class Web(BaseHTTPRequestHandler):
    mdp = ""
    def log_message(self, *a): pass
    def rep(self, code, corps, typ="text/plain; charset=utf-8"):
        b = corps.encode("utf-8") if isinstance(corps, str) else corps
        self.send_response(code); self.send_header("Content-Type", typ)
        self.send_header("Content-Length", str(len(b))); self.send_header("Cache-Control", "no-store")
        self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        chemin = urllib.parse.urlparse(self.path).path
        if chemin == "/api/state": self.rep(200, json.dumps(etat_json()), "application/json")
        elif chemin == "/": self.rep(200, PAGE, "text/html; charset=utf-8")
        else: self.rep(404, "introuvable")
    def do_POST(self):
        q = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        if self.mdp and q.get("mdp", [""])[0] != self.mdp: return self.rep(403, "mot de passe incorrect")
        n = int(self.headers.get("Content-Length", 0) or 0)
        try: corps = json.loads(self.rfile.read(n) or b"{}")
        except ValueError: corps = {}
        chemin = urllib.parse.urlparse(self.path).path
        if chemin == "/api/start": self.rep(200, demarrer())
        elif chemin == "/api/stop": self.rep(200, arreter())
        elif chemin == "/api/kick": self.rep(200, exclure(str(corps.get("id", ""))))
        elif chemin == "/api/effacer_top": self.rep(200, effacer_top())
        elif chemin == "/api/niveau": choisir_niveau(int(corps.get("niveau", 0))); self.rep(200, "ok")
        else: self.rep(404, "introuvable")

def ip_locale():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.connect(("10.255.255.255", 1))
        ip = s.getsockname()[0]; s.close(); return ip
    except OSError: return "127.0.0.1"

def main():
    global sock
    ap = argparse.ArgumentParser()
    ap.add_argument("--udp", type=int, default=4210); ap.add_argument("--http", type=int, default=8080)
    ap.add_argument("--mdp", default=""); a = ap.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", a.udp))
    Web.mdp = a.mdp
    charger_top()
    threading.Thread(target=ecoute, daemon=True).start()
    threading.Thread(target=boucle, daemon=True).start()
    ip = ip_locale()
    print(f"\nGAME TAB serveur : tablettes sur UDP {a.udp}")
    print(f"Page admin : http://{ip}:{a.http}   (ou http://localhost:{a.http} sur ce PC)")
    if a.mdp: print(f"Mot de passe actif : ajouter ?mdp={a.mdp} à l'adresse")
    print("Si les tablettes ne te voient pas : autoriser Python dans le pare-feu (UDP entrant).\nCtrl+C pour arrêter.\n")
    ThreadingHTTPServer(("", a.http), Web).serve_forever()

if __name__ == "__main__":
    main()
