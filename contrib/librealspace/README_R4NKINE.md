# R4nkine dans libRealSpace : r4bridge et r4objviewer

Ce paquet branche **R4nkine** sur **libRealSpace** sans rien relire.
- libRealSpace lit les données, comme aujourd'hui :
  - `AssetManager` ouvre les TRE ;
  - `RSPalette` lit la palette ;
  - `RSEntity` lit un objet 3D.
- **r4bridge** convertit un `RSEntity` en modèle R4nkine (`r4::assets::Model`).
- **R4nkine** le charge sur le GPU et le dessine (OpenGL 3.3 core, ou 4.1 sur macOS).

```
TOBIGTRE.TRE ──AssetManager──▶ TreEntry ──RSEntity::InitFromRAM──▶ RSEntity
                                                               │
                                     r4bridge::toModel(entity, palette)
                                                               ▼
                       r4::assets::Model (images, matériaux, meshes, nœuds)
                                                               │
                                          renderer.upload(model)
                                                               ▼
                       r4::scene::Node ──renderer.render(root, camera)──▶ l'écran
```

## Contenu

| Fichier | Rôle |
|---|---|
| `src/r4bridge/R4Bridge.h/.cpp` | L'adaptateur `RSEntity` vers `r4::assets::Model`, et la table des teintes de la palette |
| `src/r4bridge/R4World.h/.cpp` | Le terrain d'une zone (`RSArea`) et ses overlays (les pistes) en modèles R4nkine, et les matrices des objets de la carte (`MapObject`) et de la mission (`MISN_PART`), calculées comme `SCRenderer` |
| `src/r4bridge/CMakeLists.txt` | La bibliothèque statique `r4bridge` |
| `src/executables/r4objviewer/main.cpp` | Le viewer : liste des objets ou des missions, caméra, GUI, post-process |
| `src/executables/r4objviewer/WorldScene.h/.cpp` | Une mission en scène R4nkine : `RSMission`, `RSWorld` et `RSArea` chargés par libRealSpace, le terrain, les objets de la carte et de la mission |
| `src/executables/r4objviewer/TerrainDiag.h/.cpp` | Le diagnostic d'un terrain (`-diag`) : ses types et ses textures en images |
| `src/executables/r4objviewer/CMakeLists.txt` | L'exécutable, lié comme le debugger |
| `cmake/R4nkine.cmake` | Ajoute R4nkine (dossier local ou FetchContent), r4bridge et r4objviewer |

## Intégration

1. **Copier** les dossiers `src/` et `cmake/` du zip à la racine de libRealSpace. Seuls des fichiers nouveaux sont ajoutés, aucun existant n'est remplacé.

2. **CMakeLists.txt principal** : ajouter une ligne, par exemple juste avant `add_subdirectory(tests)` :

   ```cmake
   include(cmake/R4nkine.cmake)
   ```

   Pour qu'`install` emporte aussi le viewer, ajouter `${R4OBJVIEWER_TARGET_NAME}` à la ligne `install(TARGETS ...)`. C'est facultatif.

3. **vcpkg.json** : R4nkine a besoin de trois bibliothèques, uniquement des en-têtes :

   ```json
   "glm",
   "stb",
   "cgltf",
   ```

4. **Configurer.** Il y a deux façons d'obtenir R4nkine.
   - Depuis un clone local (recommandé pour apprendre et modifier les deux à la fois) :
     ```
     cmake --preset <ton preset> -DR4NKINE_DIR=/chemin/vers/r4nkine
     ```
   - Sans `R4NKINE_DIR` : CMake le télécharge depuis GitHub (FetchContent), branche `R4NKINE_TAG`. Par défaut c'est `claude/magical-bell-y9meqd`.

   Avec `-DREALSPACE_R4=OFF`, rien de tout cela n'est construit.

