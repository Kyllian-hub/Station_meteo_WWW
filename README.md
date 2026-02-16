## MAIN (à compléter)
```mermaid
flowchart TD

START([Debut])

START --> ASK0[Appuyer bouton rouge pour config]
ASK0 --> WAIT0[Attendre bouton rouge]
WAIT0 --> C0{BOUTONROUGE == 1}

C0 -- Oui --> R0[Reset bouton rouge]
R0 --> M0[MODE = 0]
M0 --> CONF0[CONFIGURATION]
CONF0 --> M1_AUTO[MODE = 1]

C0 -- Non --> M1[MODE = 1]

M1 --> LOOP
M1_AUTO --> LOOP

LOOP{MODE > 0}

LOOP -- Non --> END([Fin])
LOOP -- Oui --> ASK1[Appuyer bouton rouge]

ASK1 --> WAIT1[Attendre bouton rouge]
WAIT1 --> C1{BOUTONROUGE == 1 OR MODE == 3}

%% =====================
%% MAINTENANCE MODE 3
%% =====================

C1 -- Oui --> M3[MODE = 3]
M3 --> MAINT1[MAINTENANCE]

MAINT1 --> RET3{BOUTONROUGE == 1}

RET3 -- Oui --> BACK1[MODE = 1]
RET3 -- Non --> VER1[VERIFICATION]

BACK1 --> VER1
VER1 --> LOOP

%% =====================
%% STANDARD / ECONOMIE
%% =====================

C1 -- Non --> ASK2[Appuyer bouton vert]
ASK2 --> WAIT2[Attendre bouton vert]
WAIT2 --> C2{BOUTONVERT == 1 OR MODE == 2}

%% =====================
%% STANDARD
%% =====================

C2 -- Non --> STD[STANDARD]
STD --> VER2[VERIFICATION]
VER2 --> LOOP

%% =====================
%% ECONOMIE / MAINTENANCE 4
%% =====================

C2 -- Oui --> ASK3[Appuyer bouton rouge]
ASK3 --> WAIT3[Attendre bouton rouge]
WAIT3 --> C3{BOUTONROUGE == 1 OR MODE == 4}

%% ---- ECONOMIE
C3 -- Non --> M2[MODE = 2]
M2 --> ECO[ECONOMIE]

%% AJOUT : retour Standard si bouton vert en mode 2
ECO --> RET_ECO{BOUTONVERT == 1}

RET_ECO -- Oui --> BACK_STD[MODE = 1]
RET_ECO -- Non --> VER3[VERIFICATION]

BACK_STD --> VER3
VER3 --> LOOP

%% =====================
%% MAINTENANCE MODE 4
%% =====================

C3 -- Oui --> M4[MODE = 4]
M4 --> MAINT2[MAINTENANCE]

MAINT2 --> RET4{BOUTONROUGE == 1}

RET4 -- Oui --> BACK2[MODE = 2]
RET4 -- Non --> VER4[VERIFICATION]

BACK2 --> VER4
VER4 --> LOOP

```

## Standart
```mermaid
flowchart TD

START([STANDARD])

START --> A1[LOGINTERVAL 600]
A1 --> A2[LEDVERTE = 1]
A2 --> A3[Entier InstantT]
A3 --> A4[TIMEOUT = 30]
A4 --> A5[InstantT = RTC + LOGINTERVAL]

%% =====================
%% Attente RTC
%% =====================

A5 --> W1{RTC != InstantT}
W1 -- Oui --> W1
W1 -- Non --> B1[InstantT = RTC]

%% =====================
%% GPS
%% =====================

B1 --> C1{RTC < InstantT + TIMEOUT and GPSINSTANT = 0 }
C1 -- Faux --> SAVE1[Mesures enregistrees]
C1 -- Vrai --> NA1[NA]
SAVE1 --> D1[InstantT = RTC]
NA1 --> D1

%% =====================
%% LUMIN
%% =====================

D1 --> C2{RTC < InstantT + TIMEOUT and LUMININSTANT = 0}
C2 -- Faux --> SAVE2[Mesures enregistrees]
C2 -- Vrai --> NA2[NA]
SAVE2 --> D2[InstantT = RTC]
NA2 --> D2

%% =====================
%% PRESSURE
%% =====================

D2 --> C3{RTC < InstantT + TIMEOUT and PRESSUREINSTANT = 0 }
C3 -- Faux --> SAVE3[Mesures enregistrees]
C3 -- Vrai --> NA3[NA]
SAVE3 --> D3[InstantT = RTC]
NA3 --> D3

%% =====================
%% HYGR
%% =====================

D3 --> C4{RTC < InstantT + TIMEOUT and HYGRINSTANT = 0}
C4 -- Faux --> SAVE4[Mesures enregistrees]
C4 -- Vrai --> NA4[NA]
SAVE4 --> D4[InstantT = RTC]
NA4 --> D4

%% =====================
%% TEMPAIR
%% =====================

D4 --> C5{RTC < InstantT + TIMEOUT and TEMPAIRINSTANT = 0 }
C5 -- Faux --> SAVE5[Mesures enregistrees]
C5 -- Vrai --> NA5[NA]

SAVE5 --> FS
NA5 --> FS

%% =====================
%% FILE SIZE
%% =====================

FS{FILEMAXSIZE < 2000}

FS -- Faux --> COPY[Creer copie fichier revision adaptee\nRecommencer en revision 0\nEnregistrer donnees et date]

FS -- Vrai --> WRITE[Enregistrer donnees et date]

COPY --> END([Fin])
WRITE --> END
```

