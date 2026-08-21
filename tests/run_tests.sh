#!/bin/bash
# amalgame-pdf — Test Runner. Requires amc 0.7.7+.
#
# Une dépendance (amalgame-crypto, pour Bytes.FromString/UTF-8) —
# résolue via `amc package add` dans un dossier temporaire séparé
# (pour obtenir sa facade.am en cache), puis compilée comme source
# LOCALE aux côtés de facade.am et du test, exactement comme le fait
# amc pour n'importe quel projet consommateur (voir CMSPress build.sh)
# — pas de résolution "vraie" via `amc package add amalgame-pdf`
# lui-même, puisqu'on veut justement tester la source LOCALE, pas
# encore publiée/taguée (utile en CI sur une PR, avant tout tag).
set -u

if [ $# -ge 1 ]; then AMC="$1"
elif [ -n "${AMC:-}" ]; then :
elif command -v amc >/dev/null 2>&1; then AMC="$(command -v amc)"
else echo "ERROR: amc not found." >&2; exit 2
fi
[ -x "$AMC" ] || { echo "ERROR: amc not executable: $AMC" >&2; exit 2; }
AMC="$(cd "$(dirname "$AMC")" && pwd)/$(basename "$AMC")"

AMC_DIR="$(cd "$(dirname "$AMC")" && pwd)"
AMC_RUNTIME=""
for cand in "$HOME/.local/share/amalgame/runtime" "/usr/local/share/amalgame/runtime"; do
    [ -d "$cand" ] && { AMC_RUNTIME="$cand"; break; }
done
[ -n "$AMC_RUNTIME" ] || { echo "ERROR: amc runtime/ not found." >&2; exit 2; }
LIBAMALGAME=""
for cand in "$HOME/.local/share/amalgame/lib/libamalgame.a" "/usr/local/share/amalgame/lib/libamalgame.a"; do
    [ -f "$cand" ] && { LIBAMALGAME="$cand"; break; }
done
[ -n "$LIBAMALGAME" ] || { echo "ERROR: libamalgame.a not found." >&2; exit 2; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PKG_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'

echo ""
echo "════════════════════════════════════════════"
echo "  amalgame-pdf — Test Suite"
echo "════════════════════════════════════════════"
echo "  amc:     $AMC ($("$AMC" --version 2>&1 | head -1))"
echo "  package: $PKG_ROOT"
echo ""

# Résolution de la dépendance crypto dans un dossier à part (jamais
# celui où on compile ensuite — amc package add y écrirait son propre
# amalgame.toml/lock, qu'on ne veut pas voir interférer).
DEP_DIR="$(mktemp -d -t apdf-deps-XXXXXX)"
BUILD_DIR="$(mktemp -d -t apdf-tests-XXXXXX)"
trap 'rm -rf "$DEP_DIR" "$BUILD_DIR"' EXIT

echo "── amc package add crypto (dépendance) ──"
( cd "$DEP_DIR" && "$AMC" package add crypto ) || { echo -e "${RED}FAIL${NC} (package add crypto)"; exit 1; }
# `amc package add` peuple le cache GLOBAL (~/.amalgame/packages/...),
# pas DEP_DIR lui-même (qui ne reçoit qu'un amalgame.toml/lock pointant
# dessus) — chercher au bon endroit, le plus récent si plusieurs
# versions sont déjà en cache d'un run précédent.
CRYPTO_FACADE="$(find "$HOME/.amalgame/packages" -path "*/amalgame-crypto/*/facade.am" 2>/dev/null | sort | tail -1)"
[ -f "$CRYPTO_FACADE" ] || { echo -e "${RED}FAIL${NC} (facade.am crypto introuvable)"; exit 1; }

cd "$BUILD_DIR"
cp "$PKG_ROOT/facade.am" ./pdf_facade.am
cp "$CRYPTO_FACADE" ./crypto_facade.am
cp "$SCRIPT_DIR/stdlib_pdf.am" ./test.am

echo "── amc: génération du .c ──"
if ! "$AMC" crypto_facade.am pdf_facade.am test.am -o test 2>&1; then
    echo -e "${RED}FAIL${NC} (amc)"
    exit 1
fi
[ -f test.c ] || { echo -e "${RED}FAIL${NC} (pas de test.c)"; exit 1; }

echo "── gcc: compilation + lien contre le runtime ──"
# -lssl -lcrypto : amalgame-crypto embarque des bindings OpenSSL bruts
# (@c, ECDSA/EVP/BN...) pour Jws/Ecdh — nécessaires même si stdlib_pdf.am
# lui-même n'utilise que Bytes.FromString (le lieur résout TOUT le
# facade de crypto, pas juste les symboles réellement appelés).
if ! gcc -O2 -I"$AMC_RUNTIME" test.c "$LIBAMALGAME" \
        -lgc -lm -lcurl -lz -ldl -lpthread -lssl -lcrypto -o test 2>&1; then
    echo -e "${RED}FAIL${NC} (gcc link)"
    exit 1
fi
[ -x ./test ] || { echo -e "${RED}FAIL${NC} (pas de binaire produit)"; exit 1; }
echo ""

echo "── Amalgame.Formats.Pdf ────────────────────────"
OUTPUT="$(./test 2>&1)"
echo "$OUTPUT" | sed 's/^/  /'
echo ""

PASS=$(echo "$OUTPUT" | grep -c '^\[PASS\]')
FAIL=$(echo "$OUTPUT" | grep -c '^\[FAIL\]')

echo "────────────────────────────────────────────"
echo -e "  ${GREEN}PASS: $PASS${NC}  |  ${RED}FAIL: $FAIL${NC}"
echo "────────────────────────────────────────────"
echo ""
[ "$FAIL" -eq 0 ] && exit 0 || exit 1