5. **Lancer** :
   ```
   r4objviewer -data /chemin/vers/les/TRE [-object F-16DES] [-tre TOBIGTRE.TRE] [-textures]
   r4objviewer -data /chemin/vers/les/TRE -mission MISN-A1 [-focus TOWB24] [-distance 2500] [-graph]
   ```
   - **Les jeux** : sans `-tre`, le viewer ouvre chaque jeu présent **seul**, car les deux ont des fichiers différents sous les mêmes noms (301 objets, la palette, les polices) :
     - `BIGTRE.TRE` pour Strike Commander. Ses textures du terrain (TXMPACK, ACCPACK) sont dans `TEXTURES.TRE`. Sans ce fichier, celles de `TOBIGTRE.TRE` les remplacent : il est ouvert en second et ne fournit que les fichiers absents de BIGTRE. Ses objets et ses missions ne sont pas listés.
     - `TOBIGTRE.TRE` pour Tactical Operations.
     - Sans aucun des deux, `OBJECTS.TRE` et `MISC.TRE`.

     Le menu **Game** en haut à gauche passe d'un jeu à l'autre et refait tout (palette, objets, missions). La fenêtre reste ouverte. `-game sc|to` choisit le jeu au départ ; avec `-mission`, c'est le jeu qui a cette mission qui est ouvert. Sans les textures du terrain, une mission est refusée avec un message au lieu de planter RSArea.
   - En haut à gauche, **Mode** : *Objects* (un objet à la fois) ou *Missions*.
   - Mode **Missions** :
     - la liste ne montre que les missions et les mondes dont le terrain (`<tera>.PAK`) est dans le dossier `-data` ;
     - un monde seul, sans mission, montre son terrain et les objets de la carte ;
     - à droite, la section **World** :
       - couches *Terrain*, *Runways*, *Map Objects*, *Mission Parts* et *Forests* ;
       - **Forest Density** (`-forests 0..3`) : aucune, clairsemée, normale ou dense ;
       - niveau de détail du terrain ;
       - chiffres : triangles, textures, objets, objets absents du TRE, lumière du monde, temps de chargement ;
     - le graphe : sous « parts of the mission », un nœud par zone de la mission (entrer dans une zone déclenche ses scènes), puis les scènes de cette zone ;
     - la section **Scenes** : les parts groupées par la scène qui les fait apparaître ; chaque scène s'affiche ou se cache, et « No scene » (les parts amenées par un programme de la mission) est cachée au départ ;
     - les sections **Parts** et **Map Objects** : un clic met la caméra sur l'objet ;
     - `-focus NOM` fait de même au démarrage, `-at X,Y` met la caméra sur un point de la carte (axes de R4nkine), et `-distance D` règle le recul de la caméra.
   - En haut à droite, **Scene Graph** (ou `-graph`), en haut du panneau de droite : le graphe de la scène affichée (objet ou mission) s'affiche sous ce bouton. On y voit le type de chaque nœud, un voyant pour le cacher ou le montrer, et un clic sur un nœud y amène la caméra.
   - Panneau de gauche en mode Objects, **Objects** : la liste des objets des TRE. Un clic choisit un objet, la molette fait défiler la liste, et il y a des boutons Précédent et Suivant.
   - Panneau de droite :
     - **Render** : fil de fer, textures, facteur de LOD, lumière, ciel, angles de la caméra ;
     - **Post Process** : MSAA, FXAA, scanlines, palette… ;
     - **Performance** : FPS, courbes des temps (image, cull, draw), draw calls, triangles, changements d'état, items et nœuds ;
     - **Object** : sommets, faces, textures, les LOD avec leurs distances (celui affiché est marqué), le temps de chargement et de conversion, les meshes R4nkine ;
     - **Textures** : chaque image de l'objet, avec sa taille et son nombre de frames. `-textures` ouvre cette section au départ.
   - Souris : bouton gauche pour tourner, molette pour le zoom, bouton droit pour déplacer.
   - Clavier : flèches haut et bas pour l'objet précédent ou suivant, Échap pour quitter.
   - Dans **Render** :
     - **Flat Shading** (ou `-flat`) : les objets prennent la normale de leur face, comme `object_gouraud = false` dans le jeu, et le terrain est recalculé par `RSArea` avec `terrain_flat` ;
     - **Gouraud by Vertex** (ou `-gouraud`) : l'éclairage des objets est calculé aux sommets puis interpolé, comme le jeu (le dos d'une face double prend la normale de la face). Sinon, il est calculé à chaque pixel.
     - **Texture Upscale** (`-upscale 0..3`) : les textures du jeu agrandies une seule fois, à leur chargement, en 2xSaI, xBR 2x ou xBR 4x (`r4/assets/upscale.h`). Cela concerne les objets, le terrain et les pistes ; dans l'atlas des pistes, chaque image est agrandie avant d'y être rangée. L'alpha compte comme une couleur, donc les trous (index 255) gardent des bords nets. xBR 4x ajoute environ 1 s au chargement de TURKEY.
     - **Mipmaps** (`-mipmaps`) : avec leurs mipmaps. Le jeu n'en a pas ; avec, les bordures des cellules de l'atlas des pistes passent de 2 à 8 texels. Les textures du terrain n'en ont pas : ce sont des index de palette, filtrés dans le shader après leur ombrage.
     - **Anisotropic Filter** (`-aniso N`, de 2 à 16) : le filtrage anisotrope, quand le GPU le permet.

       Ces trois réglages refont l'objet ou le monde affiché (`r4bridge::textureQuality()`, `finishTexture`). Par défaut, tout est comme le jeu.

     Les deux se règlent au départ d'après le `config.ini` du jeu s'il est là.
   - **Sun** (les deux modes) :
     - **Sun Heading / Sun Elevation** : la direction du soleil (`-sun C E`), qui éclaire les objets ;
     - **World's Light** : revient à la lumière du monde, `RSWorld::light_direction` ;
     - **Lit Terrain** (`-lit`) : le terrain éclairé par le soleil plutôt que par la lumière des données. Chaque triangle prend le ton le plus clair de la rampe de son type de terrain (`RSArea::terrainTone`), assombri le long de cette rampe par l'intensité, comme les objets (PaletteRamp : ambiant + lumière × N·L). Les normales sont calculées à partir du relief, sur toute la carte ;
     - **Relief of the Light** (`-relief 0..4`) : multiplie le relief pour les normales seulement (×1 à ×16, ×4 par défaut). Le relief des cartes est doux, avec un sommet tous les 1 000, alors que la lumière des données l'exagère beaucoup.
     - **Cast Shadows** (`-shadows`) : les ombres portées du soleil. Une *shadow map* (2048², vue orthographique depuis le soleil) couvre la vue jusqu'à **Shadow Distance** (Auto : 3 fois la distance de la caméra), avec un PCF 3×3 et un fondu au bord. Les objets jusqu'à 8 km vers le soleil y projettent leur ombre, par exemple un avion haut au-dessus du sol. Les objets éclairés perdent la lumière directe et gardent l'ambiante. Ce qui n'est pas éclairé, comme le terrain de la lumière des données ou les pistes, est assombri de **Shadow on Unlit**.
   - **Navigation**, dans une mission :
     - les touches à leur place sur le clavier (WASD en QWERTY, ZQSD en AZERTY) et les flèches pour avancer et se décaler, Q/E (A/E en AZERTY) pour tourner, R/F pour se rapprocher ou s'éloigner, Maj pour aller plus vite ;
     - **double clic** sur le terrain : la caméra y va (rayon lancé contre `RSArea::getY`) ;
     - **Follow the Ground** : la cible reste sur le sol ;
     - les boutons **Whole Map**, **Top View** et un par zone de la mission.
   - `-frames N -shot image.bmp` fait une capture après N images, utile pour les tests.
   - `-mission NOM -diag DOSSIER` écrit le diagnostic du terrain dans DOSSIER, qui doit exister. Tout est au niveau LVL0, 360 × 360 sommets, avec le nord en haut :
     - `terrain.ppm` : les couleurs de RSArea ;
     - `types.ppm` : le type de chaque sommet, dans le ton le plus clair de sa rampe ;
     - `textures.ppm` : les triangles texturés, chacun dans la couleur moyenne de son image ;
     - `tex_<id>.ppm` : chaque image utilisée, avec les texels clairs en magenta ;
     - `types.txt` : le nombre de sommets par type ;
     - `textures.txt` : par image, le nombre de triangles, les types sur lesquels elle est posée et ses premières cellules.

     Avec `-frames 1`, le viewer se ferme ensuite.

