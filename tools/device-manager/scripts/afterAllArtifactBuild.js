'use strict';

// scripts/afterAllArtifactBuild.js -- RE-6 (Desktop Packaging & Windows
// Installer). electron-builder's own `afterAllArtifactBuild` hook: called
// once after every configured Windows target (nsis, portable) has produced
// its artifact. Generates a SHA256 checksum file next to each real
// packaged artifact, so a technician (or a future RE-7 release step) can
// verify a download wasn't corrupted or tampered with, without a separate,
// easy-to-forget manual step.
//
// Runs as part of `npm run dist` (electron-builder itself), NOT as part of
// `.github/workflows/ci.yml` -- packaging is deliberately kept out of the
// fast push/PR CI loop (RE-6's own explicit scope); see
// `.github/workflows/desktop-package.yml` and `Docs/DESKTOP_PACKAGING.md`.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

module.exports = async function afterAllArtifactBuild(context) {
  const artifactPaths = context.artifactPaths || [];
  const checksumPaths = [];

  for (const artifactPath of artifactPaths) {
    // .blockmap files are electron-builder's own auto-update metadata --
    // never a distributable artifact a technician downloads directly, so a
    // checksum for one would be meaningless noise, not a real deliverable.
    if (artifactPath.endsWith('.blockmap')) continue;
    if (!fs.existsSync(artifactPath) || !fs.statSync(artifactPath).isFile()) continue;

    const hash = crypto.createHash('sha256');
    hash.update(fs.readFileSync(artifactPath));
    const digest = hash.digest('hex');

    // Standard sha256sum-compatible line format ("<hash>  <filename>", two
    // spaces) so a technician can verify with either `certutil -hashfile
    // <file> SHA256` (Windows, comparing the hash by eye) or
    // `sha256sum -c <file>.sha256` (any platform with coreutils) --
    // deliberately not a bespoke format only this project's own tooling
    // could read.
    const checksumPath = `${artifactPath}.sha256`;
    const fileName = path.basename(artifactPath);
    fs.writeFileSync(checksumPath, `${digest}  ${fileName}\n`, 'utf8');
    checksumPaths.push(checksumPath);
  }

  // Returned paths are recognized by electron-builder as additional build
  // outputs alongside the artifacts they check -- not published anywhere
  // by this phase (RE-6 explicitly does not publish releases), but keeps
  // them visible to anything inspecting the build result programmatically
  // (e.g. a future RE-7 step).
  return checksumPaths;
};
