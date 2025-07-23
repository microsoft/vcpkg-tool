// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { spawn } from 'child_process';
import { i } from './i18n';
import { DownloadEvents } from './interfaces/events';
import { Session } from './session';
import { Uri } from './util/uri';

function streamVcpkg(vcpkgCommand: string | undefined, args: Array<string>, listener: (chunk: any) => void): Promise<void> {
  return new Promise((accept, reject) => {
    if (!vcpkgCommand) {
      reject(i`VCPKG_COMMAND was not set`);
      return;
    }

    const subproc = spawn(vcpkgCommand, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    subproc.stdout.on('data', listener);
    subproc.stderr.pipe(process.stdout);
    subproc.on('error', (err) => { reject(err); });
    subproc.on('close', (code: number) => {
      if (code === 0) {
        accept();
        return;
      }
      reject(i`Running vcpkg internally returned a nonzero exit code: ${code}`);
    });
  });
}

async function runVcpkg(vcpkgCommand: string | undefined, args: Array<string>): Promise<string> {
  let result = '';
  await streamVcpkg(vcpkgCommand, args, (chunk) => { result += chunk; });
  return result.trimEnd();
}

export function vcpkgFetch(session: Session, fetchKey: string): Promise<string> {
  return runVcpkg(session.vcpkgCommand, ['fetch', fetchKey, '--x-stderr-status']).then((output) => {
    return output;
  }, (error) => {
    if (fetchKey === 'git') {
      session.channels.warning('failed to fetch git, falling back to attempting to use git from the PATH');
      return Promise.resolve('git');
    }

    return Promise.reject(error);
  });
}

export async function vcpkgExtract(session: Session, archive: string, target:string, strip?:number|string): Promise<string> {
  const args: Array<string> = ['z-extract', archive, target];
  if (strip)
  {
    args.push(`--strip=${strip}`);
  }

  return runVcpkg(session.vcpkgCommand, args);
}

export async function vcpkgDownload(session: Session, destination: string, sha512: string | undefined, uris: Array<Uri>, events: Partial<DownloadEvents>) : Promise<void> {
  const args = ['x-download', destination, '--z-machine-readable-progress'];
  if (sha512) {
    args.push(`--sha512=${sha512}`);
  } else {
    args.push('--skip-sha512');
  }

  for (const uri of uris) {
    events.downloadProgress?.(uri, destination, 0);
    const uriArgs = [...args, `--url=${uri.toString()}`];
    try {
      await streamVcpkg(session.vcpkgCommand, uriArgs, (chunk) => {
        const match = /(\d+)(\.\d+)?%\s*$/.exec(chunk);
        if (!match) { return; }
        const number = parseFloat(match[1]);
        // throwing out 100s avoids displaying temporarily full progress bars resulting from redirects getting resolved
        if (number && number < 100) {
          events.downloadProgress?.(uri, destination, number);
        }
      });

      return;
    } catch {
      session.channels.warning(i`failed to download from ${uri.toString()}`);
    }
  }

  throw new Error(i`failed to download ${destination} from any source`);
}
