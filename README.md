````mermaid
flowchart TD

%% =====================
%% DEMARRAGE & CONFIG
%% =====================

START([Debut])

START --> ASK0[Afficher : Appuyer bouton rouge pour config ?]
ASK0 --> WAIT0[Attendre saisie bouton rouge]
WAIT0 --> C0{BOUTONROUGE == 1 ?}

%% Branche CONFIGURATION : Redirection vers le mode opérationnel après exécution
C0 -- Oui --> R0[Réinitialiser BOUTONROUGE = 0]
R0 --> M0[MODE = 0]
M0 --> VER0[Appel VERIFICATION]
VER0 --> CONF0[Appel CONFIGURATION]
CONF0 --> M1_AUTO[MODE = 1]

%% Branche Directe vers Standard
C0 -- Non --> M1[MODE = 1]

%% =====================
%% BOUCLE PRINCIPALE (OPERATIOUNELLE)
%% =====================

M1 --> WHILE{{Tant que MODE > 0}}
M1_AUTO --> WHILE

WHILE -- Faux --> END([Fin])
WHILE -- Vrai --> ASK1[Afficher : Appuyer bouton rouge ?]

ASK1 --> WAIT1[Attendre saisie bouton rouge]
WAIT1 --> C1{BOUTONROUGE == 1<br/>ou MODE == 3 ?}

%% ---- Branche MAINTENANCE 1
C1 -- Oui --> M3[MODE = 3]
M3 --> MAINT1[Appel MAINTENANCE]
MAINT1 --> JOIN1(( ))
JOIN1 --> VER1[Appel VERIFICATION]
VER1 --> WHILE

%% ---- Branche STANDARD / ECONOMIE
C1 -- Non --> ASK2[Afficher : Appuyer bouton vert ?]
ASK2 --> WAIT2[Attendre saisie bouton vert]
WAIT2 --> C2{BOUTONVERT == 1<br/>ou MODE == 2 ?}

%% ---- Mode STANDARD
C2 -- Faux --> STD[Appel STANDARD]
STD --> JOIN2(( ))
JOIN2 --> VER2[Appel VERIFICATION]
VER2 --> WHILE

%% ---- Mode ECONOMIE ou MAINTENANCE 4
C2 -- Vrai --> ASK3[Afficher : Appuyer bouton rouge ?]
ASK3 --> WAIT3[Attendre saisie bouton rouge]
WAIT3 --> C3{BOUTONROUGE == 1<br/>ou MODE == 4 ?}

%% Sous-branche Économie
C3 -- Faux --> M2[MODE = 2]
M2 --> ECO[Appel ECONOMIE]
ECO --> JOIN3(( ))

%% Sous-branche Maintenance via Mode Éco
C3 -- Vrai --> M4[MODE = 4]
M4 --> MAINT2[Appel MAINTENANCE]
MAINT2 --> JOIN3

%% Retour commun vers la vérification cyclique
JOIN3 --> VER3[Appel VERIFICATION]
VER3 --> WHILE


```mermaid
