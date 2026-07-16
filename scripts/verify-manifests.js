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

function extractList(sourceText, listName) {
    const re = new RegExp(listName + '\\s*=\\s*\\[([\\s\\S]*?)\\];');
    const m = sourceText.match(re);
    if (!m) { return null; }
    return m[1]
        .split(',')
        .map(s => s.trim().replace(/^['"]|['"]$/g, ''))
        .filter(Boolean)
        .sort();
}

function listDiskFiles(dir, filter) {
    const abs = path.join(root, dir);
    if (!fs.existsSync(abs)) { return []; }
    return fs.readdirSync(abs, { withFileTypes: true })
        .filter(filter)
        .map(e => e.name)
        .sort();
}

const ext = fs.readFileSync(path.join(root, 'src', 'extension.ts'), 'utf8');
const un  = fs.readFileSync(path.join(root, 'uninstall.js'),        'utf8');

const disk = {
    AGENT_FILES:       listDiskFiles('agents',       e => e.isFile()      && e.name.endsWith('.md')),
    INSTRUCTION_FILES: listDiskFiles('instructions', e => e.isFile()      && e.name.endsWith('.md')),
    SKILL_DIRS:        listDiskFiles('skills',       e => e.isDirectory()),
    PROMPT_FILES:      listDiskFiles('prompts',      e => e.isFile()      && e.name.endsWith('.md')),
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
