/*
 * facade-stub.h — empty runtime header for the pdf facade.
 *
 * The package's API is implemented entirely in `facade.am` (pure
 * Amalgame). This file exists only because the manifest's
 * `[stdlib].header` field is currently required by
 * PackageRegistry.LoadFrom in amc. The user binary's #include of
 * this header is a no-op.
 */
