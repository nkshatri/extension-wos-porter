// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
// Verify that the three asset manifests are in sync:
//   1. AGENT_FILES / INSTRUCTION_FILES / SKILL_DIRS / PROMPT_FILES in src/extension.ts
//   2. Same four lists in uninstall.js
//   3. Actual files/dirs under agents/, instructions/, skills/, prompts/
//
// Runs as part of `vscode:prepublish` so a mismatch fails the VSIX build.

'use strict';
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');

// Matches any `NAME = [ ... ];` assignment. Hardcoded literal (no RegExp built
// from a variable) so the pattern is fixed and ReDoS-free; the caller selects
// the list it wants by name from the captured groups.
const LIST_RE = /(\w+)\s*=\s*\[([\s\S]*?)\];/g;

function extractList(sourceText, listName) {
    for (const m of sourceText.matchAll(LIST_RE)) {
        if (m[1] !== listName) { continue; }
        return m[2]
            .split(',')
            .map(s => s.trim().replace(/^['"]|['"]$/g, ''))
            .filter(Boolean)
            .sort();
    }
    return null;
}

// Absolute paths to the four asset directories, each built from a string
// literal (no variable ever flows into path.join) — so there is no path to
// traverse. listDiskFiles receives a ready-made absolute dir, not a name.
const ASSET_DIRS = {
    AGENT_FILES:       path.join(root, 'agents'),
    INSTRUCTION_FILES: path.join(root, 'instructions'),
    SKILL_DIRS:        path.join(root, 'skills'),
    PROMPT_FILES:      path.join(root, 'prompts'),
};

function listDiskFiles(absDir, filter) {
    if (!fs.existsSync(absDir)) { return []; }
    return fs.readdirSync(absDir, { withFileTypes: true })
        .filter(filter)
        .map(e => e.name)
        .sort();
}

const ext = fs.readFileSync(path.join(root, 'src', 'extension.ts'), 'utf8');
const un  = fs.readFileSync(path.join(root, 'uninstall.js'),        'utf8');

const disk = {
    AGENT_FILES:       listDiskFiles(ASSET_DIRS.AGENT_FILES,       e => e.isFile()      && e.name.endsWith('.md')),
    INSTRUCTION_FILES: listDiskFiles(ASSET_DIRS.INSTRUCTION_FILES, e => e.isFile()      && e.name.endsWith('.md')),
    SKILL_DIRS:        listDiskFiles(ASSET_DIRS.SKILL_DIRS,        e => e.isDirectory()),
    PROMPT_FILES:      listDiskFiles(ASSET_DIRS.PROMPT_FILES,      e => e.isFile()      && e.name.endsWith('.md')),
};

function arraysEqual(a, b) {
    if (a.length !== b.length) { return false; }
    for (let i = 0; i < a.length; i++) { if (a[i] !== b[i]) { return false; } }
    return true;
}

let failed = false;
for (const key of Object.keys(disk)) {
    const extList  = extractList(ext, key);
    const unList   = extractList(un,  key);
    const diskList = disk[key];

    if (!extList) { console.error(`FAIL ${key}: not found in src/extension.ts`); failed = true; continue; }
    if (!unList)  { console.error(`FAIL ${key}: not found in uninstall.js`);       failed = true; continue; }

    const okExt = arraysEqual(extList,  diskList);
    const okUn  = arraysEqual(unList,   diskList);
    const okXU  = arraysEqual(extList,  unList);

    if (okExt && okUn && okXU) {
        console.log(`OK   ${key}  (${diskList.length} entries)`);
        continue;
    }
    failed = true;
    console.error(`FAIL ${key}  ext=${extList.length}  un=${unList.length}  disk=${diskList.length}`);
    const only = (a, b) => a.filter(x => !b.includes(x));
    if (!okExt) {
        console.error(`  ext ⊕ disk: onlyInExt=[${only(extList, diskList).join(',')}]  onlyOnDisk=[${only(diskList, extList).join(',')}]`);
    }
    if (!okUn) {
        console.error(`  un  ⊕ disk: onlyInUn=[${only(unList, diskList).join(',')}]  onlyOnDisk=[${only(diskList, unList).join(',')}]`);
    }
    if (!okXU) {
        console.error(`  ext ⊕ un  : onlyInExt=[${only(extList, unList).join(',')}]  onlyInUn=[${only(unList, extList).join(',')}]`);
    }
}

if (failed) {
    console.error('\nManifest mismatch. Update src/extension.ts and uninstall.js to match the on-disk asset directories.');
    process.exit(1);
}
console.log('\nAll asset manifests in sync.');