## Ce qu'il faut regarder pour apprendre R4nkine

Le viewer suit les étapes d'un programme R4nkine, numérotées dans `main.cpp`.

1. **Le device** : `r4::gpu::createGLDevice(SDL_GL_GetProcAddress)`.
   - C'est la couche la plus basse : buffers, textures, shaders, pipelines.
   - Les fonctions GL sont chargées à l'exécution, donc il n'y a ni GLEW ni GLAD.
2. **Le renderer** : `r4::render::Renderer renderer(*device)`.
   - Il dessine un graphe de scène : cull, tri opaque/transparent, puis draw.
   - `renderer.environment()` règle la lumière et le fond.
   - `renderer.options()` règle les textures, le facteur de LOD et le reste.
3. **Le modèle neutre** : `r4::assets::Model`.
   - Les chargeurs de R4nkine (OBJ, glTF, OpenFlight…) produisent ce modèle, et r4bridge aussi.
   - Il ne contient que des données : `images`, `materials`, `meshes` et `nodes`, avec `nodes[0]` pour la racine.
   - `renderer.upload(model)` en fait un sous-graphe prêt à dessiner.
4. **La caméra** : `r4::render::Orbit` tourne autour d'un point et `orbit.apply(camera, fov, aspect, near, far)` remplit la `Camera`.
5. **La GUI** : `r4::ui::Context`, de l'immédiat comme ImGui mais avec le look Performer.
   - Le code : `beginFrame`, `beginPanel`, les widgets (`button`, `toggle`, `slider`, `section`…), `endPanel`, `endFrame`, puis `render()` après la scène.
   - `r4::ui::postProcessControls()` fournit d'un coup tout le panneau des effets.
