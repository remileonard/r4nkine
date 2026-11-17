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
| `src/r4bridge/CMakeLists.txt` | La bibliothèque statique `r4bridge` |
| `src/executables/r4objviewer/main.cpp` | Le viewer : liste des objets des TRE, caméra, GUI, post-process |
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
   ```
   - Sans `-tre`, le viewer ouvre `TOBIGTRE.TRE` s'il existe, sinon `OBJECTS.TRE` et `MISC.TRE`.
   - Panneau de gauche, **Objects** : la liste des objets des TRE. Un clic choisit un objet, la molette fait défiler la liste, et il y a des boutons Précédent et Suivant.
   - Panneau de droite :
     - **Render** : fil de fer, textures, facteur de LOD, lumière, ciel, angles de la caméra ;
     - **Post Process** : MSAA, FXAA, scanlines, palette… ;
     - **Performance** : FPS, courbes des temps (image, cull, draw), draw calls, triangles, changements d'état, items et nœuds ;
     - **Object** : sommets, faces, textures, les LOD avec leurs distances (celui affiché est marqué), le temps de chargement et de conversion, les meshes R4nkine ;
     - **Textures** : chaque image de l'objet, avec sa taille et son nombre de frames. `-textures` ouvre cette section au départ.
   - Souris : bouton gauche pour tourner, molette pour le zoom, bouton droit pour déplacer.
   - Clavier : flèches haut et bas pour l'objet précédent ou suivant, Échap pour quitter.
   - `-frames N -shot image.bmp` fait une capture après N images, utile pour les tests.

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
| Passe couleur : triangle `color`, quad `color − 1` | Matériau `Shading::PaletteRamp` ; l'index est dans le rouge de la couleur de sommet |
| Éclairage (ambiant 3/8, lumière 5/8, `shadeAlongRamp`) | Image 256 × 32 (`rampImage`) : ligne k = intensité k/31 |
| Face double : `flags[2] == 1` (triangle), `attrs->props2 == 1` (quad) | `doubleSided`, sinon face arrière éliminée comme dans le jeu |
| Passe texture (LOD0, toutes les `uvs` et `qmapuvs`) | Images RGBA à travers la palette ; index 255 transparent (`Mask`, seuil 0,1 comme `glAlphaFunc`) |
| Propriété 6 ou 9 : la texture est la face (`GL_LESS`) | Matériau `rampTextured` |
| Autres faces texturées : sur la face colorée (`GL_EQUAL`) | Même matériau avec `decal = true` (polygon offset) |
| Passe transparente : propriété 2, couleur `color − 1`, `glBlendFunc(GL_ONE, GL_ONE)` | `AlphaMode::Additive`, palette sans rampe (couleur × intensité) |

La lumière se règle avec `r4bridge::Lighting` ou avec les curseurs Ambient et Light du viewer.

Pour afficher un objet ailleurs que dans le viewer, par exemple dans un futur `SCRenderer` sur R4nkine :

```cpp
RSEntity *entity = ...;                       // déjà chargé par libRealSpace
auto model = r4bridge::toModel(entity, *palette.GetColorPalette(), palette.ranges);
r4::scene::NodePtr node = renderer.upload(*model);
someGroup->addChild(node);                    // et le renderer s'en occupe
```

## Testé

- Testé sur Linux (GCC 13) avec une copie de libRealSpace (CMake complet du projet, plus les trois lignes ci-dessus) et R4nkine `6af0d4b`. Les données venaient de `TOBIGTRE.TRE`.
- 342 objets listés. Rendu vérifié sur F-16DES, A-10 (verrière), 747, CARRIERW et T-72.
- R4nkine doit être au moins à `6af0d4b` (le mode `AlphaMode::Additive`, ajouté pour les verrières).
- Les objets sans géométrie (les obus `20MM`…) s'affichent vides, c'est normal.
- T-72 : les bandes derrière les chenilles sont les faces `TRAX` (propriété 9, `flags[1] = 255`), des traces au sol que `SCRenderer` dessine aussi. Si on veut les cacher, c'est probablement `flags[1] == 255` qui les distingue.
