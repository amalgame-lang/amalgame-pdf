/*
 * facade-stub.h -- runtime header for the pdf facade.
 *
 * Most of the package's API is implemented entirely in `facade.am`
 * (pure Amalgame). This file exists primarily because the manifest's
 * `[stdlib].header` field is required by PackageRegistry.LoadFrom in
 * amc -- but it also carries the embedded TrueType font data (v0.3.0,
 * font embedding) since `@c` blocks in facade.am need these C arrays
 * in scope. Vendored, not generated at build time: Liberation Sans is
 * metric-compatible with Helvetica/Arial, SIL Open Font License (see
 * vendor/OFL.txt -- explicitly permits bundling/embedding/redistribution),
 * already the de facto free substitute used across the Linux/print
 * ecosystem for exactly this purpose.
 */
#include "vendor/liberation_sans_regular.h"
#include "vendor/liberation_sans_bold.h"
#include "vendor/ttf_parse.h"
