# 1. Découverte du réseau et évitement des collisions

  

## Le Défi Physique

En LoRa, une émission radio dure typiquement entre 100 et 800 millisecondes (Time-on-Air). Si deux modules émettent simultanément sur la même fréquence, les ondes se superposent et détruisent les paquets. Pire encore, en forêt, le phénomène du nœud caché est permanent : le Nœud A et le Nœud C ne s'entendent pas à cause de la végétation ou de la distance, mais tous deux peuvent atteindre le Nœud B. S'ils émettent ensemble vers B, B ne recevra que du bruit.

  

## La Stratégie d'Anti-Collision (CSMA/CA adapté au LoRa)

Pour découvrir le réseau sans déclencher une tempête de diffusion (Broadcast Storm), chaque équipement applique trois barrières successives :

  

**L'Écoute Préalable (CAD - Channel Activity Detection) :**

* **Pourquoi :** Contrairement au Wi-Fi qui mesure la simple énergie (RSSI), le LoRa peut émettre sous le plancher de bruit thermique. Mesurer la puissance brute ne suffit pas à savoir si le canal est occupé.

* **Comment :** La radio écoute pendant la durée exacte de quelques symboles LoRa pour détecter la corrélation de phase d'un préambule. Si un préambule est détecté, le canal est déclaré occupé.

  

**Le Retard Aléatoire Initial (Randomized Jitter) :**

* **Pourquoi :** Si la station de base crie "Où êtes-vous ?", dix nœuds à portée vont vouloir répondre en même temps. Sans étalement temporel, ils entreront tous en collision.

* **Comment :** Avant de relayer un paquet de découverte ou d'y répondre, chaque nœud tire un délai d'attente aléatoire au hasard dans une fenêtre temporelle (ex. entre 50 ms et 400 ms).

  

**Le Backoff Exponentiel :**

* **Pourquoi :** Si le nœud fait son CAD et trouve le canal occupé, il ne doit pas retester immédiatement.

* **Comment :** Le nœud double la fenêtre temporelle de tirage aléatoire à chaque échec d'accès au canal (ex. tentative 1 : 0–100 ms, tentative 2 : 0–200 ms, tentative 3 : 0–400 ms) jusqu'à un seuil maximal, après quoi la tentative est avortée.

  

## Le Processus de Découverte (RREQ / RREP inversé)

* **L'Émission de la Requête (RREQ) :** La station émet un paquet de découverte en mode Diffusion (Broadcast). Ce paquet contient un identifiant unique de découverte, un compteur de sauts initialisé à zéro, et un champ métrique de coût initialisé à zéro.

* **La Propagation Contrôlée :** Un nœud qui reçoit ce paquet ne le rediffuse pas immédiatement :

  * Il vérifie dans sa mémoire s'il a déjà vu ce numéro de découverte.

  * Si oui, il l'ignore silencieusement.

  * Si non, il enregistre qui lui a transmis ce paquet (son "parent" potentiel vers la station), incrémente le compteur de sauts, ajoute la dégradation du signal mesurée, applique son Jitter aléatoire, puis rediffuse.

* **La Réponse de Découverte (RREP) :** Lorsque la cible est atteinte (ou que chaque nœud répond périodiquement à un appel général), la réponse n'est jamais envoyée en Broadcast, mais en Unicast strict en remontant le chemin inverse point par point jusqu'à la station.

  

# 2. Structure de la Cache en RAM et Gestion des Voisins

Pour qu'un microcontrôleur avec des ressources limitées (comme un STM32 doté de 64 Ko de RAM) tienne la charge sans fuite de mémoire ni saturation, la gestion mémoire doit être entièrement statique et circulaire.

  

## A. La Cache Anti-Doublon (Deduplication Ring Buffer)

* **Pourquoi :** Dans un réseau maillé, un même message est répété par plusieurs nœuds et reviendra inévitablement aux oreilles de ceux qui l'ont déjà transmis. Sans filtre strict, le paquet tournera en boucle infinie jusqu'à saturer la bande passante.