6. **Le post-process** : `r4::render::PostProcess`.
   - `post.begin()` donne une cible, le renderer y dessine avec `setTarget`, puis `post.end()` applique les effets : MSAA, FXAA, scanlines, palette…
   - Quand aucun effet n'est actif, `begin()` renvoie une cible nulle et la scène est dessinée directement.

## Comment r4bridge convertit un RSEntity

Le pont suit `SCRenderer::drawModel` passe par passe (couleur, texture, transparence) :

| RealSpace (libRealSpace) | R4nkine |
|---|---|
| Axes : Y en haut (`entity->vertices`) | Z en haut : (x, y, z) devient (x, −z, y), une rotation de 90° |
| Faces dans le sens horaire (`glFrontFace(GL_CW)`) | Ordre inversé (a, c, b) : R4nkine attend le sens anti-horaire |
| Normales `(a − b) × (c − b)`, lissées comme `objectNormals` (un quad compte pour deux triangles) | Même calcul, après le changement d'axes |
| `lods` (LVL0, LVL1…) et `attrs` (T/Q/L) | Un nœud `NodeKind::LOD`, avec un enfant par niveau ; `lodRanges` = [0, dist1, dist2, …, ∞] |
| Passe couleur : triangle `color`, quad `color − 1` | Matériau `Shading::PALETTE_RAMP` ; l'index est dans le rouge de la couleur de sommet |
| Éclairage (ambiant 3/8, lumière 5/8, `shadeAlongRamp`) | Image 256 × 32 (`rampImage`) : ligne k = intensité k/31 |
| Face double : `flags[2] == 1` (triangle), `attrs->props2 == 1` (quad) | `doubleSided`, sinon face arrière éliminée comme dans le jeu |
| Passe texture (LOD0, toutes les `uvs` et `qmapuvs`) | Images RGBA à travers la palette ; index 255 transparent (`Mask`, seuil 0,1 comme `glAlphaFunc`) |
| Propriété 6 ou 9 : la texture est la face (`GL_LESS`) | Matériau `rampTextured` |
| Autres faces texturées : sur la face colorée (`GL_EQUAL`) | Même matériau avec `decal = true` (polygon offset) |
| Passe transparente : propriété 2, couleur `color − 1`, `glBlendFunc(GL_ONE, GL_ONE)` | `AlphaMode::ADDITIVE`, palette sans rampe (couleur × intensité) |

