# amalgame-pdf

Générateur **PDF 1.4** souverain, **pur Amalgame** — aucune dépendance native.
Conçu pour les documents d'une page : factures, reçus, bons de commande.

## Capacités

- Texte **Helvetica** / **Helvetica-Bold**, encodage **WinAnsi** (Latin-1 +
  `€` et guillemets typographiques) — couvre le français.
- Alignement à gauche et à droite (métriques Helvetica intégrées).
- Vecteurs : traits, rectangles (contour et remplis), couleurs RVB.
- Images **JPEG** embarquées (filtre `DCTDecode`).
- Rendu final en octets (`List<int>`) — à servir en HTTP
  (`HttpResponse.Bytes`) ou joindre à un e-mail.

Repère : coordonnées en **points**, origine **coin haut-gauche** (y vers le
bas) ; conversion vers le repère PDF natif faite en interne. Tout en entiers.

## Exemple

```amalgame
import Amalgame.Formats.Pdf

let p: Pdf = Pdf.A4Portrait()
p.TextBold(40, 60, 18, "FACTURE")
p.Text(40, 90, 11, "Client : Jean Dupont")
p.Line(40, 110, 555, 110, 1)
p.TextRight(555, 140, 11, "90.00 €")
let bytes: List<int> = p.Build()
```

## API (classe `Pdf`)

- `Pdf.A4Portrait()` / `Pdf.A4Landscape()` / `Pdf.New(wPts, hPts)`
- `Text` / `TextBold` / `TextColor` / `TextBoldColor(x, yTop, size, s [, rgb])`
- `TextRight` / `TextRightBold` / `TextRightColor(xRight, yTop, size, s [, rgb])`
- `Line(x1, y1, x2, y2, widthPts)` / `LineColor(..., rgb)`
- `Rect(x, yTop, w, h, widthPts)` (contour) / `FillRect(x, yTop, w, h, rgb)`
- `TextWidth(size, s) -> int`
- `AddJpeg(bytes, wPx, hPx) -> int` (handle) / `DrawImage(handle, x, yTop, w, h)`
- `Build() -> List<int>`

`rgb` est un entier `0xRRGGBB`.

## Limites (v0.1)

- Une seule page.
- Images : JPEG (RGB) uniquement, via `DCTDecode`.
- Pas de retour à la ligne automatique (positionner chaque ligne).

## Licence

Apache-2.0.