* **Structure Logique :**

  * Un tableau à taille fixe en RAM (ex. 32 ou 64 emplacements maximum).

  * Chaque case contient : l'ID du nœud source initial, le numéro de séquence du message (Message_ID), et un horodatage interne (le temps système en secondes).

* **Fonctionnement :**

  * À chaque réception, le microcontrôleur parcourt cette liste. Si le couple [Source_ID + Message_ID] est présent, le paquet est jeté immédiatement à la poubelle sans aucun calcul supplémentaire.

  * Si le paquet est nouveau, il est écrit à l'indice d'un pointeur qui avance de façon circulaire (retour à zéro arrivé au bout).

  * Les vieux enregistrements sont automatiquement écrasés par les plus récents. Un paquet vieux de plus de quelques minutes expire logiquement.

  

## B. La Table des Voisins Directs (1-Hop Neighbor Table)

Chaque nœud, qu'il soit relais ou terminal, ne connaît physiquement que ses voisins directs à 1 saut.

* **Structure d'un enregistrement voisin :**

  * **ID du Voisin :** Identifiant matériel unique.

  * **Qualité de Lien Reçue (LQI / SNR moyen) :** Une moyenne glissante du signal reçu lors des derniers échanges.

  * **Indicateur d'Activité :** Horodatage de la dernière trame entendue depuis ce voisin.

  * **Compteur d'Échecs d'Acquittement :** Nombre de fois consécutives où ce voisin n'a pas répondu à une transmission directe.

  * **Statut du Voisin :** Actif, Dégradé (signal faible ou échecs récents), ou Mort (à purger).

  

# 3. Trame Réseau, CRC8, ACKs, TTL et Timers

  

## A. Découpage Conceptuel d'une Trame Réseau

Chaque octet transmis par radio coûte du temps et de l'énergie. La trame doit être compacte :

* **Préambule & Synchro :** Gérés au niveau radio (PHY).

* **En-tête Contrôle (1 octet) :** Type de paquet (Données, Découverte, Réponse, Erreur, Acquittement) et drapeaux de priorité.

* **Identifiants de Routage :**

  * **Source Finale (1 octet) :** L'initiateur du message.

  * **Destination Finale (1 octet) :** La cible visée.

  * **Émetteur Immédiat (1 octet) :** Le nœud qui transmet la trame à cet instant.

  * **Récepteur Immédiat (1 octet) :** Le nœud qui doit réceptionner cette trame précise au saut actuel.

* **Gestion Réseau :**

  * **Numéro de Séquence (1 octet) :** Identifiant unique pour la déduplication.

  * **TTL (Time-To-Live, 4 bits) :** Nombre maximal de sauts autorisés.

  * **Longueur des Données Utiles (4 bits).**

* **Chemin de Routage (Optionnel, en Source Routing) :** Liste ordonnée des IDs des nœuds intermédiaires à traverser.

* **Charge Utile (Payload) :** Données applicatives (ex. Coordonnées GPS : Latitude, Longitude, Altitude, Statut batterie).

* **CRC8 applicatif (1 octet) :** Octet de validation d'intégrité de bout en bout.

  

## B. Pourquoi un CRC8 applicatif si la radio a déjà un CRC16 ?