## Maintenance
```mermaid
flowchart TD

A([MAINTENANCE]) --> B[LEDOrange  = 1]
B --> C[LOGINTERVAL 600]
C --> D[Entrer InstantT]
D --> E[TIMEOUT 30]
E --> F[InstantT = RTC + LOGINTERVAL]
F --> G[Booleen CarteSDretiree]
G --> H[Afficher Voulez-vous retirer la carte SD ?]
H --> I[Attendre saisie CarteSDretiree]

I --> J{CarteSDretiree VRAI}

J -- Oui --> K[Autoriser changement carte SD]
K --> END([Fin])

J -- Non --> L{RTC != InstantT}
L -- Oui --> L
L -- Non --> M[InstantT = RTC]

M --> N1{RTC < InstantT + TIMEOUT and GPSINSTANT = 0}
N1 -- Non --> O1[AfficherDonnees]
N1 -- Oui --> P1[NA]
O1 --> Q1[InstantT = RTC]
P1 --> Q1

Q1 --> N2{RTC < InstantT + TIMEOUT and LUMININSTANT = 0 }
N2 -- Non --> O2[AfficherDonnees]
N2 -- Oui --> P2[NA]
O2 --> Q2[InstantT = RTC]
P2 --> Q2

Q2 --> N3{RTC < InstantT + TIMEOUT and PRESSUREINSTANT = 0 }
N3 -- Non --> O3[AfficherDonnees]
N3 -- Oui --> P3[NA]
O3 --> Q3[InstantT = RTC]
P3 --> Q3

Q3 --> N4{RTC < InstantT + TIMEOUT and HYGRINSTANT = 0 }
N4 -- Non --> O4[AfficherDonnees]
N4 -- Oui --> P4[NA]
O4 --> Q4[InstantT = RTC]
P4 --> Q4

Q4 --> N5{RTC inferieur InstantT + TIMEOUT and TEMPAIRINSTANT = 0}
N5 -- Non --> O5[AfficherDonnees]
N5 -- Oui --> P5[NA]

O5 --> END
P5 --> END
```

