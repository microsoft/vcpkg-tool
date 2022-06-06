// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { spawn } from 'child_process';
import { i } from './i18n';
import { Session } from './session';

/** @internal */
export class Vcpkg {
  constructor(private readonly session: Session) { }

  fetch(fetchKey: string): Promise<string> {
    return this.runVcpkg(['fetch', fetchKey, '--x-stderr-status']).then((output) => {
      return output.trimEnd();
    }, (error) => {
      if (fetchKey === 'git') {
        this.session.channels.warning('failed to fetch git, falling back to attempting to use git from the PATH');
        return Promise.resolve('git');
      }

      return Promise.reject(error);
    });
  }

  private runVcpkg(args: Array<string>): Promise<string> {
    return new Promise((accept, reject) => {
      if (!this.session.vcpkgCommand) {
        reject(i`VCPKG_COMMAND was not set`);
        return;
      }

      const subproc = spawn(this.session.vcpkgCommand, args, { stdio: ['ignore', 'pipe', 'pipe'] });
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
}
