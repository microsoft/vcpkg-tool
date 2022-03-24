// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { strict } from 'assert';
import { tmpdir } from 'os';
import { join, resolve } from 'path';
import { i } from '../i18n';
import { intersect } from '../util/intersect';
import { Command } from './command';
import { cmdSwitch } from './format';

export type switches = {
  [key: string]: Array<string>;
}

export interface Help {
  readonly help: Array<string>;
  readonly title: string;
}

class Ctx {
  constructor(cmdline: CommandLine) {
    this.os =
      cmdline.isSet('windows') ? 'win32' :
        cmdline.isSet('osx') ? 'darwin' :
          cmdline.isSet('linux') ? 'linux' :
            cmdline.isSet('freebsd') ? 'freebsd' :
              process.platform;
    this.arch = cmdline.isSet('x64') ? 'x64' :
      cmdline.isSet('x86') ? 'x32' :
        cmdline.isSet('arm') ? 'arm' :
          cmdline.isSet('arm64') ? 'arm64' :
            process.arch;
  }

  readonly os: string;
  readonly arch: string;

  get windows(): boolean {
    return this.os === 'win32';
  }

  get linux(): boolean {
    return this.os === 'linux';
  }

  get freebsd(): boolean {
    return this.os === 'freebsd';
  }

  get osx(): boolean {
    return this.os === 'darwin';
  }

  get x64(): boolean {
    return this.arch === 'x64';
  }

  get x86(): boolean {
    return this.arch === 'x32';
  }

  get arm(): boolean {
    return this.arch === 'arm';
  }

  get arm64(): boolean {
    return this.arch === 'arm64';
  }
}

export function resolvePath(v: string | undefined) {
  return v?.startsWith('.') ? resolve(v) : v;
}

export class CommandLine {
  readonly commands = new Array<Command>();
  readonly inputs = new Array<string>();
  readonly switches: switches = {};
  readonly context: Ctx & switches;

  #home?: string;
  get homeFolder() {
    // home folder is determined by
    // command line (--vcpkg_root, --vcpkg-root )
    // environment (VCPKG_ROOT)
    // default 1 $HOME/.vcpkg
    // default 2 <tmpdir>/.vcpkg

    // note, this does not create the folder, that would happen when the session is initialized.

    return this.#home || (this.#home = resolvePath(
      this.switches['vcpkg-root']?.[0] ||
      this.switches['vcpkg_root']?.[0] ||
      process.env['VCPKG_ROOT'] ||
      join(process.env['HOME'] || process.env['USERPROFILE'] || tmpdir(), '.vcpkg')));
  }

  get force() {
    return !!this.switches['force'];
  }

  get debug() {
    return !!this.switches['debug'];
  }

  get fromVCPKG() {
    return !!this.switches['from-vcpkg'];
  }

  get language() {
    const l = this.switches['language'] || [];
    strict.ok((l?.length || 0) < 2, i`Expected a single value for ${cmdSwitch('language')} - found multiple`);
    return l[0] || Intl.DateTimeFormat().resolvedOptions().locale;
  }

  get allLanguages(): boolean {
    const l = this.switches['all-languages'] || [];
    strict.ok((l?.length || 0) < 2, i`Expected a single value for ${cmdSwitch('all-languages')} - found multiple`);
    return !!l[0];
  }

  isSet(sw: string) {
    const s = this.switches[sw];
    if (s && s.last !== 'false') {
      return true;
    }
    return false;
  }

  claim(sw: string) {
    const v = this.switches[sw];
    delete this.switches[sw];
    return v;
  }

  addCommand(command: Command) {
    this.commands.push(command);
  }

  /** parses the command line and returns the command that has been requested */
  get command() {
    return this.commands.find(cmd => cmd.command === this.inputs[0] || !!cmd.aliases.find(alias => alias === this.inputs[0]));
  }

  constructor(args: Array<string>) {
    for (let i = 0; i < args.length; i++) {
      const arg = args[i];
      // eslint-disable-next-line prefer-const
      let [, name, sep, value] = /^--([^=:]+)([=:])?(.+)?$/g.exec(arg) || [];
      if (name) {
        if (!value) {
          if (i + 1 < args.length && !args[i + 1].startsWith('--')) {
            // if you say --foo bar then bar is the value
            value = args[++i];
          }
        }
        this.switches[name] = this.switches[name] === undefined ? [] : this.switches[name];
        this.switches[name].push(value);
        continue;
      }
      this.inputs.push(arg);
    }

    this.context = intersect(new Ctx(this), this.switches);
  }
}
