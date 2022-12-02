// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { spawn } from 'child_process';
import { i } from './i18n';
import { Session } from './session';
import { Uri } from './util/uri';

function runVcpkg(vcpkgCommand: string | undefined, args: Array<string>): Promise<string> {
  return new Promise((accept, reject) => {
    if (!vcpkgCommand) {
      reject(i`VCPKG_COMMAND was not set`);
      return;
    }

    const subproc = spawn(vcpkgCommand, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let result = '';
    subproc.stdout.on('data', (chunk) => { result += chunk; });
    subproc.stderr.pipe(process.stdout);
    subproc.on('error', (err) => { reject(err); });
    subproc.on('close', (code: number, signal) => {
      if (code === 0) { accept(result); }
      reject(i`Running vcpkg internally returned a nonzero exit code: ${code}`);
    });
  });
}

export function vcpkgFetch(session: Session, fetchKey: string): Promise<string> {
  return runVcpkg(session.vcpkgCommand, ['fetch', fetchKey, '--x-stderr-status']).then((output) => {
    return output.trimEnd();
  }, (error) => {
    if (fetchKey === 'git') {
      session.channels.warning('failed to fetch git, falling back to attempting to use git from the PATH');
      return Promise.resolve('git');
    }

    return Promise.reject(error);
  });
}

export function vcpkgDownload(session: Session, destination: string, sha512: string | undefined, uris: Array<Uri>) : Promise<string> {
  const args = ['x-download', destination];
  if (sha512) {
    args.push(`--sha512=${sha512}`);
  } else {
    args.push('--skip-sha512');
  }

  for (const uri of uris) {
    args.push(`--url=${uri.toString()}`);
  }

  return runVcpkg(session.vcpkgCommand, args).then((output) => {
    return output.trimEnd();
  });
}