La lumière se règle avec `r4bridge::Lighting` ou avec les curseurs Ambient et Light du viewer.

Pour afficher un objet ailleurs que dans le viewer, par exemple dans un futur `SCRenderer` sur R4nkine :

```cpp
RSEntity *entity = ...;                       // déjà chargé par libRealSpace
auto model = r4bridge::toModel(entity, *palette.GetColorPalette(), palette.ranges);
r4::scene::NodePtr node = renderer.upload(*model);
someGroup->addChild(node);                    // et le renderer s'en occupe
```

## Les missions : un seul changement de repère

Une mission reste entièrement dans le repère de libRealSpace (Y en haut) :
- le terrain : `terrainModel` reprend les sommets de `RSArea` tels quels ;
- les objets : `toModel(..., Axes::REAL_SPACE)` ;
- leurs matrices : `mapObjectMatrix` et `missionPartMatrix` reprennent les `glTranslate`, `glRotate` et `glMultMatrix` de `SCRenderer`.

Une seule matrice, `r4bridge::realSpaceToR4()`, posée à la racine de la scène, passe dans le repère de R4nkine (Z en haut). C'est la frontière unique entre les deux conventions : physique, IA et mission n'ont rien à convertir.

Pour la lumière du monde, `setObjectLight` reçoit (c0, c1, c2) et en fait (c0, c2, −c1) en Y up. Ramenée dans le repère de R4nkine, elle redevient (c0, c1, c2), c'est-à-dire les valeurs du fichier telles quelles.