## Economique
```mermaid

flowchart TD

START([ECONOMIQUE])

START --> A1[LOGINTERVAL 600]
A1 --> A2[LEDVERTE = 1]
A2 --> A3[Entier InstantT]
A3 --> A4[TIMEOUT = 30]
A4 --> A5[InstantT = RTC + LOGINTERVAL*2]

%% =====================
%% Attente RTC
%% =====================

A5 --> W1{RTC != InstantT}
W1 -- Oui --> W1
W1 -- Non --> B1[InstantT = RTC]

%% =====================
%% GPS
%% =====================

B1 --> C1{RTC < InstantT + TIMEOUT and GPSINSTANT = 0 }
C1 -- Faux --> SAVE1[Mesures enregistrees]
C1 -- Vrai --> NA1[NA]
SAVE1 --> D1[InstantT = RTC]
NA1 --> D1

%% =====================
%% LUMIN
%% =====================

D1 --> C2{RTC < InstantT + TIMEOUT and LUMININSTANT = 0}
C2 -- Faux --> SAVE2[Mesures enregistrees]
C2 -- Vrai --> NA2[NA]
SAVE2 --> D2[InstantT = RTC]
NA2 --> D2

%% =====================
%% PRESSURE
%% =====================

D2 --> C3{RTC < InstantT + TIMEOUT and PRESSUREINSTANT = 0 }
C3 -- Faux --> SAVE3[Mesures enregistrees]
C3 -- Vrai --> NA3[NA]
SAVE3 --> D3[InstantT = RTC]
NA3 --> D3

%% =====================
%% HYGR
%% =====================

D3 --> C4{RTC < InstantT + TIMEOUT and HYGRINSTANT = 0}
C4 -- Faux --> SAVE4[Mesures enregistrees]
C4 -- Vrai --> NA4[NA]
SAVE4 --> D4[InstantT = RTC]
NA4 --> D4

%% =====================
%% TEMPAIR
%% =====================

D4 --> C5{RTC < InstantT + TIMEOUT and TEMPAIRINSTANT = 0 }
C5 -- Faux --> SAVE5[Mesures enregistrees]
C5 -- Vrai --> NA5[NA]

SAVE5 --> FS
NA5 --> FS

%% =====================
%% FILE SIZE
%% =====================

FS{FILEMAXSIZE < 2000}

FS -- Faux --> COPY[Creer copie fichier revision adaptee\nRecommencer en revision 0\nEnregistrer donnees et date]

FS -- Vrai --> WRITE[Enregistrer donnees et date]

COPY --> END([Fin])
WRITE --> END
```

## Verification (à compléter)
```mermaid
flowchart TD

START([VERIFICATION])

%% =====================
%% Initialisation durée LED
%% =====================

START --> D0[dureeLED = 1]

%% =====================
%% Initialisation flags
%% =====================

D0 --> A1[Booleen AccesHorlogeRTC]
A1 --> A2[Booleen DonneesCoherentes]
A2 --> A3[Booleen StockagePlein]
A3 --> A4[Booleen AccesCarteSD]
A4 --> A5[Booleen Erreur]

A5 --> SETERR[Erreur = 1]

%% =====================
%% Debut verification
%% =====================

SETERR --> C0{Erreur == 1}

C0 -- Faux --> END([Fin])

C0 -- Vrai --> C1{AccesHorlogeRTC == 1}

%% =====================
%% GPS
%% =====================

C1 -- Faux --> LED1[Led = dureeLED* rouge and dureeLED* bleu]
C1 -- Vrai --> C2{GPSINSTANT != NA}

C2 -- Faux --> LED2[Led = dureeLED* rouge and dureeLED* jaune]
C2 -- Vrai --> C3{LUMININSTANT != NA}

%% =====================
%% LUMIN
%% =====================

C3 -- Faux --> LED3[Led = dureeLED* rouge and dureeLED* vert]
C3 -- Vrai --> C4{TEMPAIRINSTANT != NA}

%% =====================
%% TEMPAIR
%% =====================

C4 -- Faux --> LED4[Led = dureeLED* rouge and dureeLED* vert]
C4 -- Vrai --> C5{HYGRINSTANT != NA}

%% =====================
%% HYGR
%% =====================

C5 -- Faux --> LED5[Led = dureeLED* rouge and dureeLED* vert]
C5 -- Vrai --> C6{PRESSUREINSTANT != NA}

%% =====================
%% PRESSURE
%% =====================

C6 -- Faux --> LED6[Led = dureeLED* rouge and dureeLED* vert]
C6 -- Vrai --> INCOH[IncoherencesDATA]

%% =====================
%% Cohérence données
%% =====================

INCOH --> C7{DonneesCoherentes == 1}

C7 -- Faux --> LED7[Led = dureeLED* rouge and 2*dureeLED * vert]
C7 -- Vrai --> SD[CarteSD]

%% =====================
%% Carte SD
%% =====================

SD --> C8{StockagePlein == 0}

C8 -- Faux --> LED8[Led = dureeLED* rouge and dureeLED* blanc]
C8 -- Vrai --> C9{AccesCarteSD == 1}

C9 -- Faux --> LED9[Led = dureeLED* rouge and 2*dureeLED * blanc]
C9 -- Vrai --> OK[Erreur = 0]

%% =====================
%% Fin
%% =====================

LED1 --> END
LED2 --> END
LED3 --> END
LED4 --> END
LED5 --> END
LED6 --> END
LED7 --> END
LED8 --> END
LED9 --> END
OK --> END

```

## Mode configuration à refaire, nous devons revoir l'ensemble de l'architecture.





