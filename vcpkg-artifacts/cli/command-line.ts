// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

import { tmpdir } from 'os';
import { join, resolve } from 'path';
import { intersect } from '../util/intersect';
import { Command } from './command';

export type switches = {
  [key: string]: Array<string>;
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

export class CommandLine {
  readonly commands = new Array<Command>();
  readonly inputs = new Array<string>();
  readonly switches: switches = {};
  readonly context: Ctx & switches;

  #home?: string;
  get homeFolder() {
    // home folder is determined by
    // command line ( --vcpkg-root )
    // environment (VCPKG_ROOT)
    // default 1 $HOME/.vcpkg
    // default 2 <tmpdir>/.vcpkg

    // note, this does not create the folder, that would happen when the session is initialized.

    return this.#home || (this.#home = resolve(
      this.switches['vcpkg-root']?.[0] ||
      process.env['VCPKG_ROOT'] ||
      join(process.env['HOME'] || process.env['USERPROFILE'] || tmpdir(), '.vcpkg')));
  }

  get vcpkgCommand() {
    return this.switches['z-vcpkg-command']?.[0];
  }

  get force() {
    return !!this.switches['force'];
  }

  get debug() {
    return !!this.switches['debug'];
  }

  get vcpkgArtifactsRoot() {
    return this.switches['z-vcpkg-artifacts-root']?.[0];
  }

  get vcpkgDownloads() {
    return this.switches['z-vcpkg-downloads']?.[0];
  }

  get vcpkgRegistriesCache() {
    return this.switches['z-vcpkg-registries-cache']?.[0];
  }

  get telemetryFile() {
    return this.switches['z-telemetry-file']?.[0];
  }

  get nextPreviousEnvironment() {
    return this.switches['z-next-previous-environment']?.[0];
  }

  get globalConfig() {
    return this.switches['z-global-config']?.[0];
  }

  get language() {
    const l = this.switches['language'] || [];
    return l[0];
  }

  get allLanguages(): boolean {
    const l = this.switches['all-languages'] || [];
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
    return this.commands.find(cmd => cmd.command === this.inputs[0]);
  }

  constructor(args: Array<string>) {
    for (let i = 0; i < args.length; i++) {
      const arg = args[i];
      // eslint-disable-next-line prefer-const
      let [, name, , value] = /^--([^=:]+)([=:])?(.+)?$/g.exec(arg) || [];
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
