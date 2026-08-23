# amalgame-pdf

Générateur **PDF 1.4** souverain, **pur Amalgame** — aucune dépendance
native de rendu (juste OpenSSL, transitif via `amalgame-crypto`, pour
l'encodage UTF-8→WinAnsi). Documents multi-pages avec retour à la
ligne automatique : factures, rapports, éditions imprimables.

## Capacités

- Texte **Liberation Sans** / **Liberation Sans Bold** (police TrueType
  **embarquée** dans le PDF, metric-compatible Helvetica/Arial, SIL Open
  Font License — voir `vendor/OFL.txt`), encodage **WinAnsi** (Latin-1 +
  `€` et guillemets typographiques) — couvre le français. Vendorisée en C
  dans `vendor/` : aucun fichier de police externe requis à l'exécution.
- Alignement à gauche et à droite (métriques **réelles** lues dans le
  fichier de police, pas une heuristique à chasse fixe).
- **Multi-page** (`NewPage()`) — un document peut contenir autant de pages
  que nécessaire, toutes de la même taille.
- **Retour à la ligne automatique** (`WrapText`/`Flow`/`FlowBold`) — découpe
  un texte en lignes tenant dans une largeur donnée, dessine colonne par
  colonne jusqu'à une limite basse, renvoie le texte non dessiné (à
  l'appelant de décider quoi en faire : page suivante, colonne suivante).
- Vecteurs : traits, rectangles (contour et remplis), couleurs RVB.
- Images **JPEG** embarquées (filtre `DCTDecode`), partagées entre toutes
  les pages d'un même document.
- Rendu final en octets (`List<int>`) — à servir en HTTP
  (`HttpResponse.Bytes`) ou joindre à un e-mail.

Repère : coordonnées en **points**, origine **coin haut-gauche** (y vers le
bas) ; conversion vers le repère PDF natif faite en interne. Tout en entiers.

## Exemple — facture simple (1 page)

```amalgame
import Amalgame.Formats.Pdf

let p: Pdf = Pdf.A4Portrait()
p.TextBold(40, 60, 18, "FACTURE")
p.Text(40, 90, 11, "Client : Jean Dupont")
p.Line(40, 110, 555, 110, 1)
p.TextRight(555, 140, 11, "90.00 €")
let bytes: List<int> = p.Build()
```

## Exemple — texte qui coule sur plusieurs pages

```amalgame
let p: Pdf = Pdf.A4Portrait()
var reste: string = article
var enCours: bool = true
while (enCours) {
    reste = p.Flow(40, 80, 500, 11, 15, reste, 780)
    if (String_Length(reste) == 0) { enCours = false } else { p.NewPage() }
}
let bytes: List<int> = p.Build()
```

## API (classe `Pdf`)

- `Pdf.A4Portrait()` / `Pdf.A4Landscape()` / `Pdf.New(wPts, hPts)`
- `NewPage()` — clôt la page courante, en ouvre une nouvelle (même taille).
- `Text` / `TextBold` / `TextColor` / `TextBoldColor(x, yTop, size, s [, rgb])`
- `TextRight` / `TextRightBold` / `TextRightColor(xRight, yTop, size, s [, rgb])`
- `WrapText(width, size, text) -> List<string>` — découpe pure, sans dessin
  (`\n` = coupure de paragraphe forcée).
- `Flow(x, yTop, width, size, lineHeight, text, bottomLimit) -> string` /
  `FlowBold(...)` — dessine, renvoie le texte non dessiné (`""` si tout
  tenait).
- `Line(x1, y1, x2, y2, widthPts)` / `LineColor(..., rgb)`
- `Rect(x, yTop, w, h, widthPts)` (contour) / `FillRect(x, yTop, w, h, rgb)`
- `TextWidth(size, s) -> int`
- `AddJpeg(bytes, wPx, hPx) -> int` (handle) / `DrawImage(handle, x, yTop, w, h)`
- `Build() -> List<int>`

`rgb` est un entier `0xRRGGBB`.

## Limites (v0.3)

- Une seule taille de page par document (pas de mélange portrait/paysage
  dans le même PDF).
- Espace colorimétrique **RGB** uniquement (`rg`/`RG`) — pas de CMYK.
  Certains imprimeurs professionnels (ex. Vistaprint) l'exigent ; à
  ajouter si le besoin se présente.
- Pas de gestion de fond perdu (bleed)/marge de sécurité — l'appelant doit
  dimensionner sa page en conséquence si le PDF est destiné à l'impression
  professionnelle.
- Images : JPEG (RGB) uniquement, via `DCTDecode`.
- `Flow` est une primitive de dessin en colonne simple — pas un moteur de
  mise en page multi-colonnes : à l'appelant de gérer la disposition
  (plusieurs colonnes, plusieurs pages) en rappelant `Flow` avec le texte
  restant.
- Limite connue : si une coupure de `Flow` tombe exactement sur une
  frontière de paragraphe (`\n` d'origine), ce saut de paragraphe est
  perdu dans le texte renvoyé (les lignes restantes sont rejointes par un
  simple espace) — cas limite rare, pas corrigé pour rester simple.

## Tests

```bash
./tests/run_tests.sh /path/to/amc
```

Vérifie la structure multi-page (comptage des marqueurs PDF réels dans les
octets produits par `Build()`) et le retour à la ligne automatique
(reconstruction du contenu, logique de débordement). Le rendu visuel réel
(un vrai lecteur PDF, poppler `pdftoppm`) a été vérifié manuellement avant
la release — texte multi-page, image sur une page non-initiale, débordement
propre entre deux pages — cette suite protège contre une régression future.

## Licence

Code : Apache-2.0.

Police embarquée (Liberation Sans / Liberation Sans Bold) : SIL Open Font
License 1.1 — voir `vendor/OFL.txt`. La licence autorise explicitement
l'embarquement/la redistribution dans un document généré ; aucune
attribution requise dans les PDF produits.