Le terrain (`WorldScene` et `R4World`) :
- **Un nœud par bloc** : un `Group` « terrain » contient les 324 blocs (« block x,y »). Chacun est un nœud `LOD` sur les trois niveaux de `RSArea` (LVL0 400 sommets, LVL1 100, LVL2 25), avec ses jointures vers ses voisins de droite et du bas, comme `renderBlock`. Le renderer élimine donc les blocs hors du champ et choisit le niveau de chacun selon sa distance : fin jusqu'à 40 000, grossier au-delà de 100 000, réglable avec « LOD Scale ». Le jeu, lui, dessine tous ses blocs au même niveau (`world_detail`). Le menu « Terrain Detail » propose les deux : « By Distance », ou un niveau forcé partout.
- **Les jupes** prolongent la carte jusqu'au bord du monde, deux fois la taille de la carte de chaque côté (environ 1 440 km de côté au lieu de 360), comme `RSArea::BuildSkirts`. Chaque sommet du bord est relié au bord du monde à la hauteur 0, avec les couleurs du bord lissées le long de l'anneau et leur moyenne à l'extérieur. Elles ne sont pas lues dans `RSArea::GetSkirts()`, qui ne les fait qu'en LVL0 : sur les bords droit et bas, LVL1 et LVL2 s'arrêtent avant LVL0 (aux 9/10 et 4/5 du dernier bloc), ce qui laisse un trou. Le pont refait donc le même anneau à chaque niveau, et chaque bloc du bord porte sa part dans son nœud LOD. Le centre de ces nœuds reste celui du bloc. « Whole Map » cadre la carte sans les jupes, et le plan lointain de la caméra passe à 1 500 km.
- **Les couleurs** sont celles des rampes de `RSArea` (`ComputeTriangleColors`). Leurs ombres dépendent de `terrain_shadow_lift` et `terrain_shadow_range` du `config.ini` du jeu : le viewer le lit dans `-data` (`config.ini` ou `assets/config.ini`) ou dans `./assets/config.ini`, et prend les valeurs par défaut sinon.
- **Des jupes** : des murs de 600 descendent le long des bords de chaque bloc, pour qu'un voisin à un autre niveau ne laisse pas de fente. Le jeu n'en a pas besoin.
- **Les couleurs** viennent de `RSArea` et ne sont pas éclairées, comme dans le jeu. Les textures sont une couche en `Blend` par-dessus la couleur, bornée aux bords. Leurs texels sont des index de palette ombrés le long de leur rampe, comme les sommets : l'intensité de chaque sommet est sa lumière (`flag` / 255, moyennée avec `terrain_flat`), ou le soleil avec **Lit Terrain**. Une texture garde donc le ton de son voisinage à l'ombre, sans halo autour des zones texturées. L'index le plus sombre d'une rampe reste lui-même (`RampFirst::ITSELF`), alors que pour les faces des objets il désigne toute la rampe. Le filtrage bilinéaire est fait dans le shader après l'ombrage ; avec `-upscale`, l'image agrandie est ramenée aux index qu'elle utilise. Le relèvement des ombres de `terrain_shadow_lift` ne s'applique qu'aux couleurs des sommets. Elles sont dans **la palette du jeu** (PALETTE.IFF), index 255 transparent. Attention : la palette propre à chaque `RSImage` est celle de `SCRenderer`, qui reste vide tant que `SCRenderer::init` n'a pas été appelé. Le pont ne l'utilise donc pas.
- **Les overlays** (les pistes et leur sol, `objectOverlay`) : comme `renderMapOverlay`.
  - Un mesh par overlay, ses triangles dans l'ordre de `drawList`, dessinés après le terrain (`drawOrder` 1) et sans écrire la profondeur. Chaque triangle recouvre donc les précédents, comme le `GL_LEQUAL` du jeu, sans surélévation.
  - Un triangle à la couleur 255 prend sa couleur de palette ; les autres prennent l'image `GetImageByID(color + 255)`, avec les UV / 128. Leurs pixels transparents prennent la couleur de palette du triangle, comme sous `GL_DECAL`.
  - Les images sont réunies dans un atlas, avec des bords répétés (le `GL_CLAMP_TO_EDGE` du jeu).