* **Pourquoi :** Le CRC matériel de la puce radio (LoRa) protège le paquet uniquement sur un seul saut. Dès qu'un nœud relais reçoit le paquet, il doit modifier certains champs de l'en-tête (décrémenter le TTL, modifier l'émetteur immédiat). Si une corruption mémoire survient en RAM sur un nœud intermédiaire, le CRC radio du saut suivant sera recalculé faux sur une donnée corrompue.

* **Comment :** Le CRC8 applicatif est calculé à l'origine uniquement sur les données immuables : [Source Finale + Destination Finale + Numéro de Séquence + Payload]. Aucun relais ne modifie ces champs. La cible finale recalcule ce CRC8 pour garantir que la donnée applicative est strictement identique à ce que la source a forgé, même après 4 sauts intermédiaires.

  

## C. Gestion des Acquittements (ACK) et Retransmissions

Il existe deux niveaux d'acquittement :

* **L'ACK Saut-par-Saut (Hop-by-Hop) :** Indispensable. Quand le Nœud A passe le paquet au Nœud B, B renvoie immédiatement un mini-paquet d'acquittement court vers A. Tant que A n'a pas reçu cet ACK, il garde le paquet dans sa file d'attente d'émission.

* **L'ACK de Bout-en-Bout (End-to-End) :** La station attend la confirmation finale de la machine cible. Si la machine cible répond directement avec les données GPS demandées, la donnée GPS elle-même fait office d'acquittement implicite.

  

## D. Architecture des Timers

Le système repose sur quatre temporisations distinctes :

* **Timer d'ACK (Court : ex. 800 ms à 1.5 s) :** Durée maximale d'attente de l'ACK du saut suivant. S'il expire, le nœud retransmet (jusqu'à 3 tentatives).

* **Timer de Jitter (Très court : 10 ms à 300 ms) :** Délai d'attente aléatoire avant émission pour désynchroniser les nœuds.

* **Timer de Validité de Route (Long : ex. 5 à 15 minutes) :** Si aucune communication n'a eu lieu sur un chemin pendant ce laps de temps, la route est déclarée obsolète car les machines ont pu bouger.

* **Timer d'Inactivité Voisin :** Si un voisin n'a émis aucun signe de vie après 2 fenêtres de Heartbeat théoriques, son statut bascule à "Inaccessible".

  

# 4. Traitement des Pannes de Route et Traçabilité (Last-Hop Traceback)

Que se passe-t-il lorsqu'un véhicule s'est déplacé derrière une colline et qu'un message en cours de route ne peut plus l'atteindre ?

  

```text

[Station] ---> [Nœud 1] ---> [Nœud 2] --X ÉCHEC X--> [Nœud 3 (Cible mobile)]

                                 |

                                 +=== (Génération du paquet RERR vers la Station) ===>

```

  

## Le Processus de Rupture

1. Le Nœud 2 reçoit le paquet en provenance du Nœud 1. Sa table (ou l'en-tête du paquet) lui indique de transmettre au Nœud 3.

2. Le Nœud 2 transmet la trame vers le Nœud 3.

3. Le Nœud 2 attend l'ACK du Nœud 3. Rien n'arrive.

4. Le Nœud 2 retente l'émission une deuxième, puis une troisième fois.

5. Après 3 échecs, le lien entre le Nœud 2 et le Nœud 3 est déclaré rompu.

  

## La Traçabilité et le Paquet RERR (Route Error)

Le Nœud 2 ne jette pas le message dans le silence. Il génère un paquet spécial d'alerte : le RERR :

* **Contenu du RERR :**

  * **Source_Erreur :** Nœud 2 (l'endroit où le paquet s'est arrêté).

  * **Destination_Visée :** Nœud 3 (la cible qui ne répond plus).

  * **ID_Message_Échoué :** Le numéro du paquet initial pour que la station sache quelle commande a échoué.

  * **Dernier_Saut_Valide :** Nœud 2.

* **Acheminement du RERR :** Le Nœud 2 renvoie ce paquet d'erreur en sens inverse vers la Station de base.

  

## Action sur la Station de Base :

La station apprend instantanément deux choses capitales :

* La commande vers le Nœud 3 a échoué.

* Le Nœud 2 est parfaitement fonctionnel et en ligne, mais le segment reliant 2 à 3 est mort.

La station met à jour sa matrice topologique en coupant la ligne entre 2 et 3.

Si la station possède un chemin alternatif en réserve dans sa matrice (par exemple via le Nœud 4), elle réémet immédiatement la commande via ce nouveau chemin. Sinon, elle déclenche une nouvelle découverte ciblée.

  

# 5. Sélection de Route par Métrique ETX (Expected Transmission Count)

  

## Pourquoi le simple comptage des sauts (Hop Count) est une erreur en forêt

Si l'on choisit le chemin qui a le moins de sauts, le réseau va systématiquement privilégier un lien unique très long qui traverse une forêt dense à la limite de la rupture (ex. un lien à -125 dBm avec 80% de perte de paquets nécessitant 5 réémissions), plutôt qu'un chemin faisant un détour par deux liens courts à vue parfaite (100% de succès du premier coup).

  

## Le Concept Mathématique de l'ETX

L'ETX représente le nombre théorique d'émissions nécessaires pour faire parvenir un paquet à destination et recevoir son acquittement avec succès.

* Un lien parfait a un ETX de 1.0 (1 émission = 1 réception réussie).

* Un lien médiocre où un paquet sur deux est perdu a un ETX de 2.0.

* La qualité totale d'une route est la somme des ETX de chaque saut.

  

## Adaptation Pratique de l'ETX pour LoRa sur Microcontrôleur

Calculer l'ETX pur exige d'envoyer des dizaines de paquets sondes pour mesurer le taux de perte, ce qui saturerait la fréquence radio. En LoRa, on estime l'ETX d'un saut de façon passive à chaque paquet reçu grâce au SNR (Signal-to-Noise Ratio) :

* La radio fournit la marge SNR de chaque paquet reçu.

* Si le SNR est supérieur à +5 dB (signal très net au-dessus du bruit) : le coût du lien est quasi-parfait (Poids = 1).

* Si le SNR est négatif (entre -5 dB et -15 dB, signal enfoui sous le bruit propre au LoRa) : la probabilité de rater le prochain paquet est élevée. On attribue un coût pénalisant au lien (ex. Poids = 3 ou 4).

* **Accumulation :** Chaque relais qui transmet le message de découverte ajoute le poids de son propre lien au champ métrique du paquet.

* **Arbitrage :** La Station choisit toujours la route dont le poids accumulé est le plus bas, garantissant ainsi la route la plus stable et la plus économe en batterie, même si elle comporte un ou deux sauts supplémentaires.

  

# 6. Gestion des Matrices Topologiques et Déconnexions Multiples

  

```text

   [Station de Base]

      /            (Lien 1)    (Lien 2)

    /              [Nœud 1]       [Nœud 2]

    |              |

 [Nœud 3]       [Nœud 4]

        \       /

      (Zone Isolé)

```

  

## A. Côté Station de Base : La Matrice d'Adjacence Globale

La station maintient en mémoire la carte complète du réseau sous forme de matrice ou de liste d'arêtes pondérées :

* Pour chaque paire de nœuds (ex. A et B), la station stocke l'état du lien : Connecté avec un coût X, Dégradé, ou Inaccessible.

* **Calcul des routes :** Dès qu'une communication doit partir, la station exécute un algorithme de plus court chemin (type Dijkstra) sur sa matrice en mémoire pour trouver la chaîne optimale de nœuds à traverser.

  

## B. Gestion des Nœuds Isolés et Réseaux Morcelés

Que faire lorsqu'un groupe de machines s'éloigne ensemble hors de portée de la station ?

* **Détection du Silence :** Les nœuds émettent un battement de cœur (Heartbeat) périodique (ex. toutes les 10 minutes). Si la station ne reçoit plus rien du groupe [Nœuds 3, 4 et 5] après deux périodes, elle marque ces nœuds comme Non Joignables dans sa matrice.

* **Gestion de l'Orphelinat côté Nœuds :**

  * Chaque nœud écoute son environnement. S'il n'entend plus aucun paquet en provenance d'un nœud ayant un lien direct ou indirect vers la station, il bascule en état "Orphelin".

  * Un nœud orphelin cesse de relayer les données applicatives vers le vide pour préserver sa batterie.

  * Il réduit drastiquement sa fréquence d'émission radio et émet de temps en temps un signal d'appel de détresse court (Beacon de recherche de réseau).

* **Le Raccrochage Dynamique :**

  * Dès qu'une machine du groupe se rapproche à nouveau d'un nœud connecté au reste du monde, le lien radio se rétablit.

  * Le premier nœud rattaché transmet immédiatement l'information à la station : "Je vois à nouveau les nœuds 3, 4 et 5 derrière moi avec telle qualité de signal".

  * La station réintègre instantanément ces branches dans sa matrice et les communications reprennent.

  

# 7. Adressage : Unicast vs Broadcast

L'usage des modes de transmission radio répond à des règles strictes :

  

| Type | Méthode d'adressage | Rôle dans l'architecture | Risque / Contrainte |

| :--- | :--- | :--- | :--- |

| **Broadcast** (Diffusion générale) | Adresse destination = 0xFF (Tous les appareils acceptent le paquet). | Réservé exclusivement à la découverte (RREQ), aux alertes d'urgence ou aux balises de synchronisation périodiques de la station. | Très dangereux pour le réseau. Doit avoir un TTL strict et passer par le filtre anti-doublon pour ne pas saturer la bande. Aucun ACK n'est possible en Broadcast. |

| **Unicast** (Adressage ciblé) | Adresse destination = ID spécifique de la machine (ex. 0x0A). | Toutes les autres communications : commandes, transfert de coordonnées GPS, paquets d'erreur RERR, et ACKs. | Nécessite de connaître le saut suivant. Permet les réémissions fiables et les acquittements matériels. |

  

## Le Filtrage Matériel/Logiciel

Pour économiser l'énergie de calcul du processeur :

1. La radio capture le paquet.

2. Le microcontrôleur lit immédiatement l'octet Destinataire_Immédiat.

3. Si cette valeur n'est ni son propre ID, ni l'adresse de Broadcast générale (0xFF), le microcontrôleur coupe le traitement sur-le-champ et retourne en veille. Il ne lit même pas le reste de la mémoire tampon radio.

  

# 8. Les Machines à États Finis (FSM)

L'architecture logicielle doit s'articuler autour de deux machines d'états asynchrones non-bloquantes.

  

## A. Machine à États du Nœud (Machine / Relais)

  

```text

                       +-------------------+

                       |    VEILLE_CAD     |<------------------+

                       +-------------------+                   |

                                 | (Activité radio détectée)   |

                                 v                             |

                       +-------------------+                   |

                       |    RECEPTION      |                   |

                       +-------------------+                   |

                                 |                             |

                 +---------------+---------------+             |

                 | (Paquet corrompu ou doublon)  |             |

                 v                               v             |

             [ REJET ]                  [ TRAITEMENT ]         |

                 |                               |             |

                 |          +--------------------+             |

                 |          | (Si je suis Cible) | (Si Relais) |

                 |          v                    v             |

                 |     [ REPONSE ]        [ ATTENTE_JITTER ]   |

                 |          |                    |             |

                 |          |                    v             |

                 |          |             [ EMISSION_SAUT ]    |

                 |          |                    |             |

                 |          +----------+---------+             |

                 |                     |                       |

                 |                     v                       |

                 |             +---------------+               |

                 |             |  ATTENTE_ACK  |               |

                 |             +---------------+               |

                 |              | (Succès)  | (Échec x3)       |

                 +------------->|           v                  |

                                |     [ GENERATION_RERR ]      |

                                |           |                  |

                                +-----------+------------------+

```

  

* **État VEILLE_CAD :** Le microcontrôleur dort en veille profonde. La radio effectue des reniflements périodiques (CAD). Si une activité est détectée, la radio réveille le microcontrôleur par interruption matérielle.

* **État RÉCEPTION :** Récupération de la trame via le bus de communication. Vérification de l'adresse immédiate et validation du CRC. Si invalide, retour direct en veille.

* **État ANALYSE_ROUTAGE :**

  * Consultation de la cache anti-doublon. Si le paquet a déjà été vu : suppression et retour en veille.

  * Le nœud est-il la Destination Finale ?

    * **Oui :** Traitement applicatif de la commande (ex. allumage GPS, lecture coordonnées) et transition vers l'état de préparation de réponse.

    * **Non :** Le nœud doit agir en Relais. Décrémentation du TTL. Si le TTL est nul, le paquet est détruit. Sinon, passage à l'état de transmission.

* **État ATTENTE_JITTER :** Calcul d'un délai aléatoire non-bloquant pour éviter la collision avec d'autres relais, puis vérification que le canal est libre (LBT).

* **État ÉMISSION_SAUT :** Envoi du paquet vers le destinataire du saut suivant.

* **État ATTENTE_ACK :** Écoute exclusive pendant la fenêtre du timer d'acquittement.

  * **ACK reçu :** Mission accomplie. Mise à jour de la table des voisins (statut positif) et retour en veille.

  * **Timeout (pas d'ACK) :** Retransmission (jusqu'à 3 fois max).

* **État GESTION_ÉCHEC :** Si les 3 tentatives échouent, le voisin est marqué défaillant. Génération d'une trame d'erreur (RERR) orientée vers la station de base pour signaler l'emplacement exact de la rupture.

  

## B. Machine à États de la Station de Base (Maître du Réseau)

* **État ÉCOUTE_RÉSEAU (IDLE) :** La station écoute en permanence le canal (elle ne dort pas car elle dispose d'une alimentation stable). Elle réceptionne les Heartbeats périodiques et met à jour sa matrice de voisins en continu.

* **État PRÉPARATION_COMMANDE :** Une demande utilisateur ou un ordre applicatif arrive (ex. "Localiser la machine 7").

  * Recherche du nœud 7 dans la matrice topologique.

  * Calcul du chemin optimal selon l'ETX accumulé.

  * Si une route valide existe : Injection de la liste des sauts dans l'en-tête du paquet (Source Routing) et transition vers l'émission.

  * Si aucune route n'est connue : Transition vers l'état DÉCOUVERTE.

* **État DÉCOUVERTE :** Émission d'une requête de recherche (RREQ en Broadcast). Démarrage d'un timer de résolution de topologie. La station collecte les réponses (RREP) et met à jour sa matrice avant de lancer la commande.

* **État ÉMISSION_COMMANDE :** Transmission de la trame de données en Unicast vers le premier nœud de la chaîne calculée.

* **État ATTENTE_RÉPONSE_FINALE :** Armement d'un timer global adapté au nombre de sauts prévus (un aller-retour sur 3 sauts prend plusieurs secondes en LoRa).

  * **Réception des données GPS :** Validation du CRC8 applicatif de bout en bout. La transaction est un succès.

  * **Réception d'un paquet RERR :** Analyse de l'incident. La station identifie quel relais a calé, marque le lien comme rompu dans son graphe, recalcule une route alternative sans ce lien et réémet la commande.

  * **Timeout Global total :** La commande est déclarée perdue. La station marque la cible comme temporairement déconnectée.

  

# 9. Synthèse Architecturale : Le "Pourquoi" et le "Comment" Global

Pour réussir ce système en milieu forestier hostile, retenez ces quatre piliers de conception :

  

* **Pourquoi le Source Routing centralisé est supérieur au maillage ad-hoc pur :**

  En laissant la station de base porter la complexité mathématique du calcul de route (Dijkstra) et le stockage de la grande matrice réseau, vous déchargez vos nœuds mobiles en forêt. Vos machines n'ont pas besoin de calculer de graphes complexes ; elles se contentent de lire l'en-tête du paquet qui leur dit bêtement : "Passe-moi au nœud suivant".

* **Pourquoi l'asynchronisme strict est obligatoire :**

  Aucune routine ne doit bloquer le processeur (aucun délai passif). Tout doit être cadencé par des interruptions matérielles (terminaison de transmission radio, détection de paquet reçu, expiration de timers système). Si un microcontrôleur attend bêtement la fin d'un délai, il rate les trames des machines voisines.

* **Pourquoi l'ETX par SNR sauve la portée en forêt :**

  En ignorant les routes fragiles sur un saut pour forcer des trajets à deux sauts avec une marge de signal excellente, vous évitez les retransmissions en chaîne. Paradoxalement, faire faire un détour à un paquet par deux nœuds fiables est beaucoup plus rapide et consomme bien moins d'énergie que de saturer la radio avec 3 échecs consécutifs sur une ligne directe trop étouffée par les arbres.

* **Pourquoi la cache circulaire est vitale :**

  Elle constitue le pare-feu absolu contre l'écroulement de votre réseau. C'est l'élément basique mais critique qui permet à des transmissions d'urgence en diffusion (Broadcast) de traverser la forêt de machine en machine sans jamais générer de boucle infinie.

  
  

# 10. Contrôle Dynamique de Puissance (TPC - Transmit Power Control)

  

Actuellement, l'architecture prévoit d'émettre à la puissance maximale de 33 dBm (2 Watts).

* **Pourquoi c'est dangereux :**

  1. Deux machines proches (ex. à 20 mètres l'une de l'autre) qui se parlent à 2 Watts saturent complètement leurs étages de réception RF respectifs (*desensitization*), ce qui paradoxalement augmente le taux d'erreur binaire.

  2. Cela vide la batterie à une vitesse fulgurante (jusqu'à 1.5 A de tirage).

* **Comment l'implémenter :**

  Utilisez le champ SNR rapporté par l'ACK du récepteur.

  * Si le récepteur répond avec un SNR excellent (> +8 dB), la machine émettrice réduit automatiquement sa puissance pour le paquet suivant (ex. passage de 33 dBm à 22 dBm, puis à 14 dBm).

  * Dès que le SNR baisse sous +2 dB ou qu'un ACK est manqué, le nœud repasse immédiatement à 33 dBm.

  

# 11. Agilité en Fréquence / Double Canal (Frequency Hopping)

  

En milieu forestier, les phénomènes de réflexions multiples créent des creux d'interférence destructrice très étroits (Multi-path fading). Une liaison radio entre deux arbres peut être totalement coupée sur 433.125 MHz, mais devenir limpide si l'on décale la fréquence de seulement 500 kHz.

  

* **Pourquoi c'est vital :** Éviter qu'un nœud ne soit déclaré mort simplement parce qu'il se trouve dans un nœud d'interférence physique stationnaire.

* **Comment l'implémenter :**

  * Définir un canal primaire (ex. 433.125 MHz) et un canal secondaire de secours (ex. 433.725 MHz).

  * Si un nœud échoue à contacter son relais après 2 tentatives sur le canal primaire, il bascule sa radio sur le canal secondaire pour la troisième tentative.

  

# 12. Mémoire Non-Volatile Résiliente (Flash Interne / EEPROM avec Wear-Leveling)

  

Un module émettant à 2 Watts provoque un appel de courant soudain qui peut, sur une batterie froide ou fatiguée, créer une micro-chute de tension (Brownout Reset) du microcontrôleur.

* **Pourquoi c'est vital :** Si le STM32 redémarre spontanément, il ne doit pas perdre son numéro de séquence de paquet (`Message_ID`), son horloge relative, ni son rôle configuré. S'il réinitialise son `Message_ID` à zéro, ses paquets risquent d'être considérés comme de vieux doublons par la cache des autres nœuds et d'être rejetés.

* **Comment l'implémenter :**

  Enregistrer en Flash (via une routine de Wear-Leveling pour ne pas user le silicium) des blocs de sauvegarde contenant le dernier bloc de 100 numéros de séquence réservés et l'état de la machine. Au réveil, le nœud repart avec un index incrémenté sans perturber le réseau maillé.

  

# 13. Chiffrement Matériel et Authentification (AES-128-CCM)

  

La bande des 433 MHz est une bande publique libre. N'importe quel promeneur, technicien ou braconnier équipé d'une clé SDR (Software Defined Radio) ou d'un module Ebyte du commerce peut écouter, injecter de fausses trames GPS ou saturer le réseau en forgeant des paquets d'erreur RERR malveillants.

  

* **Pourquoi c'est facile sur STM32 :** Le microcontrôleur recommandé (**STM32L433**) intègre un moteur de calcul cryptographique matériel **AES-128**.

* **Comment l'implémenter :**

  Utilisez le mode **AES-CCM** (Counter with CBC-MAC). Il garantit deux choses en une seule opération :

  1.  **Confidentialité :** Vos coordonnées GPS sont illisibles pour un tiers.

  2.  **Authenticité (Tag MAC) :** Le paquet contient une signature cryptographique de 4 octets. Si un intrus tente de modifier l'en-tête de routage ou d'injecter une fausse commande, la signature est invalide et le paquet est détruit avant même d'atteindre la couche applicative. L'accélération matérielle du STM32 exécute ce calcul en quelques microsecondes sans impact mesurable sur la batterie.