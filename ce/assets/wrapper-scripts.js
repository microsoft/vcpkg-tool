const { existsSync: exists, chmod, chmodSync } = require('fs');
const { stat, copyFile, unlink } = require('fs').promises;
const { join } = require('path');

/**
 * This script creates/removes custom wrapper scripts for vcpkg-ce.
 */
async function findScriptFolder() {
  const root = `${__dirname}`;
  let s = root;
  while (true) {
    s = join(s, '..');

    // did we find a folder where the script is in the folder (windows style)
    if (exists(s) && (await stat(s)).isDirectory() && (
      exists(join(s, 'ce_.ps1')) ||
      exists(join(s, 'ce_.cmd')) ||
      exists(join(s, 'ce.ps1')) ||
      exists(join(s, 'ce.cmd')))
    ) {
      return s;
    }

    // find it in a bin folder 
    for (const f of ['.bin', 'bin']) {
      const b1 = join(s, f);
      if (exists(b1) && (await stat(b1)).isDirectory() && (
        exists(join(b1, 'ce_')) ||
        exists(join(b1, 'ce')) ||
        exists(join(b1, 'ce.ps1')) ||
        exists(join(b1, 'ce_.ps1')))
      ) {
        return b1;
      }
    }

    if (s === join(s, '..')) {
      return undefined;
    }
  }
}

async function create() {
  const folder = await findScriptFolder();
  if (!folder) {
    console.error("Unable to find install'd folder. Aborting.")
    return process.exit(1);
  }
  const files = {
    'ce': {
      source: 'ce',
      install: process.platform !== 'win32'
    },
    'ce.ps1': {
      source: 'ce.ps1',
      install: true
    },
    'ce.cmd': {
      source: 'ce.ps1',
      install: process.platform === 'win32'
    }
  }

  for (const file of ['ce_', 'ce_.ps1', 'ce_.cmd']) {
    // remove the normally created scripts 
    const target = join(folder, file);
    if (exists(target)) {
      await unlink(target);
    }
  }

  // we install all of these, because an installation from bash can still work with powershell
  for (const file of Object.keys(files)) {
    console.log(`file: ${file} <== ${files[file].source} if ${files[file].install}`)
    if (files[file].install) {
      const target = join(folder, file);

      // remove the symlink/script file if it exists
      if (exists(target)) {
        await unlink(target);
      }
      // copy the shell script into it's place
      console.log(`copyFile: ${join(__dirname, "scripts", files[file].source)}  ==>  ${target} }`);
      await copyFile(join(__dirname, "scripts", files[file].source), target);

      chmodSync(target, 0o765);
    }
  }
}

async function remove() {
  const folder = await findScriptFolder();
  if (!folder) {
    return process.exit(0);
  }

  for (const file of ['ce', 'ce.ps1', 'ce.cmd']) {
    // remove the custom created scripts 
    const target = join(folder, file);
    if (exists(target)) {
      await unlink(target);
    }
  }
}

if (process.argv[2] !== 'remove') {
  console.error('Installing Scripts');
  create();
} else {
  console.error('After this is uninstalled, you should close this terminal.');
  remove()
}