- **Les forêts** (`forestChunks`, `plantForest` et `treeModel` dans R4World). RSArea n'a pas d'arbres : le jeu en peint seulement dans quelques textures (217 sur TURKEY). Le pont en génère sur le vert.
  Deux sources, au choix (menu **Forest Source**, `-woods 0|1`) :
  - **Green Terrain + Noise** (`ForestMode::NOISE`, par défaut) : le vert du terrain et un bruit, décrits ci-dessous.
  - **Woods of the Textures** (`ForestMode::TEXTURES`) : un arbre sur les bois peints dans les textures du jeu, comme pour les bâtiments.
    - **Texels d'arbre** : vert foncé, et pas un aplat (leurs voisins varient), car les arbres dessinés sont ombrés alors qu'un aplat de jungle est uni.
    - **Textures retenues** : celles qui ont au moins 8 % de texels d'arbre, soit les bois, les vergers, les bosquets des villages et des parcs, et les buissons des cratères. Les rangs de culture des champs (232, 233) sont écartés.
    - **Densité** : un arbre par 400 de surface, les couronnes se touchent. Les arbres sont posés par les UV de chaque triangle.
    - **Comparaison** :

      | Carte | Noise | Textures |
      |---|---|---|
      | NICARAGA | 11,2 M arbres, 2,4 s | 3,8 M arbres, 0,5 s |
      | TURKEY | 2,1 M arbres, 1,2 s | 70 000 arbres, 0,3 s |
      | MAURITAN | 2,5 M arbres, 1,4 s | 139 000 arbres, 0,4 s |

  Le reste ci-dessous vaut pour les deux sources (modèles, graphe, chargement).
  - **Où** : les triangles de LVL0 de type 3 (la rampe verte de la palette) ou de type 4 (la jungle, rampe verte foncée à partir de 128), sans sommet d'eau. Le type est celui du sommet le plus haut, comme dans `RSArea`. Les cellules de la ville et du port (224, 225, 229, 231) restent nues.
  - **La jungle** (NICARAGA) : des bois sur la plus grande partie, mais moins denses (×0,4), et presque seulement des feuillus, dans la rampe verte foncée de leur sol.
  - **Densité** : un bruit de quelques km fait des bois, jusqu'à 750 arbres/km² en leur cœur, et des arbres isolés entre eux. Sous les textures de bois (216 et 217), la densité est fixe. Il n'y a pas d'arbre près des pistes ni des objets de la carte et de la mission.
  - **Les mêmes arbres à chaque chargement** : le tirage est calé sur la cellule.
  - **Les modèles** : deux essences en low poly, à faces plates, dans les rampes de la palette (PaletteRamp) :
    - un conifère, plus fréquent en altitude ;
    - un feuillu.

    Chacune a une version détaillée jusqu'à 1 500 et une version simple (pyramide, octaèdre) jusqu'à 6 000.
  - **Le graphe** : la carte est découpée en carrés de 5 km, chacun un nœud `LOD` coupé au-delà de 6 000 avec un `Instancer` par essence. Le renderer cule chaque arbre et choisit son niveau ; les arbres projettent leur ombre.
  - **Tout au chargement**, en `Placement` compactes de R4nkine (24 octets par arbre) :
    - TURKEY : environ 2,1 millions d'arbres, 50 Mo, plantés en environ 1 s ;
    - NICARAGA : environ 11 millions d'arbres, 270 Mo, plantés en environ 1,5 s.

    Seuls les arbres visités par le renderer reçoivent une matrice. Pendant le vol, les carrés hors du champ ou au-delà de 6 km sont écartés d'un bloc par leur nœud `LOD`.
