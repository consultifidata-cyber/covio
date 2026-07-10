'use strict';

// eslint.config.js -- RE-3 (Release Engineering, Code Quality Gates).
// Flat config (ESLint 9+) -- the legacy .eslintrc format is deprecated
// upstream, so this project starts directly on the current format rather
// than adopting something already on its way out.
//
// Two separate rule sets, matching this app's own two genuinely different
// JS runtime contexts (documented in main.js's own header comment: the
// main process has full Node/Electron access; the renderer is sandboxed,
// contextIsolation on, and talks to the main process only through
// preload.js's contextBridge):
//   - src/main/**, test/**  -- CommonJS (require/module.exports), Node globals.
//   - src/renderer/**       -- ES modules (import/export), browser globals only
//                              (no Node globals -- nodeIntegration is off).
//
// RE-3 is configuration-only: this file does not itself reformat or fix
// anything. See Docs/CODE_QUALITY.md for the policy this config implements.

const js = require('@eslint/js');

const NODE_GLOBALS = {
  require: 'readonly',
  module: 'readonly',
  exports: 'writable',
  __dirname: 'readonly',
  __filename: 'readonly',
  process: 'readonly',
  console: 'readonly',
  global: 'readonly',
  Buffer: 'readonly',
  setTimeout: 'readonly',
  clearTimeout: 'readonly',
  setInterval: 'readonly',
  clearInterval: 'readonly',
  AbortController: 'readonly',
  fetch: 'readonly',
};

const BROWSER_GLOBALS = {
  window: 'readonly',
  document: 'readonly',
  console: 'readonly',
  setTimeout: 'readonly',
  clearTimeout: 'readonly',
  setInterval: 'readonly',
  clearInterval: 'readonly',
};

module.exports = [
  js.configs.recommended,
  {
    // Main process + preload + native test files + this config file itself
    // (also CommonJS -- ESLint doesn't exempt its own config from its own
    // rules, so it needs a glob match too, caught by actually running this
    // config against itself during RE-3's own verification pass) +
    // scripts/ (RE-6's electron-builder hooks, e.g.
    // afterAllArtifactBuild.js -- also CommonJS, same reason; this exact
    // omission was caught the same way, by actually running ESLint against
    // the real tree after adding a new scripts/ file, not assumed safe).
    files: ['src/main/**/*.js', 'test/**/*.js', 'eslint.config.js', 'scripts/**/*.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'commonjs',
      globals: NODE_GLOBALS,
    },
    rules: {
      // caughtErrorsIgnorePattern (not argsIgnorePattern -- that option only
      // covers function arguments, not catch-clause bindings) recognizes
      // this codebase's own established `catch (_e)` "intentionally
      // unused" convention, already used throughout deviceClient.js/
      // backendClient.js/etc. -- caught by actually running this config
      // against the real codebase, not assumed.
      'no-unused-vars': ['warn', { argsIgnorePattern: '^_', caughtErrorsIgnorePattern: '^_' }],
      'no-console': 'off', // Serial-equivalent logging is this app's own
                            // established diagnostic convention throughout
                            // main.js/discovery.js/etc. -- not a smell here.
    },
  },
  {
    // Renderer: ES modules, browser globals only -- no Node access by design
    // (nodeIntegration: false, contextIsolation: true -- main.js's own
    // header comment). window.covio is the ONLY bridge to the main process.
    files: ['src/renderer/**/*.js'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'module',
      globals: BROWSER_GLOBALS,
    },
    rules: {
      // caughtErrorsIgnorePattern (not argsIgnorePattern -- that option only
      // covers function arguments, not catch-clause bindings) recognizes
      // this codebase's own established `catch (_e)` "intentionally
      // unused" convention, already used throughout deviceClient.js/
      // backendClient.js/etc. -- caught by actually running this config
      // against the real codebase, not assumed.
      'no-unused-vars': ['warn', { argsIgnorePattern: '^_', caughtErrorsIgnorePattern: '^_' }],
      'no-restricted-globals': [
        'error',
        { name: 'require', message: 'Renderer code must not use require() -- see contextIsolation in main.js.' },
        { name: 'process', message: 'Renderer code must not access process -- see nodeIntegration:false in main.js.' },
      ],
    },
  },
  {
    ignores: ['node_modules/**', 'dist/**', 'out/**'],
  },
];