- **Les villes, villages, fermes et ports** (`cityModel` dans R4World ; couche **Buildings** du viewer). Les textures du jeu (TXMPACK) sont les mêmes sur toutes les cartes. Sur chaque cellule dont la texture montre des bâtiments, des immeubles sont posés sur ce que l'image montre :
  - **relevés à la main** sur les images (`kBuildings`, 143 bâtiments) :
    - les deux centres-villes (229, 230) ;
    - le port (223 à 228, en morceaux qui ne couvrent chacun que la moitié d'une cellule) ;
    - la ferme (233) ;
    - les villages sur herbe (248, 249) et sur sable (250, 251) ; ces derniers sont sur TURKEY ;
  - **trouvées sur l'image** : les maisons (231), à leurs toits gris clair. Les maisons dessinées seulement en contour restent à plat.

  Le passage d'un texel de l'image au sol se fait par les UV de chaque triangle, ceux que `RSArea` donne : l'image entière sur la cellule, ou une moitié d'image sur un triangle pour les textures 64 × 64 du port.

  **Rendu** :
  - **toit** : l'image elle-même, donc vue de haut la carte reste la texture ;
  - **murs** : des façades faites par le pont (bureaux à fenêtres, maisons, bardage des hangars), sur des étages de 8 ;
  - **éclairage** : par le soleil comme les objets, et ils projettent leur ombre.

  **Aucun bâtiment sur un objet de la carte ou de la mission** : un bâtiment dont l'emprise touche le rayon d'un objet n'est pas posé. Par exemple, les deux silos d'un village de TURKEY laissent la place à l'objet de la carte posé à cet endroit.

  Un nœud par cellule (« city x,y »). NICARAGA en compte environ 2 200, TURKEY 136. `-diag` écrit aussi toutes les textures du jeu (`all_<id>.ppm`), utilisées ou non.
- **Pas encore fait** : le ciel, les nuages et le brouillard.

Le placement des parts reprend `SCMission::loadMission` et `SCMissionScene::spawnCast` :
- **Le joueur** : sa position plus celle de la zone de la mission qu'il nomme.
- **Les autres parts** apparaissent avec la première scène dont le `cast` les contient (le `cast` donne l'index de la part dans la liste) :
  - avec `placement` 0, elles sont placées dans leur zone (position plus `areas[area_id]->position`) ;
  - avec `placement` 1, le jeu les place par rapport au joueur au moment où la scène s'active (`placeRelativeToPlayer`). Le viewer les met au centre de la zone de la scène, à 4000 d'altitude pour un avion (`EntityType::jet`).
- **Ensuite**, une part sous le sol est posée dessus (`RSArea::getY`), et un avion à moins de 10 du sol aussi.
- **Une part placée dans le repère du monde** (`area_id` 255, `placement` 0) reste telle quelle.

## Testé

- Mode Missions : testé avec `TURKEY.PAK` sur les mondes TURKEY et TURKEY22 :
  - 257 762 triangles de terrain, 22 textures et 44 objets de carte ;
  - environ 0,3 s de chargement ;
  - les objets sont posés sur le terrain.

  Aucune mission de TOBIGTRE.TRE n'utilise la Turquie.
- Avec `BIGTRE.TRE` (Strike Commander, textures du terrain prises dans TOBIGTRE.TRE), `MAURITAN.PAK` et `TURKEY.PAK`, on obtient 11 missions et mondes : MISN-1A à 2A, 12A/B, 14A/B, MAURITAN, TURKEY et TURKEY2.
  - MISN-1A : 31 objets de carte et 9 parts.
  - Nouakchott, avec son centre-ville, ses maisons et son port, et 28 fermes autour ; 3 074 bâtiments en tout.
  - 2,5 millions d'arbres, plantés en environ 1,4 s.
  - Les forêts sont en carrés de 2 km groupés par bloc : la culture passe de 23 à 5 ms sur cette carte.
- Avec `SYRIA.PAK`, MISN-A1 (la première mission de Tactical Operations) montre :
  - les deux pistes (236 triangles d'overlay) ;
  - les 41 parts réparties dans 10 scènes, plus 8 sans scène ;
  - la base d'attaque (scène 0) et la base d'entraînement (scène 6), posées au sol à côté de leurs pistes.
- Testé sur Linux (GCC 13) avec une copie de libRealSpace (CMake complet du projet, plus les trois lignes ci-dessus) et R4nkine `6af0d4b`. Les données venaient de `TOBIGTRE.TRE`.
- 342 objets listés. Rendu vérifié sur F-16DES, A-10 (verrière), 747, CARRIERW et T-72.
- R4nkine doit être au moins à `6af0d4b` (le mode `AlphaMode::ADDITIVE`, ajouté pour les verrières).
- Les objets sans géométrie (les obus `20MM`…) s'affichent vides, c'est normal.
- T-72 : les bandes derrière les chenilles sont les faces `TRAX` (propriété 9, `flags[1] = 255`), des traces au sol que `SCRenderer` dessine aussi. Si on veut les cacher, c'est probablement `flags[1] == 255` qui les distingue.| Textures (`uvs`, `qmapuvs`, `images`) | Images RGBA à travers la palette, index 255 transparent ; `GL_LINEAR` sans mipmaps et bords bornés (`Image::mipmaps = false`, `clampS/T`), comme `createTextureInGPU` |
| Passe texture : mélange `SRC_ALPHA, ONE_MINUS_SRC_ALPHA` et test alpha `> 0,1` | `AlphaMode::BLEND` avec `alphaTest = 0,1` |
| Propriété 6 ou 9 : la texture est la face (`GL_LESS`, profondeur écrite) | Matériau `rampTextured`, `blendDepthWrite` |
| Autres faces texturées : sur la face colorée (`GL_EQUAL`, profondeur non écrite) | `depthEqual`, `depthWrite = false` : la texture n'apparaît que là où la face colorée est dessinée |
